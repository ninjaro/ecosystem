#include "workspace/release.hpp"

#include "workspace/template_text.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace ecosystem {
namespace release_support {

const std::regex semver_pattern("^[0-9]+\\.[0-9]+\\.[0-9]+$");

struct prerelease_package_state {
    std::string package_name;
    std::string description;
    std::string artifact;
    std::string base_version = "0.1.0";
    int next_prerelease = 1;
    std::string latest_logical_version;
    std::string latest_debian_version;
    std::string latest_debian_architecture;
    std::string latest_debian_package;
    std::string latest_pacman_version;
    std::string latest_pacman_architecture;
    std::string latest_pacman_package;
    std::uintmax_t latest_installed_size = 0U;
    std::int64_t latest_build_epoch = 0;
    std::vector<std::string> latest_files;
};

struct prerelease_state {
    std::vector<prerelease_package_state> packages;
};

std::string trim_copy(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

std::string env_or_empty(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

std::string env_or_empty_with_fallback(
    const char* preferred_name, const char* fallback_name
) {
    std::string value = env_or_empty(preferred_name);
    if (!value.empty()) {
        return value;
    }
    return env_or_empty(fallback_name);
}

std::optional<std::string> effective_signing_key(const prerelease_signing_options& options) {
    if (options.key_id.has_value() && !options.key_id->empty()) {
        return options.key_id;
    }

    const std::string prerelease_key = trim_copy(env_or_empty_with_fallback(
        "MANIFESTO_PRERELEASE_SIGN_KEY", "ECOSYSTEM_PRERELEASE_SIGN_KEY"
    ));
    if (!prerelease_key.empty()) {
        return prerelease_key;
    }

    const std::string generic_key = trim_copy(
        env_or_empty_with_fallback("MANIFESTO_GPG_KEY", "ECOSYSTEM_GPG_KEY")
    );
    if (!generic_key.empty()) {
        return generic_key;
    }

    return std::nullopt;
}

std::string join_space_separated(const std::set<std::string>& values) {
    std::ostringstream joined;
    bool first = true;
    for (const std::string& value : values) {
        if (!first) {
            joined << " ";
        }
        joined << value;
        first = false;
    }
    return joined.str();
}

std::set<std::string> debian_architectures(const prerelease_state& state) {
    std::set<std::string> architectures;
    for (const prerelease_package_state& package : state.packages) {
        if (!package.latest_debian_package.empty() && !package.latest_debian_architecture.empty()) {
            architectures.insert(package.latest_debian_architecture);
        }
    }
    return architectures;
}

void remove_if_exists(const fs::path& path) {
    std::error_code error;
    fs::remove(path, error);
}

std::string format_rfc2822_utc(const std::int64_t build_epoch) {
    const std::time_t raw_time = static_cast<std::time_t>(build_epoch);
    const std::tm* utc = std::gmtime(&raw_time);
    if (utc == nullptr) {
        return {};
    }

    std::ostringstream formatted;
    formatted << std::put_time(utc, "%a, %d %b %Y %H:%M:%S +0000");
    return formatted.str();
}

std::string normalize_package_token(const std::string& value) {
    std::string normalized;
    bool last_was_dash = false;
    for (const char character : value) {
        const unsigned char raw = static_cast<unsigned char>(character);
        if (std::isalnum(raw)) {
            normalized.push_back(
                static_cast<char>(std::tolower(raw))
            );
            last_was_dash = false;
            continue;
        }
        if (!last_was_dash) {
            normalized.push_back('-');
            last_was_dash = true;
        }
    }

    while (!normalized.empty() && normalized.front() == '-') {
        normalized.erase(normalized.begin());
    }
    while (!normalized.empty() && normalized.back() == '-') {
        normalized.pop_back();
    }
    if (normalized.empty()) {
        return "package";
    }
    return normalized;
}

std::string package_description(
    const manifest& manifest_value, const resolved_artifact& resolved
) {
    const std::optional<artifact_ref> facade_ref = parse_artifact_ref(manifest_value.facade_entry_artifact);
    if (facade_ref.has_value() && format_artifact_ref(*facade_ref) == format_artifact_ref(resolved.ref)) {
        return manifest_value.description;
    }
    return manifest_value.description + " (" + format_artifact_ref(resolved.ref) + ")";
}

std::string packager_string() {
    const std::string packager = trim_copy(env_or_empty_with_fallback(
        "MANIFESTO_PACKAGER", "ECOSYSTEM_PACKAGER"
    ));
    if (!packager.empty()) {
        return packager;
    }
    const std::string generic_packager = trim_copy(env_or_empty("PACKAGER"));
    if (!generic_packager.empty()) {
        return generic_packager;
    }
    return "ecosystem prerelease <noreply@local>";
}

std::string native_machine() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__)
    return "aarch64";
#elif defined(__arm__) || defined(_M_ARM)
    return "armv7l";
#else
    return trim_copy(capture_command({ "uname", "-m" }));
#endif
}

std::string debian_architecture_for_machine(const std::string& machine) {
    if (machine == "x86_64") {
        return "amd64";
    }
    if (machine == "aarch64") {
        return "arm64";
    }
    if (machine == "armv7l" || machine == "armv6l") {
        return "armhf";
    }
    if (machine.empty()) {
        return "amd64";
    }
    return machine;
}

std::string pacman_architecture_for_machine(const std::string& machine) {
    if (machine.empty()) {
        return "x86_64";
    }
    return machine;
}

std::string logical_prerelease_version(const std::string& base_version, const int number) {
    return base_version + "-pre." + std::to_string(number);
}

std::string debian_prerelease_version(const std::string& base_version, const int number) {
    return base_version + "~pre." + std::to_string(number);
}

std::string pacman_prerelease_version(const std::string& base_version, const int number) {
    return base_version + "pre" + std::to_string(number);
}

json package_state_to_json(const prerelease_package_state& value) {
    json root = json::object();
    root["package_name"] = value.package_name;
    root["description"] = value.description;
    root["artifact"] = value.artifact;
    root["base_version"] = value.base_version;
    root["next_prerelease"] = value.next_prerelease;
    if (!value.latest_logical_version.empty()) {
        root["latest_logical_version"] = value.latest_logical_version;
    }
    if (!value.latest_debian_version.empty()) {
        root["latest_debian_version"] = value.latest_debian_version;
        root["latest_debian_architecture"] = value.latest_debian_architecture;
        root["latest_debian_package"] = value.latest_debian_package;
    }
    if (!value.latest_pacman_version.empty()) {
        root["latest_pacman_version"] = value.latest_pacman_version;
        root["latest_pacman_architecture"] = value.latest_pacman_architecture;
        root["latest_pacman_package"] = value.latest_pacman_package;
    }
    if (value.latest_installed_size > 0U) {
        root["latest_installed_size"] = value.latest_installed_size;
    }
    if (value.latest_build_epoch > 0) {
        root["latest_build_epoch"] = value.latest_build_epoch;
    }
    if (!value.latest_files.empty()) {
        root["latest_files"] = value.latest_files;
    }
    return root;
}

prerelease_package_state package_state_from_json(const json& root) {
    prerelease_package_state value;
    if (root.contains("package_name") && root.at("package_name").is_string()) {
        value.package_name = root.at("package_name").get<std::string>();
    }
    if (root.contains("description") && root.at("description").is_string()) {
        value.description = root.at("description").get<std::string>();
    }
    if (root.contains("artifact") && root.at("artifact").is_string()) {
        value.artifact = root.at("artifact").get<std::string>();
    }
    if (root.contains("base_version") && root.at("base_version").is_string()) {
        value.base_version = root.at("base_version").get<std::string>();
    }
    if (root.contains("next_prerelease") && root.at("next_prerelease").is_number_integer()) {
        value.next_prerelease = std::max(1, root.at("next_prerelease").get<int>());
    }
    if (root.contains("latest_logical_version") && root.at("latest_logical_version").is_string()) {
        value.latest_logical_version = root.at("latest_logical_version").get<std::string>();
    }
    if (root.contains("latest_debian_version") && root.at("latest_debian_version").is_string()) {
        value.latest_debian_version = root.at("latest_debian_version").get<std::string>();
    }
    if (root.contains("latest_debian_architecture")
        && root.at("latest_debian_architecture").is_string()) {
        value.latest_debian_architecture = root.at("latest_debian_architecture").get<std::string>();
    }
    if (root.contains("latest_debian_package") && root.at("latest_debian_package").is_string()) {
        value.latest_debian_package = root.at("latest_debian_package").get<std::string>();
    }
    if (root.contains("latest_pacman_version") && root.at("latest_pacman_version").is_string()) {
        value.latest_pacman_version = root.at("latest_pacman_version").get<std::string>();
    }
    if (root.contains("latest_pacman_architecture")
        && root.at("latest_pacman_architecture").is_string()) {
        value.latest_pacman_architecture = root.at("latest_pacman_architecture").get<std::string>();
    }
    if (root.contains("latest_pacman_package") && root.at("latest_pacman_package").is_string()) {
        value.latest_pacman_package = root.at("latest_pacman_package").get<std::string>();
    }
    if (root.contains("latest_installed_size") && root.at("latest_installed_size").is_number_unsigned()) {
        value.latest_installed_size = root.at("latest_installed_size").get<std::uintmax_t>();
    }
    if (root.contains("latest_build_epoch") && root.at("latest_build_epoch").is_number_integer()) {
        value.latest_build_epoch = root.at("latest_build_epoch").get<std::int64_t>();
    }
    if (root.contains("latest_files") && root.at("latest_files").is_array()) {
        for (const json& entry : root.at("latest_files")) {
            if (entry.is_string()) {
                value.latest_files.push_back(entry.get<std::string>());
            }
        }
    }
    return value;
}

fs::path local_prerelease_state_path(const fs::path& project_root) {
    return local_prerelease_dir(project_root) / "state.json";
}

fs::path local_prerelease_deb_dir(const fs::path& project_root) {
    return local_prerelease_dir(project_root) / "deb";
}

fs::path local_prerelease_deb_distribution_dir(const fs::path& project_root) {
    return local_prerelease_deb_dir(project_root) / "dists" / "prerelease";
}

fs::path local_prerelease_deb_binary_dir(
    const fs::path& project_root, const std::string& architecture
) {
    return local_prerelease_deb_distribution_dir(project_root) / "main" / ("binary-" + architecture);
}

fs::path local_prerelease_deb_packages_path(
    const fs::path& project_root, const std::string& architecture
) {
    return local_prerelease_deb_binary_dir(project_root, architecture) / "Packages";
}

fs::path local_prerelease_deb_packages_gz_path(
    const fs::path& project_root, const std::string& architecture
) {
    return local_prerelease_deb_binary_dir(project_root, architecture) / "Packages.gz";
}

fs::path local_prerelease_deb_release_path(const fs::path& project_root) {
    return local_prerelease_deb_distribution_dir(project_root) / "Release";
}

std::string debian_pool_bucket(const std::string& package_name) {
    if (package_name.empty()) {
        return "x";
    }
    return std::string(1U, package_name.front());
}

std::string debian_pool_relative_path(
    const std::string& package_name, const std::string& file_name
) {
    return "pool/main/" + debian_pool_bucket(package_name) + "/" + package_name + "/" + file_name;
}

fs::path local_prerelease_deb_pool_dir(
    const fs::path& project_root, const std::string& package_name
) {
    return local_prerelease_deb_dir(project_root)
        / "pool"
        / "main"
        / debian_pool_bucket(package_name)
        / package_name;
}

fs::path local_prerelease_work_dir(const fs::path& project_root) {
    return local_prerelease_dir(project_root) / "work";
}

fs::path local_prerelease_pacman_dir(const fs::path& project_root, const std::string& architecture) {
    return local_prerelease_dir(project_root) / "pacman" / architecture;
}

prerelease_state load_prerelease_state(const fs::path& project_root, std::string* error_message) {
    prerelease_state state;
    std::string read_error;
    const std::string contents = read_text_file(local_prerelease_state_path(project_root), &read_error);
    if (!read_error.empty()) {
        if (read_error.find("unable to open") != std::string::npos) {
            return state;
        }
        *error_message = read_error;
        return state;
    }

    try {
        const json root = json::parse(contents);
        if (!root.is_object()) {
            *error_message = "invalid prerelease state";
            return state;
        }
        if (root.contains("packages") && root.at("packages").is_array()) {
            for (const json& entry : root.at("packages")) {
                if (!entry.is_object()) {
                    continue;
                }
                const prerelease_package_state package = package_state_from_json(entry);
                if (!package.package_name.empty()) {
                    state.packages.push_back(package);
                }
            }
        }
    } catch (const json::parse_error& error) {
        *error_message = "invalid prerelease state: " + std::string(error.what());
    }

    return state;
}

bool save_prerelease_state(
    const fs::path& project_root,
    const prerelease_state& state,
    std::string* error_message
) {
    json root = json::object();
    root["packages"] = json::array();
    for (const prerelease_package_state& package : state.packages) {
        root["packages"].push_back(package_state_to_json(package));
    }
    return write_text_file(local_prerelease_state_path(project_root), root.dump(2) + "\n", error_message);
}

prerelease_package_state* find_package_state(
    prerelease_state* state, const std::string& package_name
) {
    for (prerelease_package_state& package : state->packages) {
        if (package.package_name == package_name) {
            return &package;
        }
    }
    return nullptr;
}

bool reserve_next_prerelease_version(
    prerelease_state* state,
    const std::string& package_name,
    const std::string& description,
    const std::string& artifact,
    const std::optional<std::string>& requested_base_version,
    prerelease_package_state** package_state,
    prerelease_version* version,
    std::string* error_message
) {
    if (requested_base_version.has_value()
        && !std::regex_match(*requested_base_version, semver_pattern)) {
        *error_message = "--version-base must use major.minor.patch form";
        return false;
    }

    prerelease_package_state* current = find_package_state(state, package_name);
    if (current == nullptr) {
        state->packages.push_back(prerelease_package_state {});
        current = &state->packages.back();
        current->package_name = package_name;
        current->base_version = requested_base_version.value_or("0.1.0");
        current->next_prerelease = 1;
    }

    current->description = description;
    current->artifact = artifact;

    if (requested_base_version.has_value()
        && current->base_version != *requested_base_version) {
        current->base_version = *requested_base_version;
        current->next_prerelease = 1;
    }

    if (!std::regex_match(current->base_version, semver_pattern)) {
        *error_message = "stored prerelease base version is invalid; pass --version-base major.minor.patch";
        return false;
    }

    version->base_version = current->base_version;
    version->prerelease_number = std::max(1, current->next_prerelease);
    version->logical_version = logical_prerelease_version(
        version->base_version,
        version->prerelease_number
    );
    version->debian_version = debian_prerelease_version(
        version->base_version,
        version->prerelease_number
    );
    version->pacman_version = pacman_prerelease_version(
        version->base_version,
        version->prerelease_number
    );

    *package_state = current;
    return true;
}

bool ensure_clean_directory(const fs::path& path, std::string* error_message) {
    std::error_code error;
    fs::remove_all(path, error);
    if (error) {
        *error_message = "unable to reset " + path.string() + ": " + error.message();
        return false;
    }
    fs::create_directories(path, error);
    if (error) {
        *error_message = "unable to create " + path.string() + ": " + error.message();
        return false;
    }
    return true;
}

bool copy_directory_tree(
    const fs::path& source,
    const fs::path& destination,
    std::string* error_message
) {
    std::error_code error;
    fs::remove_all(destination, error);
    error.clear();
    fs::create_directories(destination.parent_path(), error);
    if (error) {
        *error_message = "unable to create " + destination.parent_path().string() + ": " + error.message();
        return false;
    }
    fs::copy(source, destination, fs::copy_options::recursive, error);
    if (error) {
        *error_message = "unable to copy " + source.string() + ": " + error.message();
        return false;
    }
    return true;
}

std::string package_install_subdirectory(const artifact& artifact_value) {
    if (artifact_value.kind == "exe" || artifact_value.kind == "qt_app") {
        return "usr/bin";
    }
    if (artifact_value.kind == "shared_lib" || artifact_value.kind == "static_lib") {
        return "usr/lib";
    }
    return {};
}

bool project_has_assets_dir(const fs::path& project_root) {
    std::error_code error;
    return fs::exists(project_root / "assets", error) && !error;
}

std::uintmax_t directory_file_size(const fs::path& root) {
    std::uintmax_t total = 0U;
    std::error_code error;
    if (!fs::exists(root, error) || error) {
        return total;
    }
    fs::recursive_directory_iterator iterator(root, error);
    const fs::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error)) {
            total += iterator->file_size(error);
        }
        error.clear();
        iterator.increment(error);
    }
    return total;
}

