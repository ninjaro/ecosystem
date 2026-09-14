#include "workspace/source_dependencies.hpp"

#include "workspace/project.hpp"
#include "workspace/sync.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ecosystem {
namespace {
    namespace fs = std::filesystem;

    std::string trimmed(std::string value) {
        while (!value.empty()
               && std::isspace(static_cast<unsigned char>(value.back())))
            value.pop_back();
        return value;
    }

    std::string execute(const string_list& arguments, const fs::path& cwd) {
        const auto result = capture_command_result(arguments, cwd);
        if (result.exit_code != 0) {
            throw std::runtime_error(
                arguments.front()
                + " failed while preparing source dependency:\n" + result.output
            );
        }
        return trimmed(result.output);
    }

    void write(const fs::path& path, const std::string& text) {
        std::string error;
        if (!write_text_file(path, text, &error))
            throw std::runtime_error(error);
    }

    json read_json(const fs::path& path) {
        std::string error;
        const auto text = read_text_file(path, &error);
        if (!error.empty())
            throw std::runtime_error(error);
        return json::parse(text);
    }

    // A stable directory key, not a trust check. The complete intent is checked
    // again before using a cached checkout, including in the event of a
    // collision.
    std::string intent_key(const json& intent) {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const char character : intent.dump()) {
            hash ^= static_cast<unsigned char>(character);
            hash *= 1099511628211ULL;
        }
        std::ostringstream out;
        out << std::hex << hash;
        return out.str();
    }

    fs::path checkout(
        const source_dependency& dependency, const fs::path& state,
        const json& intent
    ) {
        const auto source = state / "source";
        const auto selection = state / "selection.json";
        if (!fs::exists(selection)) {
            if (fs::exists(source)) {
                throw std::runtime_error(
                    "incomplete managed checkout at " + source.string()
                    + "; remove this generated dependency state and retry"
                );
            }
            fs::create_directories(state);
            execute(
                { "git", "clone", "--no-checkout", "--", dependency.repository,
                  source.string() },
                state
            );
            auto revision = capture_command_result(
                { "git", "rev-parse", "--verify", "--end-of-options",
                  dependency.revision + "^{commit}" },
                source
            );
            if (revision.exit_code != 0) {
                revision = capture_command_result(
                    { "git", "rev-parse", "--verify", "--end-of-options",
                      "refs/remotes/origin/" + dependency.revision
                          + "^{commit}" },
                    source
                );
            }
            if (revision.exit_code != 0)
                throw std::runtime_error(
                    "unable to resolve source revision " + dependency.revision
                    + ":\n" + revision.output
                );
            const auto commit = trimmed(revision.output);
            execute({ "git", "checkout", "--detach", commit }, source);
            write(
                selection,
                json({ { "intent", intent }, { "commit", commit } }).dump(2)
                    + "\n"
            );
        }
        const auto selected = read_json(selection);
        if (selected.at("intent") != intent)
            throw std::runtime_error(
                "source dependency cache intent mismatch: " + selection.string()
            );
        const auto head = execute({ "git", "rev-parse", "HEAD" }, source);
        if (head != selected.at("commit").get<std::string>()
            || !execute(
                    { "git", "status", "--porcelain", "--untracked-files=all" },
                    source
            )
                    .empty()) {
            throw std::runtime_error(
                "managed source checkout changed: " + source.string() + "; use "
                + source_dependency_override(dependency)
                + " for mutable local work"
            );
        }
        return source;
    }

    struct preparation {
        std::string profile;
        fs::path storage_root;
        std::set<std::string> active;
        std::map<std::string, json> selected_packages;

        string_list prepare(
            const fs::path& project, const manifest& value, const fs::path& root
        ) {
            if (!active.insert(value.id).second)
                throw std::runtime_error(
                    "source dependency cycle at " + value.id
                );
            string_list options;
            for (const auto& owner : value.components) {
                const auto dependency = source_dependency_for(owner);
                if (!dependency)
                    continue;
                if (profile == "android")
                    throw std::runtime_error(
                        "managed source dependency preparation currently "
                        "requires a desktop profile"
                    );
                const auto variable = source_dependency_override(*dependency);
                const char* override_value = std::getenv(variable.c_str());
                const std::string local = override_value ? override_value : "";
                const json intent { { "repository", dependency->repository },
                                    { "revision", dependency->revision },
                                    { "local", local } };
                const auto [existing, inserted]
                    = selected_packages.emplace(dependency->package, intent);
                if (!inserted && existing->second != intent)
                    throw std::runtime_error(
                        "conflicting source selections for package "
                        + dependency->package
                    );
                const auto state
                    = root / dependency->package / intent_key(intent);
                string_list paths;
                for (const auto& destination :
                     { state, state / "source", state / "selection.json",
                       state / "project/CMakeLists.txt",
                       state / "build" / profile,
                       state / "install" / profile }) {
                    const auto issues = validate_project_paths(
                        storage_root,
                        { destination.lexically_relative(storage_root) }
                    );
                    paths.insert(paths.end(), issues.begin(), issues.end());
                }
                if (!paths.empty())
                    throw std::runtime_error(paths.front());
                fs::path source;
                if (!local.empty()) {
                    if (!fs::path(local).is_absolute())
                        throw std::runtime_error(
                            variable + " must be an absolute path"
                        );
                    source = fs::canonical(local);
                } else {
                    source = checkout(*dependency, state, intent);
                }
                const auto loaded = load_manifest(source / "manifest.json");
                if (!loaded.value || !loaded.errors.empty()) {
                    std::string message
                        = "invalid provider manifest at " + source.string();
                    for (const auto& error : loaded.errors)
                        message += "\n" + error;
                    throw std::runtime_error(message);
                }
                const auto& provider = *loaded.value;
                if (provider.id != dependency->package)
                    throw std::runtime_error(
                        "provider package identity mismatch: expected "
                        + dependency->package + ", found " + provider.id
                    );
                const auto artifact
                    = resolve_artifact(provider, dependency->artifact);
                if (!artifact
                    || artifact->artifact_value->kind
                        != owner.artifacts.front().kind)
                    throw std::runtime_error(
                        "provider artifact missing or kind mismatch: "
                        + format_artifact_ref(dependency->artifact)
                    );
                if (!artifact_has_cmake_export(
                        provider, dependency->artifact
                    )) {
                    throw std::runtime_error(
                        "provider artifact is not exported: "
                        + format_artifact_ref(dependency->artifact)
                        + "; select its library as facade or install_artifacts"
                    );
                }
                const auto provider_paths
                    = validate_manifest_paths(provider, source);
                if (!provider_paths.empty())
                    throw std::runtime_error(provider_paths.front());
                const auto provider_dependencies
                    = prepare(source, provider, state / "dependencies");
                auto provider_options = provider_dependencies;
                options.insert(
                    options.end(), provider_dependencies.begin(),
                    provider_dependencies.end()
                );
                const auto generated = state / "project";
                const auto build = state / "build" / profile;
                const auto prefix = state / "install" / profile;
                write(
                    generated / "CMakeLists.txt",
                    generate_developer_cmakelists(provider, source)
                );
                provider_options.insert(
                    provider_options.end(),
                    { "-DECOSYSTEM_PROJECT_ROOT:PATH=" + source.string(),
                      "-DECOSYSTEM_BUILD_TESTS=OFF",
                      "-DECOSYSTEM_BUILD_BENCHMARKS=OFF",
                      "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
                      "-DCMAKE_INSTALL_LIBDIR=lib",
                      "-DCMAKE_INSTALL_INCLUDEDIR=include",
                      "-DCMAKE_INSTALL_PREFIX:PATH=" + prefix.string(),
                      std::string("-DECOSYSTEM_PROFILE_KDE=")
                          + (profile == "kde" ? "ON" : "OFF") }
                );
                std::string error;
                if (configure_cmake_source_tree(
                        generated, build, provider_options,
                        profile == "release" ? "Release" : "Debug", &error, true
                    )
                    != command_error::ok)
                    throw std::runtime_error(error);
                execute({ "cmake", "--build", build.string() }, project);
                fs::remove_all(prefix);
                execute({ "cmake", "--install", build.string() }, project);
                const auto package_dir = prefix / "lib/cmake" / provider.id;
                if (!fs::is_regular_file(
                        package_dir / (provider.id + "Config.cmake")
                    )) {
                    throw std::runtime_error(
                        "provider must install a CMake package for "
                        + provider.id
                        + "; select its library as facade or install_artifacts"
                    );
                }
                options.push_back(
                    "-D" + provider.id + "_DIR:PATH=" + package_dir.string()
                );
            }
            active.erase(value.id);
            return options;
        }
    };
}

