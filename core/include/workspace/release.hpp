#pragma once

#include "workspace/project.hpp"
#include "workspace/tooling.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace ecosystem {

struct prerelease_version {
    std::string base_version;
    int prerelease_number = 0;
    std::string logical_version;
    std::string debian_version;
    std::string pacman_version;
};

struct prerelease_signing_options {
    bool sign = false;
    std::optional<std::string> key_id;
};

struct prerelease_artifacts {
    std::filesystem::path deb_repo_dir;
    std::filesystem::path deb_package_path;
    std::filesystem::path deb_packages_path;
    std::filesystem::path deb_packages_gz_path;
    std::filesystem::path pacman_repo_dir;
    std::filesystem::path pacman_package_path;
    std::filesystem::path pacman_db_path;
    string_list pacman_dependencies;
    string_list debian_dependencies;
    string_list warnings;
};

std::filesystem::path local_prerelease_dir(const std::filesystem::path& project_root);
std::string prerelease_package_name(
    const manifest& manifest_value,
    const artifact_ref& ref,
    const artifact& artifact_value
);
command_error create_prerelease_packages(
    const std::filesystem::path& project_root,
    const manifest& manifest_value,
    const resolved_artifact& resolved,
    const std::filesystem::path& built_artifact_path,
    const std::optional<std::string>& version_base,
    const prerelease_signing_options& signing_options,
    prerelease_version* version,
    prerelease_artifacts* artifacts,
    std::string* error_message
);

}  // namespace ecosystem