std::vector<std::string> collect_relative_files(const fs::path& root) {
    std::vector<std::string> files;
    std::error_code error;
    if (!fs::exists(root, error) || error) {
        return files;
    }

    fs::recursive_directory_iterator iterator(root, error);
    const fs::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error)) {
            files.push_back(iterator->path().lexically_relative(root).generic_string());
        }
        error.clear();
        iterator.increment(error);
    }

    std::sort(files.begin(), files.end());
    return files;
}

bool stage_package_payload(
    const fs::path& project_root,
    const resolved_artifact& resolved,
    const fs::path& built_artifact_path,
    const fs::path& stage_root,
    std::uintmax_t* installed_size,
    std::vector<std::string>* installed_files,
    std::string* error_message
) {
    const std::string install_subdirectory = package_install_subdirectory(*resolved.artifact_value);
    if (install_subdirectory.empty()) {
        *error_message = "prerelease packaging does not support interface libraries";
        return false;
    }

    std::error_code error;
    const fs::path install_dir = stage_root / install_subdirectory;
    fs::create_directories(install_dir, error);
    if (error) {
        *error_message = "unable to create " + install_dir.string() + ": " + error.message();
        return false;
    }

    const fs::path destination = install_dir / built_artifact_path.filename();
    fs::copy_file(
        built_artifact_path, destination, fs::copy_options::none, error
    );
    if (error) {
        if (error == std::errc::file_exists) {
            *error_message
                = "package payload collision: " + destination.string() + " for "
                + format_artifact_ref(resolved.ref);
        } else {
            *error_message = "unable to copy " + built_artifact_path.string()
                + " to " + destination.string() + ": " + error.message();
        }
        return false;
    }

    if ((resolved.artifact_value->kind == "exe" || resolved.artifact_value->kind == "qt_app")
        && project_has_assets_dir(project_root)
        && !copy_directory_tree(project_root / "assets", install_dir / "assets", error_message)) {
        return false;
    }

    *installed_size = directory_file_size(stage_root);
    *installed_files = collect_relative_files(stage_root);
    return true;
}

