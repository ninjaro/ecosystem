#include "internal.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ecosystem::release_detail {

bool is_elf_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    char magic[4] {};
    input.read(magic, sizeof(magic));
    return input.gcount() == 4 && magic[0] == '\x7f' && magic[1] == 'E'
        && magic[2] == 'L' && magic[3] == 'F';
}

std::string cmake_path_argument(const fs::path& path) {
    const auto value = path.generic_string();
    if (value.find_first_of(";\r\n") != std::string::npos)
        throw std::runtime_error(
            "package runtime paths cannot contain semicolons or line breaks: "
            + value
        );
    std::string delimiter = "=";
    while (value.find("]" + delimiter + "]") != std::string::npos)
        delimiter += "=";
    return "[" + delimiter + "[" + value + "]" + delimiter + "]";
}

bool path_within(const fs::path& path, const fs::path& root) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute()
        && std::find(relative.begin(), relative.end(), fs::path(".."))
        == relative.end();
}

bool stage_runtime(
    const fs::path& project_root, const fs::path& stage_root,
    const fs::path& work_dir, std::map<fs::path, fs::path>* payload_sources,
    std::set<fs::path>* system_libraries, std::string* error_message
) {
    std::string executables;
    std::string libraries;
    for (const auto& [destination, source] : *payload_sources) {
        if (!is_elf_file(source))
            continue;
        auto& inputs = destination.parent_path() == stage_root / "usr/bin"
            ? executables
            : libraries;
        inputs += "    " + cmake_path_argument(source) + "\n";
    }
    if (executables.empty() && libraries.empty())
        return true;

    const auto cmake = find_command_path("cmake");
    if (cmake.empty()) {
        *error_message
            = "ELF package runtime inspection requires cmake and objdump";
        return false;
    }
    const auto resolved_path = work_dir / "runtime-libraries.txt";
    const auto script_path = work_dir / "runtime-scan.cmake";
    const auto script = render_text_template(
        "release/runtime_scan.cmake.tpl",
        { { "executables", executables },
          { "libraries", libraries },
          { "directories", "" },
          { "resolved_path", cmake_path_argument(resolved_path) } },
        error_message
    );
    if (!error_message->empty()
        || !write_text_file(script_path, script, error_message))
        return false;
    const auto scanned = capture_command_result(
        { cmake, "-P", script_path.string() }, work_dir
    );
    if (!write_text_file(
            work_dir / "runtime-scan.log", scanned.output, error_message
        ))
        return false;
    if (scanned.exit_code != 0) {
        *error_message = "package runtime dependency inspection failed:\n"
            + scanned.output;
        return false;
    }
    const auto resolved_text = read_text_file(resolved_path, error_message);
    if (!error_message->empty())
        return false;
    std::istringstream resolved(resolved_text);
    json inventory { { "bundled", json::array() },
                     { "system", json::array() } };
    const auto owned_root = fs::canonical(project_root);
    for (std::string line; std::getline(resolved, line);) {
        if (line.empty())
            continue;
        const fs::path dependency(line);
        const auto canonical = fs::canonical(dependency);
        if (!path_within(canonical, owned_root)) {
            bool system = false;
            for (const auto& prefix :
                 { "/lib", "/lib64", "/usr/lib", "/usr/lib64" }) {
                if (path_within(canonical, fs::weakly_canonical(prefix))) {
                    system = true;
                    break;
                }
            }
            if (!system) {
                *error_message = "unsupported package runtime library outside "
                                 "the project and system library directories: "
                    + dependency.string()
                    + "; use an installed system package or a managed source "
                      "provider";
                return false;
            }
            inventory["system"].push_back(dependency.generic_string());
            system_libraries->insert(canonical);
            continue;
        }

        // Keep every name in a SONAME symlink chain. Regular payload copies
        // remain compatible with the existing archive and file-list machinery.
        fs::path source = dependency;
        std::set<fs::path> seen;
        while (seen.insert(source).second) {
            const auto destination = stage_root / "usr/lib" / source.filename();
            const auto existing = payload_sources->find(destination);
            if (existing != payload_sources->end()) {
                if (!fs::equivalent(existing->second, source)) {
                    *error_message = "package runtime payload collision: "
                        + destination.string() + " from " + source.string()
                        + " and " + existing->second.string();
                    return false;
                }
            } else {
                std::error_code error;
                fs::create_directories(destination.parent_path(), error);
                if (!error)
                    fs::copy_file(
                        source, destination, fs::copy_options::none, error
                    );
                if (error) {
                    *error_message = "unable to stage runtime library "
                        + source.string() + ": " + error.message();
                    return false;
                }
                payload_sources->emplace(destination, source);
            }
            inventory["bundled"].push_back(
                { { "source", source.generic_string() },
                  { "installed",
                    destination.lexically_relative(stage_root)
                        .generic_string() } }
            );
            if (!fs::is_symlink(source))
                break;
            const auto target = fs::read_symlink(source);
            source = (target.is_absolute() ? target
                                           : source.parent_path() / target)
                         .lexically_normal();
        }
    }

    std::string files;
    executables.clear();
    libraries.clear();
    for (const auto& [destination, source] : *payload_sources) {
        if (!is_elf_file(source))
            continue;
        const auto argument = "    " + cmake_path_argument(destination) + "\n";
        files += argument;
        auto& inputs = destination.parent_path() == stage_root / "usr/bin"
            ? executables
            : libraries;
        inputs += argument;
    }
    const auto strip_path = work_dir / "runtime-relocate.cmake";
    const auto strip = render_text_template(
        "release/runtime_relocate.cmake.tpl", { { "files", files } },
        error_message
    );
    if (!error_message->empty()
        || !write_text_file(strip_path, strip, error_message))
        return false;
    const auto relocated = capture_command_result(
        { cmake, "-P", strip_path.string() }, work_dir
    );
    if (!write_text_file(
            work_dir / "runtime-relocate.log", relocated.output, error_message
        ))
        return false;
    if (relocated.exit_code != 0) {
        *error_message = "unable to remove build paths from package payload:\n"
            + relocated.output;
        return false;
    }
    // Inspect the shipped copies as well. This rejects any dependency that
    // still resolves into a private tree after relocation, including absolute
    // DT_NEEDED entries that cannot be repaired by removing an RPATH.
    const auto verified_path = work_dir / "runtime-installed-libraries.txt";
    const auto verify_path = work_dir / "runtime-verify.cmake";
    const auto verify = render_text_template(
        "release/runtime_scan.cmake.tpl",
        { { "executables", executables },
          { "libraries", libraries },
          { "directories",
            "    " + cmake_path_argument(stage_root / "usr/lib") + "\n" },
          { "resolved_path", cmake_path_argument(verified_path) } },
        error_message
    );
    if (!error_message->empty()
        || !write_text_file(verify_path, verify, error_message))
        return false;
    const auto verified = capture_command_result(
        { cmake, "-P", verify_path.string() }, work_dir
    );
    if (!write_text_file(
            work_dir / "runtime-verify.log", verified.output, error_message
        ))
        return false;
    if (verified.exit_code != 0) {
        *error_message
            = "packaged runtime verification failed:\n" + verified.output;
        return false;
    }
    const auto verified_text = read_text_file(verified_path, error_message);
    if (!error_message->empty())
        return false;
    std::istringstream installed(verified_text);
    for (std::string line; std::getline(installed, line);) {
        if (line.empty())
            continue;
        const auto dependency = fs::canonical(line);
        if (!path_within(dependency, fs::canonical(stage_root))
            && !system_libraries->contains(dependency)) {
            *error_message
                = "packaged runtime still depends on a private library: "
                + line;
            return false;
        }
    }
    return write_text_file(
        work_dir / "runtime.json", inventory.dump(2) + "\n", error_message
    );
}

