#pragma once

#include "workspace/release.hpp"

#include <cstdint>
#include <map>
#include <regex>
#include <set>

namespace fs = std::filesystem;

namespace ecosystem::release_detail {

extern const std::regex semver_pattern;
std::string trim(const std::string& value);

// Shared read-only prepared-input and native payload rules.
void reject_aliases(const fs::path& path);
std::string contract_path(const fs::path& path);
std::string clean_revision(const fs::path& root);
std::string package_install_subdirectory(const artifact& value);
bool is_elf_file(const fs::path& path);
bool path_within(const fs::path& path, const fs::path& root);
bool system_runtime_library(const fs::path& path);
std::string cmake_path_argument(const fs::path& path);
bool collect_tooling_templates(
    const fs::path& root, const manifest& value, const artifact_ref& primary,
    std::vector<fs::path>* files, std::string* error
);

std::string package_token(const std::string& value);

std::string package_description(
    const manifest& manifest_value, const resolved_artifact& resolved
);

bool ensure_clean_directory(const fs::path& path, std::string* error_message);

bool stage_payload(
    const fs::path& project_root, const resolved_artifact& resolved,
    const fs::path& built_artifact_path, const fs::path& stage_root,
    bool install_assets, std::map<fs::path, fs::path>* payload_sources,
    std::string* error_message
);

bool stage_tooling_templates(
    const fs::path& project_root, const manifest& value,
    const artifact_ref& primary, const fs::path& stage_root,
    std::string* error_message
);

struct staged_distribution {
    std::map<fs::path, artifact_ref> artifact_files;
    std::set<fs::path> system_libraries;
};

// Complete manifest-derived product preparation for the handoff.
command_error stage_distribution(
    const fs::path& project_root, const manifest& value,
    const resolved_artifact& primary, const fs::path& built_primary,
    const fs::path& work_dir, staged_distribution* staged,
    std::string* error_message
);

/** Stage local ELF dependencies, retain SONAME aliases and record system
 * libraries. */
bool stage_runtime(
    const fs::path& project_root, const fs::path& stage_root,
    const fs::path& work_dir, std::map<fs::path, fs::path>* payload_sources,
    std::set<fs::path>* system_libraries, std::string* error_message
);

} // namespace ecosystem::release_detail