std::string render_required_release_template(
    const fs::path& relative_path,
    const template_bindings& bindings,
    std::string* error_message
) {
    return render_text_template(relative_path, bindings, error_message);
}

std::string optional_template_line(
    const std::string& prefix,
    const std::string& value
) {
    if (value.empty()) {
        return {};
    }
    return prefix + value + "\n";
}

std::string newline_terminated_block(const std::vector<std::string>& lines) {
    std::ostringstream block;
    for (const std::string& line : lines) {
        block << line << "\n";
    }
    return block.str();
}

std::string debian_control_contents(
    const std::string& package_name,
    const prerelease_version& version,
    const std::string& architecture,
    const std::string& description,
    const std::string& packager,
    const std::uintmax_t installed_size,
    std::string* error_message
) {
    return render_required_release_template(
        "release/debian_control.tpl",
        {
            { "package_name", package_name },
            { "debian_version", version.debian_version },
            { "architecture", architecture },
            { "installed_size_kib",
              std::to_string((installed_size + 1023U) / 1024U) },
            { "packager", packager },
            { "description", description },
        },
        error_message
    );
}

std::string pacman_pkginfo_contents(
    const std::string& package_name,
    const prerelease_version& version,
    const std::string& architecture,
    const std::string& description,
    const std::string& packager,
    const std::uintmax_t installed_size,
    const std::int64_t build_epoch,
    std::string* error_message
) {
    return render_required_release_template(
        "release/pacman_pkginfo.tpl",
        {
            { "package_name", package_name },
            { "pacman_version", version.pacman_version },
            { "description", description },
            { "architecture", architecture },
            { "installed_size", std::to_string(installed_size) },
            { "build_epoch", std::to_string(build_epoch) },
            { "packager", packager },
        },
        error_message
    );
}

