#pragma once

#include "analysis/clang.hpp"
#include "analysis/tidy.hpp"
#include "packages/package_catalog.hpp"
#include "packages/package_summary.hpp"
#include "workspace/benchmark.hpp"
#include "workspace/doctor.hpp"
#include "workspace/mutation.hpp"
#include "workspace/project.hpp"
#include "workspace/release.hpp"
#include "workspace/repository.hpp"
#include "workspace/sync.hpp"
#include "workspace/tooling.hpp"
#include "workspace/workspace_scope.hpp"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace ecosystem::command_support {

namespace fs = std::filesystem;

struct qualified_workspace_artifact {
    std::string project_selector;
    artifact_ref artifact;
};

struct unresolved_workspace_artifact_filter {
    std::optional<std::string> project_selector;
    artifact_ref artifact;
};

struct gradle_command {
    bool available = false;
    std::string path;
};

struct test_target {
    std::string build_target;
    std::string test_name;
    std::string binary_name;
};

extern const std::vector<std::string> known_build_profiles;
extern const std::vector<std::string> known_check_profiles;
extern const std::vector<std::string> known_report_kinds;

void print_error(
    std::ostream& err, command_error error_class, const std::string& message
);
std::string join_set(
    const std::set<std::string>& values, const std::string& separator
);
std::string join_strings(
    const string_list& values, const std::string& separator
);
command_error
combine_status(command_error current, command_error next);
manifest load_current_manifest(
    const fs::path& project_root, std::ostream& err, int* exit_status
);
void emit_manifest_errors(const manifest_report& report, std::ostream& err);
std::string trim_copy(const std::string& value);
bool starts_with(const std::string& value, const std::string& prefix);
bool contains_string(
    const std::vector<std::string>& values, const std::string& candidate
);
bool path_is_executable(const fs::path& path);
bool path_exists(const fs::path& path);
command_error run_format_files(
    const fs::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact, bool apply,
    std::ostream& out, std::ostream& err
);
void append_unique_paths(
    std::vector<fs::path>* files, const std::vector<fs::path>& candidates
);
std::optional<int> parse_positive_int(const std::string& value);
bool process_is_traced();
command_error spawn_background_process(
    const std::vector<std::string>& args, const fs::path& log_path,
    std::ostream& err
);
fs::path java_project_dir(const fs::path& project_root);
gradle_command resolve_gradle_command(const fs::path& project_root);
command_error build_artifacts(
    const fs::path& project_root, const std::string& profile,
    const std::vector<artifact_ref>& refs, std::ostream& err
);
void emit_dependency_line(std::ostream& out, const package_line& line);
void emit_declared_dependencies(
    std::ostream& out, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    const std::optional<std::string>& profile
);
void emit_configured_package_line(
    std::ostream& out, const package_status_line& line
);
void emit_configured_package_state(
    std::ostream& out, const fs::path& project_root,
    const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    const build_cache_status& cache_status, bool refreshed
);
json matrix_report(
    const manifest& manifest_value,
    const std::vector<artifact_ref>& requested_artifacts
);
json matrix_report(
    const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact
);
json workspace_matrix_report(
    const workspace_context& workspace, const workspace_scope& scope
);
void ensure_stream_trailing_newline(std::ostringstream* stream);
void emit_workspace_project_output(
    const workspace_context& workspace, const workspace_project* project,
    const std::ostringstream& project_out, const std::ostringstream& project_err,
    std::ostream& out, std::ostream& err
);
bool check_profile_supports_multi_artifact_filters(
    const std::string& profile
);
bool is_runnable_artifact(const artifact& artifact_value);
command_error run_configure_build_tree(
    const fs::path& project_root, const manifest& manifest_value,
    const std::string& profile, bool with_tests, bool with_coverage,
    bool with_benchmarks, std::ostream& err
);
command_error build_target(
    const fs::path& project_root, const std::string& profile,
    const std::string& target, std::ostream& err
);
std::vector<test_target> collect_test_targets(
    const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact
);
command_error build_facade_entry_artifact(
    const fs::path& project_root, const manifest& manifest_value,
    const std::string& profile, std::ostream& err
);
command_error build_test_targets(
    const fs::path& project_root, const std::string& profile,
    const std::vector<test_target>& test_targets, std::ostream& err
);
std::string ctest_regex_for(const std::vector<test_target>& test_targets);
bool has_java_gradle_surface(const fs::path& project_root);
bool project_has_docs_surface(const fs::path& project_root);
bool project_supports_java_check(
    const fs::path& project_root, const manifest& manifest_value
);
std::vector<std::pair<std::string, std::string>>
sphinx_environment(const std::optional<std::string>& sphinx_theme);
command_error resolve_benchmark_artifact(
    const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    resolved_artifact* resolved, std::ostream& err
);
std::optional<fs::path> runnable_binary_path(
    const fs::path& project_root, const manifest& manifest_value,
    const std::string& profile, const resolved_artifact& resolved
);
command_error run_runnable_binary(
    const fs::path& build_dir, const fs::path& binary_path,
    const string_list& passthrough_args, std::ostream& err
);
command_error run_android_artifact(
    const fs::path& project_root, const manifest& manifest_value,
    const resolved_artifact& resolved,
    const std::optional<std::string>& requested_mode,
    const string_list& passthrough_args, std::ostream& out, std::ostream& err
);
command_error build_android_apk(
    const fs::path& project_root, const resolved_artifact& resolved,
    fs::path* apk_path, std::ostream& out, std::ostream& err
);

}  // namespace ecosystem::command_support
