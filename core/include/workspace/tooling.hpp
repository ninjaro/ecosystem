#pragma once

#include "manifest.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ecosystem {

enum class command_error {
    ok = 0,
    invalid_request = 2,
    unsupported_by_manifest = 3,
    missing_local_tooling = 4,
    task_failed = 5,
};

struct tool_status {
    std::string name;
    bool available = false;
    std::string path;
    std::string version;
};

struct captured_command {
    int exit_code = -1;
    std::string output;
};

struct build_cache_status {
    std::filesystem::path cache_path;
    bool exists = false;
    bool stale = false;
    string_list stale_reasons;
};

struct android_environment {
    std::string sdk_root;
    std::string ndk_root;
    std::string ndk_version;
    std::string platform;
    std::string build_tools_version;
    std::string qt_dir;
    std::string qt_version;
    std::string qt_host_path;
    std::string qt_arch;
    std::string qt_cmake_bin;
    std::string emulator_bin;
    std::string avd_name;
    std::string adb_bin;
    std::string aapt_bin;
};

struct build_tree_layout {
    std::string scope;
    std::string platform;
    std::string configuration;
    std::string variant;
};

int exit_code(command_error error_class);

std::filesystem::path
local_state_dir(const std::filesystem::path& project_root);
std::filesystem::path
local_developer_source_dir(const std::filesystem::path& project_root);
std::filesystem::path
local_developer_cmakelists_path(const std::filesystem::path& project_root);
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
std::filesystem::path
local_report_dir(const std::filesystem::path& project_root);
std::filesystem::path
local_benchmark_dir(const std::filesystem::path& project_root);
std::filesystem::path
local_sphinx_dir(const std::filesystem::path& project_root);

build_cache_status inspect_build_cache(
    const std::filesystem::path& project_root, const std::string& profile,
    const std::vector<std::filesystem::path>& freshness_inputs
);
build_cache_status inspect_cache_file(
    const std::filesystem::path& project_root,
    const std::filesystem::path& cache_path,
    const std::vector<std::filesystem::path>& freshness_inputs
);

bool command_exists(const std::string& name);
std::string find_command_path(const std::string& name);
std::string capture_command(
    const std::vector<std::string>& args,
    const std::filesystem::path& working_directory = std::filesystem::path()
);
captured_command capture_command_result(
    const std::vector<std::string>& args,
    const std::filesystem::path& working_directory = std::filesystem::path(),
    const std::vector<std::pair<std::string, std::string>>& environment = {}
);
int run_command(
    const std::vector<std::string>& args,
    const std::filesystem::path& working_directory,
    const std::vector<std::pair<std::string, std::string>>& environment = {}
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
tool_status probe_tool(
    const std::string& name,
    const std::vector<std::string>& version_args = { "--version" }
);
android_environment detect_android_environment();

bool write_text_file(
    const std::filesystem::path& path, const std::string& contents,
    std::string* error_message
);
std::string
read_text_file(const std::filesystem::path& path, std::string* error_message);
void ensure_local_artifacts(
    const std::filesystem::path& project_root, bool with_clang_format,
    bool with_clang_tidy, bool with_doxygen
);
bool write_local_sphinx_conf(
    const std::filesystem::path& project_root, const std::string& project_name,
    std::string* error_message
);
json toolchains_report();

} // namespace ecosystem