command_error create_debian_package(
    const fs::path& work_dir,
    const fs::path& package_dir,
    const tool_status& tar_tool,
    const tool_status& ar_tool,
    const std::string& package_name,
    const prerelease_version& version,
    const std::string& architecture,
    const std::string& description,
    const std::string& packager,
    const fs::path& payload_root,
    const std::uintmax_t installed_size,
    fs::path* package_path,
    std::string* error_message
) {
    const std::string file_name = package_name + "_" + version.debian_version + "_" + architecture + ".deb";
    *package_path = package_dir / file_name;

    const fs::path package_work_dir = work_dir / "deb";
    const fs::path control_dir = package_work_dir / "control";
    if (!ensure_clean_directory(control_dir, error_message)) {
        return command_error::task_failed;
    }
    const std::string control_contents = debian_control_contents(
        package_name,
        version,
        architecture,
        description,
        packager,
        installed_size,
        error_message
    );
    if (!error_message->empty()) {
        return command_error::task_failed;
    }
    if (!write_text_file(
            control_dir / "control",
            control_contents,
            error_message)) {
        return command_error::task_failed;
    }
    if (!write_text_file(package_work_dir / "debian-binary", "2.0\n", error_message)) {
        return command_error::task_failed;
    }

    const fs::path control_archive = package_work_dir / "control.tar.gz";
    const fs::path data_archive = package_work_dir / "data.tar.gz";
    std::error_code error;
    fs::create_directories(package_path->parent_path(), error);
    if (error) {
        *error_message = "unable to create " + package_path->parent_path().string() + ": " + error.message();
        return command_error::task_failed;
    }
    error.clear();
    fs::remove(*package_path, error);
    if (run_command(
            {
                tar_tool.path,
                "-czf",
                control_archive.string(),
                "-C",
                control_dir.string(),
                ".",
            },
            package_work_dir
        )
        != 0) {
        *error_message = "unable to archive Debian control payload";
        return command_error::task_failed;
    }
    if (run_command(
            {
                tar_tool.path,
                "-czf",
                data_archive.string(),
                "-C",
                payload_root.string(),
                ".",
            },
            package_work_dir
        )
        != 0) {
        *error_message = "unable to archive Debian data payload";
        return command_error::task_failed;
    }
    if (run_command(
            {
                ar_tool.path,
                "r",
                package_path->string(),
                "debian-binary",
                "control.tar.gz",
                "data.tar.gz",
            },
            package_work_dir
        )
        != 0) {
        *error_message = "unable to assemble Debian package";
        return command_error::task_failed;
    }
    return command_error::ok;
}

