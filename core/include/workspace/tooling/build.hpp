#pragma once

#include "workspace/tooling/process.hpp"

namespace ecosystem {

struct build_cache_status {
    std::filesystem::path cache_path;
    bool exists = false;
    bool stale = false;
    string_list stale_reasons;
};

struct build_tree_layout {
    std::string scope;
    std::string platform;
    std::string configuration;
    std::string variant;
};

build_tree_layout describe_build_tree_layout(
    const std::string& profile, const std::string& scope = "project"
);
std::filesystem::path
local_build_root(const std::filesystem::path& project_root);
std::filesystem::path local_build_scope_dir(
    const std::filesystem::path& project_root, const std::string& scope
);
std::filesystem::path local_build_dir(
    const std::filesystem::path& project_root, const std::string& profile
);
std::filesystem::path local_build_cache_path(
    const std::filesystem::path& project_root, const std::string& profile
);
build_cache_status inspect_build_cache(
    const std::filesystem::path& project_root, const std::string& profile,
    const std::vector<std::filesystem::path>& freshness_inputs
);
build_cache_status inspect_cache_file(
    const std::filesystem::path& project_root,
    const std::filesystem::path& cache_path,
    const std::vector<std::filesystem::path>& freshness_inputs
);
command_error configure_cmake_source_tree(
    const std::filesystem::path& source_dir,
    const std::filesystem::path& build_dir,
    const std::vector<std::string>& cmake_options,
    const std::string& build_type, std::string* error_message,
    bool capture_output = false
);
command_error configure_build_tree(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::string& profile, bool with_tests, bool with_coverage,
    bool with_benchmarks, std::string* error_message,
    bool capture_output = false
);
command_error ensure_local_developer_surface(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    std::string* error_message
);

} // namespace ecosystem
