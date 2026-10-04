#include "internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace ecosystem::release_detail {

const std::regex semver_pattern("^[0-9]+\\.[0-9]+\\.[0-9]+$");
const std::regex pacman_package_pattern("^[a-zA-Z0-9@_+][a-zA-Z0-9@._+-]*$");
const std::regex pacman_version_pattern("^[a-zA-Z0-9._+:~-]+$");
const std::regex debian_package_pattern("^[a-z0-9][a-z0-9+.-]+(:[a-z0-9-]+)?$");
const std::regex debian_dependency_pattern(
    "^[a-z0-9][a-z0-9+.-]+(:[a-z0-9-]+)? \\(>= [a-zA-Z0-9._+:~-]+\\)$"
);

bool valid_pacman_dependency(const std::string& value) {
    const auto separator = value.find(">=");
    return separator != std::string::npos
        && std::regex_match(value.substr(0, separator), pacman_package_pattern)
        && std::regex_match(
               value.substr(separator + 2), pacman_version_pattern
        );
}

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

std::string env_value(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

std::string
env_fallback(const char* preferred_name, const char* fallback_name) {
    std::string value = env_value(preferred_name);
    if (!value.empty()) {
        return value;
    }
    return env_value(fallback_name);
}

std::string package_token(const std::string& value) {
    std::string normalized;
    bool last_was_dash = false;
    for (const char character : value) {
        const unsigned char raw = static_cast<unsigned char>(character);
        if (std::isalnum(raw)) {
            normalized.push_back(static_cast<char>(std::tolower(raw)));
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
    const std::optional<artifact_ref> facade_ref
        = parse_artifact_ref(manifest_value.facade_entry_artifact);
    if (facade_ref.has_value()
        && format_artifact_ref(*facade_ref)
            == format_artifact_ref(resolved.ref)) {
        return manifest_value.description;
    }
    return manifest_value.description + " (" + format_artifact_ref(resolved.ref)
        + ")";
}

std::string package_author() {
    const std::string packager
        = trim(env_fallback("MANIFESTO_PACKAGER", "ECOSYSTEM_PACKAGER"));
    if (!packager.empty()) {
        return packager;
    }
    const std::string generic_packager = trim(env_value("PACKAGER"));
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
    return trim(capture_command({ "uname", "-m" }));
#endif
}

std::string debian_arch(const std::string& machine) {
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

std::string pacman_arch(const std::string& machine) {
    if (machine.empty()) {
        return "x86_64";
    }
    return machine;
}

std::string logical_version(const std::string& base_version, const int number) {
    return base_version + "-pre." + std::to_string(number);
}

std::string debian_version(const std::string& base_version, const int number) {
    return base_version + "~pre." + std::to_string(number);
}

std::string pacman_version(const std::string& base_version, const int number) {
    return base_version + "pre" + std::to_string(number);
}

json package_state_to_json(const package_record& value) {
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
        root["latest_debian_dependencies"] = value.latest_debian_dependencies;
    }
    if (!value.latest_pacman_version.empty()) {
        root["latest_pacman_version"] = value.latest_pacman_version;
        root["latest_pacman_architecture"] = value.latest_pacman_architecture;
        root["latest_pacman_package"] = value.latest_pacman_package;
        root["latest_pacman_dependencies"] = value.latest_pacman_dependencies;
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

package_record package_state_from_json(const json& root) {
    package_record value;
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
    if (root.contains("next_prerelease")
        && root.at("next_prerelease").is_number_integer()) {
        value.next_prerelease
            = std::max(1, root.at("next_prerelease").get<int>());
    }
    if (root.contains("latest_logical_version")
        && root.at("latest_logical_version").is_string()) {
        value.latest_logical_version
            = root.at("latest_logical_version").get<std::string>();
    }
    if (root.contains("latest_debian_version")
        && root.at("latest_debian_version").is_string()) {
        value.latest_debian_version
            = root.at("latest_debian_version").get<std::string>();
    }
    if (root.contains("latest_debian_architecture")
        && root.at("latest_debian_architecture").is_string()) {
        value.latest_debian_architecture
            = root.at("latest_debian_architecture").get<std::string>();
    }
    if (root.contains("latest_debian_package")
        && root.at("latest_debian_package").is_string()) {
        value.latest_debian_package
            = root.at("latest_debian_package").get<std::string>();
    }
    if (root.contains("latest_pacman_version")
        && root.at("latest_pacman_version").is_string()) {
        value.latest_pacman_version
            = root.at("latest_pacman_version").get<std::string>();
    }
    if (root.contains("latest_pacman_architecture")
        && root.at("latest_pacman_architecture").is_string()) {
        value.latest_pacman_architecture
            = root.at("latest_pacman_architecture").get<std::string>();
    }
    if (root.contains("latest_pacman_package")
        && root.at("latest_pacman_package").is_string()) {
        value.latest_pacman_package
            = root.at("latest_pacman_package").get<std::string>();
    }
    if (root.contains("latest_installed_size")
        && root.at("latest_installed_size").is_number_unsigned()) {
        value.latest_installed_size
            = root.at("latest_installed_size").get<std::uintmax_t>();
    }
    if (root.contains("latest_build_epoch")
        && root.at("latest_build_epoch").is_number_integer()) {
        value.latest_build_epoch
            = root.at("latest_build_epoch").get<std::int64_t>();
    }
    if (root.contains("latest_files") && root.at("latest_files").is_array()) {
        for (const json& entry : root.at("latest_files")) {
            if (entry.is_string()) {
                value.latest_files.push_back(entry.get<std::string>());
            }
        }
    }
    if (root.contains("latest_pacman_dependencies")) {
        const auto& dependencies = root.at("latest_pacman_dependencies");
        if (!dependencies.is_array())
            throw std::runtime_error(
                "invalid persisted Pacman dependencies for "
                + value.package_name
            );
        for (const auto& dependency : dependencies) {
            if (!dependency.is_string()
                || !valid_pacman_dependency(dependency.get<std::string>()))
                throw std::runtime_error(
                    "invalid persisted Pacman dependency for "
                    + value.package_name
                );
            value.latest_pacman_dependencies.push_back(
                dependency.get<std::string>()
            );
        }
    }
    if (root.contains("latest_debian_dependencies")) {
        const auto& dependencies = root.at("latest_debian_dependencies");
        if (!dependencies.is_array())
            throw std::runtime_error(
                "invalid persisted Debian dependencies for "
                + value.package_name
            );
        for (const auto& dependency : dependencies) {
            if (!dependency.is_string()
                || !std::regex_match(
                    dependency.get<std::string>(), debian_dependency_pattern
                ))
                throw std::runtime_error(
                    "invalid persisted Debian dependency for "
                    + value.package_name
                );
            value.latest_debian_dependencies.push_back(
                dependency.get<std::string>()
            );
        }
    }
    return value;
}

fs::path state_path(const fs::path& project_root) {
    return local_prerelease_dir(project_root) / "state.json";
}

fs::path deb_dir(const fs::path& project_root) {
    return local_prerelease_dir(project_root) / "deb";
}

fs::path deb_distribution(const fs::path& project_root) {
    return deb_dir(project_root) / "dists" / "prerelease";
}

fs::path
deb_binary_dir(const fs::path& project_root, const std::string& architecture) {
    return deb_distribution(project_root) / "main" / ("binary-" + architecture);
}

fs::path deb_packages_path(
    const fs::path& project_root, const std::string& architecture
) {
    return deb_binary_dir(project_root, architecture) / "Packages";
}

fs::path
deb_packages_gz(const fs::path& project_root, const std::string& architecture) {
    return deb_binary_dir(project_root, architecture) / "Packages.gz";
}

fs::path deb_release_path(const fs::path& project_root) {
    return deb_distribution(project_root) / "Release";
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
    return "pool/main/" + debian_pool_bucket(package_name) + "/" + package_name
        + "/" + file_name;
}

fs::path
deb_pool_dir(const fs::path& project_root, const std::string& package_name) {
    return deb_dir(project_root) / "pool" / "main"
        / debian_pool_bucket(package_name) / package_name;
}

fs::path release_work_dir(const fs::path& project_root) {
    return local_prerelease_dir(project_root) / "work";
}

fs::path
pacman_dir(const fs::path& project_root, const std::string& architecture) {
    return local_prerelease_dir(project_root) / "pacman" / architecture;
}

release_state
load_state(const fs::path& project_root, std::string* error_message) {
    release_state state;
    std::string read_error;
    const std::string contents
        = read_text_file(state_path(project_root), &read_error);
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
                const package_record package = package_state_from_json(entry);
                if (!package.package_name.empty()) {
                    state.packages.push_back(package);
                }
            }
        }
    } catch (const json::parse_error& error) {
        *error_message
            = "invalid prerelease state: " + std::string(error.what());
    }

    return state;
}

bool save_state(
    const fs::path& project_root, const release_state& state,
    std::string* error_message
) {
    json root = json::object();
    root["packages"] = json::array();
    for (const package_record& package : state.packages) {
        root["packages"].push_back(package_state_to_json(package));
    }
    return write_text_file(
        state_path(project_root), root.dump(2) + "\n", error_message
    );
}

package_record*
find_package_state(release_state* state, const std::string& package_name) {
    for (package_record& package : state->packages) {
        if (package.package_name == package_name) {
            return &package;
        }
    }
    return nullptr;
}

bool reserve_version(
    release_state* state, const std::string& package_name,
    const std::string& description, const std::string& artifact,
    const std::optional<std::string>& requested_base_version,
    package_record** package_state, prerelease_version* version,
    std::string* error_message
) {
    if (requested_base_version.has_value()
        && !std::regex_match(*requested_base_version, semver_pattern)) {
        *error_message = "--version-base must use major.minor.patch form";
        return false;
    }

    package_record* current = find_package_state(state, package_name);
    if (current == nullptr) {
        state->packages.push_back(package_record {});
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
        *error_message = "stored prerelease base version is invalid; pass "
                         "--version-base major.minor.patch";
        return false;
    }

    version->base_version = current->base_version;
    version->prerelease_number = std::max(1, current->next_prerelease);
    version->logical_version
        = logical_version(version->base_version, version->prerelease_number);
    version->debian_version
        = debian_version(version->base_version, version->prerelease_number);
    version->pacman_version
        = pacman_version(version->base_version, version->prerelease_number);

    *package_state = current;
    return true;
}

} // namespace ecosystem::release_detail