command_error create_pacman_package(
    const fs::path& work_dir,
    const fs::path& repo_dir,
    const tool_status& tar_tool,
    const std::string& package_name,
    const prerelease_version& version,
    const std::string& architecture,
    const std::string& description,
    const std::string& packager,
    const fs::path& payload_root,
    const std::uintmax_t installed_size,
    const std::int64_t build_epoch,
    fs::path* package_path,
    std::string* error_message
) {
    const std::string file_name
        = package_name + "-" + version.pacman_version + "-1-" + architecture + ".pkg.tar.gz";
    *package_path = repo_dir / file_name;

    const fs::path package_root = work_dir / "pacman" / "package";
    if (!ensure_clean_directory(package_root, error_message)) {
        return command_error::task_failed;
    }
    const std::string pkginfo_contents = pacman_pkginfo_contents(
        package_name,
        version,
        architecture,
        description,
        packager,
        installed_size,
        build_epoch,
        error_message
    );
    if (!error_message->empty()) {
        return command_error::task_failed;
    }
    if (!write_text_file(
            package_root / ".PKGINFO",
            pkginfo_contents,
            error_message)) {
        return command_error::task_failed;
    }

    const fs::path payload_usr = payload_root / "usr";
    std::error_code error;
    if (fs::exists(payload_usr, error) && !error) {
        if (!copy_directory_tree(payload_usr, package_root / "usr", error_message)) {
            return command_error::task_failed;
        }
    }

    fs::remove(*package_path, error);
    if (run_command(
            {
                tar_tool.path,
                "-czf",
                package_path->string(),
                "-C",
                package_root.string(),
                ".",
            },
            work_dir
        )
        != 0) {
        *error_message = "unable to assemble Pacman package";
        return command_error::task_failed;
    }
    return command_error::ok;
}

std::string sha256_for_file(const tool_status& sha256_tool, const fs::path& path) {
    if (!sha256_tool.available) {
        return {};
    }
    const std::string output = trim_copy(capture_command({ sha256_tool.path, path.string() }));
    const std::size_t separator = output.find_first_of(" \t");
    if (separator == std::string::npos) {
        return {};
    }
    return output.substr(0U, separator);
}

bool write_packages_gz(
    const tool_status& gzip_tool,
    const fs::path& packages_path,
    const fs::path& packages_gz_path,
    std::string* error_message
) {
    const std::string gz_contents = capture_command(
        { gzip_tool.path, "-n", "-c", packages_path.string() },
        packages_path.parent_path()
    );
    if (gz_contents.empty()) {
        *error_message = "unable to compress Debian package index";
        return false;
    }
    return write_text_file(packages_gz_path, gz_contents, error_message);
}

struct debian_release_file {
    std::string filename;
    std::uintmax_t size = 0U;
    std::string sha256;
};

std::string debian_release_sha256_block(
    const std::vector<debian_release_file>& files
) {
    std::vector<std::string> lines;
    for (const debian_release_file& file : files) {
        if (file.sha256.empty()) {
            continue;
        }
        lines.push_back(
            " " + file.sha256 + " " + std::to_string(file.size)
            + " " + file.filename
        );
    }
    if (lines.empty()) {
        return {};
    }
    return "SHA256:\n" + newline_terminated_block(lines);
}

std::string debian_release_contents(
    const manifest& manifest_value,
    const std::set<std::string>& architectures,
    const std::vector<debian_release_file>& files,
    const std::int64_t build_epoch,
    std::string* error_message
) {
    const std::string formatted_date = format_rfc2822_utc(build_epoch);
    return render_required_release_template(
        "release/debian_release.tpl",
        {
            { "project_id", manifest_value.id },
            { "date_line",
              optional_template_line("Date: ", formatted_date) },
            { "architectures_line",
              optional_template_line(
                  "Architectures: ", join_space_separated(architectures)
              ) },
            { "description", manifest_value.description },
            { "sha256_block", debian_release_sha256_block(files) },
        },
        error_message
    );
}

std::string debian_packages_entry_contents(
    const prerelease_package_state& package,
    const fs::path& package_path,
    const tool_status& sha256_tool,
    std::string* error_message
) {
    std::error_code error;
    return render_required_release_template(
        "release/debian_packages_entry.tpl",
        {
            { "package_name", package.package_name },
            { "debian_version", package.latest_debian_version },
            { "architecture", package.latest_debian_architecture },
            { "packager", packager_string() },
            { "description", package.description },
            { "filename", package.latest_debian_package },
            { "size", std::to_string(fs::file_size(package_path, error)) },
            { "sha256_line",
              optional_template_line(
                  "SHA256: ", sha256_for_file(sha256_tool, package_path)
              ) },
        },
        error_message
    );
}

bool write_debian_release_file(
    const prerelease_state& state,
    const manifest& manifest_value,
    const fs::path& project_root,
    const tool_status& sha256_tool,
    const std::int64_t build_epoch,
    std::string* error_message
) {
    const fs::path distribution_dir = local_prerelease_deb_distribution_dir(project_root);
    std::error_code error;
    const std::set<std::string> architectures = debian_architectures(state);

    std::vector<debian_release_file> files;
    for (const std::string& architecture : architectures) {
        for (const fs::path& path : {
                 local_prerelease_deb_packages_path(project_root, architecture),
                 local_prerelease_deb_packages_gz_path(project_root, architecture),
             }) {
            if (!fs::exists(path, error) || error) {
                error.clear();
                continue;
            }
            files.push_back(
                {
                    path.lexically_relative(distribution_dir).generic_string(),
                    fs::file_size(path, error),
                    sha256_for_file(sha256_tool, path),
                }
            );
            error.clear();
        }
    }

    const std::string release_contents = debian_release_contents(
        manifest_value, architectures, files, build_epoch, error_message
    );
    if (!error_message->empty()) {
        return false;
    }
    return write_text_file(
        local_prerelease_deb_release_path(project_root),
        release_contents,
        error_message
    );
}

bool rewrite_debian_repo(
    const prerelease_state& state,
    const manifest& manifest_value,
    const fs::path& project_root,
    const tool_status& gzip_tool,
    const tool_status& sha256_tool,
    const std::int64_t build_epoch,
    std::string* error_message
) {
    const fs::path repo_dir = local_prerelease_deb_dir(project_root);
    if (!ensure_clean_directory(local_prerelease_deb_distribution_dir(project_root), error_message)) {
        return false;
    }
    const std::set<std::string> architectures = debian_architectures(state);
    for (const std::string& architecture : architectures) {
        std::ostringstream packages;
        for (const prerelease_package_state& package : state.packages) {
            if (package.latest_debian_package.empty()
                || package.latest_debian_architecture != architecture) {
                continue;
            }
            const fs::path package_path = repo_dir / package.latest_debian_package;
            std::error_code error;
            if (!fs::exists(package_path, error) || error) {
                error.clear();
                continue;
            }
            packages << debian_packages_entry_contents(
                package, package_path, sha256_tool, error_message
            );
            if (!error_message->empty()) {
                return false;
            }
        }

        const fs::path packages_path = local_prerelease_deb_packages_path(project_root, architecture);
        const fs::path packages_gz_path = local_prerelease_deb_packages_gz_path(project_root, architecture);
        if (!write_text_file(packages_path, packages.str(), error_message)) {
            return false;
        }
        if (!write_packages_gz(gzip_tool, packages_path, packages_gz_path, error_message)) {
            return false;
        }
    }
    return write_debian_release_file(
        state,
        manifest_value,
        project_root,
        sha256_tool,
        build_epoch,
        error_message
    );
}