std::optional<source_dependency> source_dependency_for(const component& value) {
    if (!value.stack.is_object() || !value.stack.contains("external_project"))
        return std::nullopt;
    const auto& entry = value.stack.at("external_project");
    if (!entry.is_object())
        return std::nullopt;
    for (const auto* key :
         { "repository", "revision", "package", "artifact" }) {
        if (!entry.contains(key) || !entry.at(key).is_string())
            return std::nullopt;
    }
    const auto artifact
        = parse_artifact_ref(entry.at("artifact").get<std::string>());
    if (!artifact)
        return std::nullopt;
    return source_dependency { entry.at("repository").get<std::string>(),
                               entry.at("revision").get<std::string>(),
                               entry.at("package").get<std::string>(),
                               *artifact };
}

std::string source_dependency_override(const source_dependency& value) {
    auto name = value.repository;
    while (!name.empty() && name.back() == '/')
        name.pop_back();
    const auto slash = name.find_last_of('/');
    if (slash != std::string::npos)
        name.erase(0, slash + 1);
    if (name.ends_with(".git"))
        name.resize(name.size() - 4);
    for (char& character : name) {
        const auto byte = static_cast<unsigned char>(character);
        character
            = std::isalnum(byte) ? static_cast<char>(std::toupper(byte)) : '_';
    }
    return name + "_SOURCE_DIR";
}

command_error prepare_source_dependencies(
    const fs::path& project_root, const manifest& value,
    const std::string& profile, string_list* cmake_options,
    std::string* error_message
) try {
    preparation state { profile, project_root, {}, {} };
    *cmake_options = state.prepare(
        project_root, value, local_state_dir(project_root) / "dependencies"
    );
    return command_error::ok;
} catch (const std::exception& error) {

    if (error_message)
        *error_message = error.what();
    return command_error::task_failed;
}

} // namespace ecosystem