bool resolve_runtime_packages(
    const std::set<fs::path>& libraries, const std::string& package_name,
    const fs::path& work_dir, prerelease_artifacts* artifacts,
    std::string* error_message
) {
    artifacts->pacman_dependencies.clear();
    artifacts->debian_dependencies.clear();
    json mapping { { "scope", "ELF system libraries" },
                   { "libraries", json::array() },
                   { "dependencies", json::array() } };
    const auto pacman = find_command_path("pacman");
    const auto dpkg = find_command_path("dpkg-query");
    const bool use_pacman = !pacman.empty();
    const std::string backend = use_pacman ? "Pacman" : "Debian";
    mapping["backend"] = libraries.empty() ? "none" : backend;
    if (!libraries.empty() && pacman.empty() && dpkg.empty()) {
        *error_message = "ELF runtime dependency mapping requires pacman or "
                         "dpkg-query and its installed package database; build "
                         "on Arch/Manjaro or Debian/Ubuntu";
        return false;
    }
    std::map<std::string, std::string> versions;
    for (const auto& library : libraries) {
        captured_command owned;
        std::string owner;
        if (use_pacman) {
            owned = capture_command_result(
                { pacman, "-Qqo", "--", library.string() }, work_dir,
                { { "LC_ALL", "C" } }
            );
            owner = trim(owned.output);
        } else {
            std::vector<fs::path> candidates { library };
            // Merged-/usr hosts can retain pre-merge paths in dpkg's file
            // lists.
            if (library.string().starts_with("/usr/lib")) {
                const fs::path alias = library.string().substr(4);
                std::error_code error;
                if (fs::equivalent(alias, library, error) && !error)
                    candidates.push_back(alias);
            }
            for (const auto& candidate : candidates) {
                owned = capture_command_result(
                    { dpkg, "-S", candidate.string() }, work_dir,
                    { { "LC_ALL", "C" } }
                );
                if (owned.exit_code != 0)
                    continue;
                const auto record = trim(owned.output);
                const auto separator = record.find(": ");
                if (separator != std::string::npos
                    && record.substr(separator + 2) == candidate.string())
                    owner = record.substr(0, separator);
                break;
            }
        }
        if (owned.exit_code != 0
            || !std::regex_match(
                owner,
                use_pacman ? pacman_package_pattern : debian_package_pattern
            )) {
            *error_message = "unable to identify one installed " + backend
                + " owner for runtime library " + library.string() + ":\n"
                + owned.output;
            return false;
        }
        if (owner.substr(0, owner.find(':')) == package_name) {
            *error_message
                = "release package name conflicts with required system package "
                + owner;
            return false;
        }
        if (!versions.contains(owner)) {
            const auto queried = capture_command_result(
                use_pacman ? string_list { pacman, "-Q", "--", owner }
                           : string_list { dpkg, "-W",
                                           "-f=${Package} ${Version} "
                                           "${db:Status-Status}\\n",
                                           "--", owner },
                work_dir, { { "LC_ALL", "C" } }
            );
            std::istringstream record(queried.output);
            std::string name, version, extra, installed_status;
            record >> name >> version;
            if (!use_pacman)
                record >> installed_status;
            if (queried.exit_code != 0
                || name != owner.substr(0, owner.find(':'))
                || (!use_pacman && installed_status != "installed")
                || !std::regex_match(version, pacman_version_pattern)
                || (record >> extra)) {
                *error_message = "unable to read installed " + backend
                    + " version for " + owner + ":\n" + queried.output;
                return false;
            }
            versions.emplace(owner, version);
        }
        mapping["libraries"].push_back({ { "path", library.generic_string() },
                                         { "package", owner },
                                         { "version", versions.at(owner) } });
    }
    auto& dependencies = use_pacman ? artifacts->pacman_dependencies
                                    : artifacts->debian_dependencies;
    for (const auto& [name, version] : versions)
        dependencies.push_back(
            use_pacman ? name + ">=" + version : name + " (>= " + version + ")"
        );
    mapping["dependencies"] = dependencies;
    if (!libraries.empty()) {
        const std::string other_backend = use_pacman ? "Debian" : "Pacman";
        artifacts->warnings.push_back(
            other_backend
            + " runtime dependency metadata is incomplete; that archive "
              "requires manual dependency review. "
            + backend + " dependencies were resolved from this host."
        );
    }
    return write_text_file(
        work_dir / "runtime-packages.json", mapping.dump(2) + "\n",
        error_message
    );
}

} // namespace ecosystem::release_detail