std::vector<std::string> gpg_base_command(
    const tool_status& gpg_tool,
    const prerelease_signing_options& signing_options
) {
    std::vector<std::string> command {
        gpg_tool.path,
        "--batch",
        "--yes",
    };

    const std::optional<std::string> signing_key = effective_signing_key(signing_options);
    if (signing_key.has_value()) {
        command.push_back("--local-user");
        command.push_back(*signing_key);
    }

    return command;
}

bool gpg_detach_sign(
    const tool_status& gpg_tool,
    const fs::path& input_path,
    const fs::path& output_path,
    const prerelease_signing_options& signing_options,
    std::string* error_message
) {
    std::vector<std::string> command = gpg_base_command(gpg_tool, signing_options);
    command.push_back("--output");
    command.push_back(output_path.string());
    command.push_back("--detach-sign");
    command.push_back(input_path.string());
    if (run_command(command, input_path.parent_path()) != 0) {
        *error_message = "unable to sign " + input_path.string();
        return false;
    }
    return true;
}

bool gpg_clearsign(
    const tool_status& gpg_tool,
    const fs::path& input_path,
    const fs::path& output_path,
    const prerelease_signing_options& signing_options,
    std::string* error_message
) {
    std::vector<std::string> command = gpg_base_command(gpg_tool, signing_options);
    command.push_back("--output");
    command.push_back(output_path.string());
    command.push_back("--clearsign");
    command.push_back(input_path.string());
    if (run_command(command, input_path.parent_path()) != 0) {
        *error_message = "unable to clear-sign " + input_path.string();
        return false;
    }
    return true;
}

bool rewrite_debian_signatures(
    const fs::path& project_root,
    const tool_status& gpg_tool,
    const prerelease_signing_options& signing_options,
    std::string* error_message
) {
    const fs::path distribution_dir = local_prerelease_deb_distribution_dir(project_root);
    const fs::path release_path = local_prerelease_deb_release_path(project_root);
    if (!gpg_clearsign(
            gpg_tool,
            release_path,
            distribution_dir / "InRelease",
            signing_options,
            error_message)) {
        return false;
    }
    return gpg_detach_sign(
        gpg_tool,
        release_path,
        distribution_dir / "Release.gpg",
        signing_options,
        error_message
    );
}

void clear_debian_signatures(const fs::path& project_root) {
    const fs::path repo_dir = local_prerelease_deb_dir(project_root);
    const fs::path distribution_dir = local_prerelease_deb_distribution_dir(project_root);
    remove_if_exists(distribution_dir / "InRelease");
    remove_if_exists(distribution_dir / "Release.gpg");
    remove_if_exists(repo_dir / "InRelease");
    remove_if_exists(repo_dir / "Release.gpg");
}

void clear_legacy_debian_flat_indexes(const fs::path& project_root) {
    const fs::path repo_dir = local_prerelease_deb_dir(project_root);
    remove_if_exists(repo_dir / "Packages");
    remove_if_exists(repo_dir / "Packages.gz");
    remove_if_exists(repo_dir / "Release");
}

std::string pacman_desc_contents(
    const prerelease_package_state& package,
    const fs::path& package_path,
    const tool_status& sha256_tool,
    std::string* error_message
) {
    std::error_code error;
    const std::string sha256 = sha256_for_file(sha256_tool, package_path);
    return render_required_release_template(
        "release/pacman_desc.tpl",
        {
            { "filename", package.latest_pacman_package },
            { "package_name", package.package_name },
            { "pacman_version", package.latest_pacman_version },
            { "description", package.description },
            { "compressed_size",
              std::to_string(fs::file_size(package_path, error)) },
            { "installed_size",
              std::to_string(package.latest_installed_size) },
            { "architecture", package.latest_pacman_architecture },
            { "build_epoch", std::to_string(package.latest_build_epoch) },
            { "packager", packager_string() },
            { "sha256_block",
              sha256.empty()
                  ? std::string()
                  : "%SHA256SUM%\n" + sha256 + "\n\n" },
        },
        error_message
    );
}

std::string pacman_files_contents(
    const prerelease_package_state& package,
    std::string* error_message
) {
    return render_required_release_template(
        "release/pacman_files.tpl",
        {
            { "files_block", newline_terminated_block(package.latest_files) },
        },
        error_message
    );
}

bool write_pacman_repo_archive(
    const tool_status& tar_tool,
    const fs::path& work_root,
    const fs::path& archive_path,
    const fs::path& working_directory,
    std::string* error_message
) {
    std::error_code error;
    fs::remove(archive_path, error);
    if (run_command(
            {
                tar_tool.path,
                "-czf",
                archive_path.string(),
                "-C",
                work_root.string(),
                ".",
            },
            working_directory
        )
        != 0) {
        *error_message = "unable to write Pacman repository metadata";
        return false;
    }
    return true;
}

