#pragma once

#include "workspace/release.hpp"
#include "workspace/template_text.hpp"

#include <cstdint>
#include <map>
#include <regex>
#include <set>

namespace fs = std::filesystem;

namespace ecosystem::release_detail {

struct package_record {
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
    string_list latest_pacman_dependencies;
    string_list latest_debian_dependencies;
};

struct release_state {
    std::vector<package_record> packages;
};

extern const std::regex pacman_package_pattern;
extern const std::regex pacman_version_pattern;
extern const std::regex debian_package_pattern;

std::string trim(const std::string& value);

std::string env_fallback(const char* preferred_name, const char* fallback_name);

std::string package_token(const std::string& value);

std::string package_description(
    const manifest& manifest_value, const resolved_artifact& resolved
);

std::string package_author();

std::string native_machine();

std::string debian_arch(const std::string& machine);

std::string pacman_arch(const std::string& machine);

fs::path deb_dir(const fs::path& project_root);

fs::path deb_distribution(const fs::path& project_root);

fs::path deb_packages_path(
    const fs::path& project_root, const std::string& architecture
);

fs::path
deb_packages_gz(const fs::path& project_root, const std::string& architecture);

fs::path deb_release_path(const fs::path& project_root);

fs::path
deb_pool_dir(const fs::path& project_root, const std::string& package_name);

fs::path release_work_dir(const fs::path& project_root);

fs::path
pacman_dir(const fs::path& project_root, const std::string& architecture);

release_state
load_state(const fs::path& project_root, std::string* error_message);

bool save_state(
    const fs::path& project_root, const release_state& state,
    std::string* error_message
);

bool reserve_version(
    release_state* state, const std::string& package_name,
    const std::string& description, const std::string& artifact,
    const std::optional<std::string>& requested_base_version,
    package_record** package_state, prerelease_version* version,
    std::string* error_message
);

bool ensure_clean_directory(const fs::path& path, std::string* error_message);

bool copy_directory_tree(
    const fs::path& source, const fs::path& destination,
    std::string* error_message
);

std::uintmax_t directory_file_size(const fs::path& root);

std::vector<std::string> collect_relative_files(const fs::path& root);

bool stage_payload(
    const fs::path& project_root, const resolved_artifact& resolved,
    const fs::path& built_artifact_path, const fs::path& stage_root,
    const bool install_assets, std::map<fs::path, fs::path>* payload_sources,
    std::string* error_message
);

bool stage_tooling_templates(
    const fs::path& project_root, const manifest& value,
    const artifact_ref& primary, const fs::path& stage_root,
    std::string* error_message
);

/** Stage local ELF dependencies, retain SONAME aliases and record system
 * libraries. */
bool stage_runtime(
    const fs::path& project_root, const fs::path& stage_root,
    const fs::path& work_dir, std::map<fs::path, fs::path>* payload_sources,
    std::set<fs::path>* system_libraries, std::string* error_message
);

bool resolve_runtime_packages(
    const std::set<fs::path>& libraries, const std::string& package_name,
    const fs::path& work_dir, prerelease_artifacts* artifacts,
    std::string* error_message
);

std::string debian_dependency_line(const string_list& dependencies);

std::string render_release(
    const fs::path& relative_path, const template_bindings& bindings,
    std::string* error_message
);

std::string
optional_template_line(const std::string& prefix, const std::string& value);

std::string newline_terminated_block(const std::vector<std::string>& lines);

command_error create_debian_package(
    const fs::path& work_dir, const fs::path& package_dir,
    const tool_status& tar_tool, const tool_status& ar_tool,
    const std::string& package_name, const prerelease_version& version,
    const std::string& architecture, const std::string& description,
    const std::string& packager, const fs::path& payload_root,
    const std::uintmax_t installed_size, const string_list& dependencies,
    fs::path* package_path, std::string* error_message
);

command_error create_pacman_package(
    const fs::path& work_dir, const fs::path& repo_dir,
    const tool_status& tar_tool, const std::string& package_name,
    const prerelease_version& version, const std::string& architecture,
    const std::string& description, const std::string& packager,
    const fs::path& payload_root, const std::uintmax_t installed_size,
    const std::int64_t build_epoch, const string_list& dependencies,
    fs::path* package_path, std::string* error_message
);

bool rewrite_debian_repo(
    const release_state& state, const manifest& manifest_value,
    const fs::path& project_root, const tool_status& gzip_tool,
    const tool_status& sha256_tool, const std::int64_t build_epoch,
    std::string* error_message
);

void clear_flat_indexes(const fs::path& project_root);

bool rewrite_pacman_repo(
    const release_state& state, const manifest& manifest_value,
    const fs::path& project_root, const std::string& architecture,
    const tool_status& tar_tool, const tool_status& sha256_tool,
    std::string* error_message
);

void remove_if_exists(const fs::path& path);

bool rewrite_debian_signatures(
    const fs::path& project_root, const tool_status& gpg_tool,
    const prerelease_signing_options& signing_options,
    std::string* error_message
);

void clear_debian_signatures(const fs::path& project_root);

bool rewrite_pacman_signatures(
    const release_state& state, const manifest& manifest_value,
    const fs::path& project_root, const std::string& architecture,
    const tool_status& gpg_tool,
    const prerelease_signing_options& signing_options,
    std::string* error_message
);

void clear_pacman_signatures(
    const manifest& manifest_value, const fs::path& project_root,
    const std::string& architecture
);

} // namespace ecosystem::release_detail