bool rewrite_pacman_repo(
    const prerelease_state& state,
    const manifest& manifest_value,
    const fs::path& project_root,
    const std::string& architecture,
    const tool_status& tar_tool,
    const tool_status& sha256_tool,
    std::string* error_message
) {
    const fs::path repo_dir = local_prerelease_pacman_dir(project_root, architecture);
    const fs::path db_root = local_prerelease_work_dir(project_root) / "pacman-db";
    const fs::path files_root = local_prerelease_work_dir(project_root) / "pacman-files";
    if (!ensure_clean_directory(db_root, error_message)) {
        return false;
    }
    if (!ensure_clean_directory(files_root, error_message)) {
        return false;
    }

    for (const prerelease_package_state& package : state.packages) {
        if (package.latest_pacman_package.empty()) {
            continue;
        }
        const fs::path package_path = repo_dir / package.latest_pacman_package;
        std::error_code error;
        if (!fs::exists(package_path, error) || error) {
            continue;
        }
        const fs::path db_entry_dir = db_root / (package.package_name + "-" + package.latest_pacman_version);
        const fs::path files_entry_dir
            = files_root / (package.package_name + "-" + package.latest_pacman_version);
        fs::create_directories(db_entry_dir, error);
        if (error) {
            *error_message = "unable to create " + db_entry_dir.string() + ": " + error.message();
            return false;
        }
        error.clear();
        fs::create_directories(files_entry_dir, error);
        if (error) {
            *error_message = "unable to create " + files_entry_dir.string() + ": " + error.message();
            return false;
        }
        const std::string desc_contents = pacman_desc_contents(
            package, package_path, sha256_tool, error_message
        );
        if (!error_message->empty()) {
            return false;
        }
        if (!write_text_file(
                db_entry_dir / "desc",
                desc_contents,
                error_message)) {
            return false;
        }
        if (!write_text_file(
                files_entry_dir / "desc",
                desc_contents,
                error_message)) {
            return false;
        }
        const std::string files_contents
            = pacman_files_contents(package, error_message);
        if (!error_message->empty()) {
            return false;
        }
        if (!write_text_file(
                files_entry_dir / "files",
                files_contents,
                error_message)) {
            return false;
        }
    }

    const std::string repo_name = normalize_package_token(manifest_value.id) + "-prerelease";
    const fs::path db_path = repo_dir / (repo_name + ".db");
    if (!write_pacman_repo_archive(tar_tool, db_root, db_path, repo_dir, error_message)) {
        return false;
    }
    const fs::path files_path = repo_dir / (repo_name + ".files");
    return write_pacman_repo_archive(tar_tool, files_root, files_path, repo_dir, error_message);
}

bool rewrite_pacman_signatures(
    const prerelease_state& state,
    const manifest& manifest_value,
    const fs::path& project_root,
    const std::string& architecture,
    const tool_status& gpg_tool,
    const prerelease_signing_options& signing_options,
    std::string* error_message
) {
    const fs::path repo_dir = local_prerelease_pacman_dir(project_root, architecture);
    for (const prerelease_package_state& package : state.packages) {
        if (package.latest_pacman_package.empty()) {
            continue;
        }
        const fs::path package_path = repo_dir / package.latest_pacman_package;
        std::error_code error;
        if (!fs::exists(package_path, error) || error) {
            error.clear();
            continue;
        }
        if (!gpg_detach_sign(
                gpg_tool,
                package_path,
                fs::path(package_path.string() + ".sig"),
                signing_options,
                error_message)) {
            return false;
        }
    }

    const fs::path db_path
        = repo_dir / (normalize_package_token(manifest_value.id) + "-prerelease.db");
    if (!gpg_detach_sign(
        gpg_tool,
        db_path,
        fs::path(db_path.string() + ".sig"),
        signing_options,
        error_message)) {
        return false;
    }

    const fs::path files_path
        = repo_dir / (normalize_package_token(manifest_value.id) + "-prerelease.files");
    return gpg_detach_sign(
        gpg_tool,
        files_path,
        fs::path(files_path.string() + ".sig"),
        signing_options,
        error_message
    );
}

void clear_pacman_signatures(
    const manifest& manifest_value,
    const fs::path& project_root,
    const std::string& architecture
) {
    const fs::path repo_dir = local_prerelease_pacman_dir(project_root, architecture);
    const fs::path db_path
        = repo_dir / (normalize_package_token(manifest_value.id) + "-prerelease.db");
    const fs::path files_path
        = repo_dir / (normalize_package_token(manifest_value.id) + "-prerelease.files");
    remove_if_exists(fs::path(db_path.string() + ".sig"));
    remove_if_exists(fs::path(files_path.string() + ".sig"));
}

}  // namespace release_support

using namespace release_support;

fs::path local_prerelease_dir(const fs::path& project_root) {
    return local_state_dir(project_root) / "prerelease";
}

std::string prerelease_package_name(
    const manifest& manifest_value,
    const artifact_ref& ref,
    const artifact& artifact_value
) {
    const std::optional<artifact_ref> facade_ref = parse_artifact_ref(manifest_value.facade_entry_artifact);
    const std::string project_name = normalize_package_token(manifest_value.id);
    if (facade_ref.has_value() && format_artifact_ref(*facade_ref) == format_artifact_ref(ref)) {
        return project_name;
    }
    return project_name + "-"
        + normalize_package_token(ref.component_id)
        + "-" + normalize_package_token(artifact_value.id);
}

command_error create_prerelease_packages(
    const fs::path& project_root,
    const manifest& manifest_value,
    const resolved_artifact& resolved,
    const fs::path& built_artifact_path,
    const std::optional<std::string>& version_base,
    const prerelease_signing_options& signing_options,
    prerelease_version* version,
    prerelease_artifacts* artifacts,
    std::string* error_message
) {
    const tool_status tar_tool = probe_tool("tar");
    const tool_status ar_tool = probe_tool("ar");
    const tool_status gzip_tool = probe_tool("gzip");
    const tool_status sha256_tool = probe_tool("sha256sum");
    const tool_status gpg_tool = probe_tool("gpg");
    if (!tar_tool.available || !ar_tool.available || !gzip_tool.available) {
        *error_message = "prerelease packaging requires tar, ar, and gzip";
        return command_error::missing_local_tooling;
    }
    if (signing_options.sign && !gpg_tool.available) {
        *error_message = "prerelease signing requires gpg";
        return command_error::missing_local_tooling;
    }

    std::string state_error;
    prerelease_state state = load_prerelease_state(project_root, &state_error);
    if (!state_error.empty()) {
        *error_message = state_error;
        return command_error::task_failed;
    }

    prerelease_package_state* package_state = nullptr;
    if (!reserve_next_prerelease_version(
            &state,
            prerelease_package_name(
                manifest_value,
                resolved.ref,
                *resolved.artifact_value
            ),
            package_description(manifest_value, resolved),
            format_artifact_ref(resolved.ref),
            version_base,
            &package_state,
            version,
            error_message)) {
        return command_error::invalid_request;
    }

    const std::string machine = native_machine();
    const std::string debian_architecture = debian_architecture_for_machine(machine);
    const std::string pacman_architecture = pacman_architecture_for_machine(machine);
    const std::int64_t build_epoch = static_cast<std::int64_t>(std::time(nullptr));

    const fs::path work_dir = local_prerelease_work_dir(project_root)
        / (package_state->package_name + "-" + version->logical_version);
    const fs::path payload_root = work_dir / "payload";
    std::uintmax_t installed_size = 0U;
    std::vector<std::string> installed_files;
    if (!ensure_clean_directory(payload_root, error_message)) {
        return command_error::task_failed;
    }
    if (!stage_package_payload(
            project_root,
            resolved,
            built_artifact_path,
            payload_root,
            &installed_size,
            &installed_files,
            error_message)) {
        return command_error::task_failed;
    }

    for (const artifact_ref& companion :
         distribution_artifacts(manifest_value, resolved.ref)) {
        if (format_artifact_ref(companion)
            == format_artifact_ref(resolved.ref)) {
            continue;
        }
        const auto item = resolve_artifact(manifest_value, companion);
        if (!item.has_value()) {
            *error_message = "unresolved install artifact: "
                + format_artifact_ref(companion);
            return command_error::invalid_request;
        }
        const auto path = artifact_output_path(
            local_build_dir(project_root, "release"), *item->artifact_value,
            artifact_output_name(
                manifest_value, *item->component_value, *item->artifact_value
            )
        );
        if (!path.has_value()) {
            *error_message
                = "missing install artifact: " + format_artifact_ref(companion);
            return command_error::task_failed;
        }
        if (!stage_package_payload(
                project_root, *item, *path, payload_root, &installed_size,
                &installed_files, error_message
            )) {
            return command_error::task_failed;
        }
    }

    artifacts->deb_repo_dir = local_prerelease_deb_dir(project_root);
    artifacts->pacman_repo_dir = local_prerelease_pacman_dir(project_root, pacman_architecture);
    std::error_code error;
    fs::create_directories(artifacts->deb_repo_dir, error);
    if (error) {
        *error_message = "unable to create " + artifacts->deb_repo_dir.string() + ": " + error.message();
        return command_error::task_failed;
    }
    error.clear();
    fs::create_directories(artifacts->pacman_repo_dir, error);
    if (error) {
        *error_message = "unable to create " + artifacts->pacman_repo_dir.string() + ": " + error.message();
        return command_error::task_failed;
    }

    command_error status = create_debian_package(
        work_dir,
        local_prerelease_deb_pool_dir(project_root, package_state->package_name),
        tar_tool,
        ar_tool,
        package_state->package_name,
        *version,
        debian_architecture,
        package_state->description,
        packager_string(),
        payload_root,
        installed_size,
        &artifacts->deb_package_path,
        error_message
    );
    if (status != command_error::ok) {
        return status;
    }

    status = create_pacman_package(
        work_dir,
        artifacts->pacman_repo_dir,
        tar_tool,
        package_state->package_name,
        *version,
        pacman_architecture,
        package_state->description,
        packager_string(),
        payload_root,
        installed_size,
        build_epoch,
        &artifacts->pacman_package_path,
        error_message
    );
    if (status != command_error::ok) {
        return status;
    }

    package_state->next_prerelease = version->prerelease_number + 1;
    package_state->latest_logical_version = version->logical_version;
    package_state->latest_debian_version = version->debian_version;
    package_state->latest_debian_architecture = debian_architecture;
    package_state->latest_debian_package
        = artifacts->deb_package_path.lexically_relative(artifacts->deb_repo_dir).generic_string();
    package_state->latest_pacman_version = version->pacman_version;
    package_state->latest_pacman_architecture = pacman_architecture;
    package_state->latest_pacman_package = artifacts->pacman_package_path.filename().generic_string();
    package_state->latest_installed_size = installed_size;
    package_state->latest_build_epoch = build_epoch;
    package_state->latest_files = installed_files;

    if (!save_prerelease_state(project_root, state, error_message)) {
        return command_error::task_failed;
    }
    clear_legacy_debian_flat_indexes(project_root);
    if (!rewrite_debian_repo(
            state,
            manifest_value,
            project_root,
            gzip_tool,
            sha256_tool,
            build_epoch,
            error_message)) {
        return command_error::task_failed;
    }
    if (!rewrite_pacman_repo(
            state,
            manifest_value,
            project_root,
            pacman_architecture,
            tar_tool,
            sha256_tool,
            error_message)) {
        return command_error::task_failed;
    }
    if (signing_options.sign) {
        if (!rewrite_debian_signatures(project_root, gpg_tool, signing_options, error_message)) {
            return command_error::task_failed;
        }
        if (!rewrite_pacman_signatures(
                state,
                manifest_value,
                project_root,
                pacman_architecture,
                gpg_tool,
                signing_options,
                error_message)) {
            return command_error::task_failed;
        }
    } else {
        clear_debian_signatures(project_root);
        clear_pacman_signatures(manifest_value, project_root, pacman_architecture);
    }

    artifacts->deb_packages_path = local_prerelease_deb_packages_path(project_root, debian_architecture);
    artifacts->deb_packages_gz_path = local_prerelease_deb_packages_gz_path(project_root, debian_architecture);
    artifacts->pacman_db_path = artifacts->pacman_repo_dir
        / (normalize_package_token(manifest_value.id) + "-prerelease.db");
    return command_error::ok;
}

}  // namespace ecosystem
