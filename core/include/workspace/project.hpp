#pragma once

#include "manifest.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace ecosystem {

struct cxx_analysis_source {
    std::filesystem::path path;
    std::string component_id;
    std::string category;
    std::string artifact;
};

struct resolved_artifact {
    artifact_ref ref;
    const component* component_value = nullptr;
    const artifact* artifact_value = nullptr;
};

// Missing destinations are allowed; existing symlinks must resolve within the
// project. These checks are preflight validation, not protection against races.
string_list validate_project_paths(
    const std::filesystem::path& project_root,
    const std::vector<std::filesystem::path>& relative_paths
);
string_list validate_manifest_paths(
    const manifest& value, const std::filesystem::path& project_root
);

const component* find_component(const manifest& value, const std::string& component_id);
const artifact* find_artifact(const component& value, const std::string& artifact_id);
std::optional<resolved_artifact> resolve_artifact(
    const manifest& value, const std::optional<artifact_ref>& requested
);
std::vector<artifact_ref>
distribution_artifacts(const manifest& value, const artifact_ref& primary);
std::vector<std::string> component_stack_values(const component& value, const std::string& key);
bool component_has_stack_key(const component& value, const std::string& key);
std::vector<const component*> component_closure_for_artifact(
    const manifest& value, const artifact_ref& root_ref
);

std::filesystem::path component_root_path(
    const std::filesystem::path& project_root, const component& value
);
std::vector<std::filesystem::path> component_include_dirs(
    const std::filesystem::path& project_root, const component& value
);
std::vector<std::filesystem::path> component_module_headers(
    const std::filesystem::path& project_root, const component& value
);
std::vector<std::filesystem::path> component_module_sources(
    const std::filesystem::path& project_root, const component& value
);
std::vector<std::filesystem::path> component_source_only_files(
    const std::filesystem::path& project_root, const component& value
);
std::vector<std::filesystem::path> component_runtime_only_files(
    const std::filesystem::path& project_root, const component& value
);
std::vector<std::filesystem::path> component_non_runtime_source_only_files(
    const std::filesystem::path& project_root, const component& value
);
std::vector<std::filesystem::path> component_header_only_files(
    const std::filesystem::path& project_root, const component& value
);
std::vector<std::filesystem::path> component_c_header_only_files(
    const std::filesystem::path& project_root, const component& value
);
std::vector<std::filesystem::path> component_template_impl_files(
    const std::filesystem::path& project_root, const component& value
);
std::vector<std::filesystem::path> component_c_header_pair_headers(
    const std::filesystem::path& project_root, const component& value
);
std::vector<std::filesystem::path> component_c_header_pair_sources(
    const std::filesystem::path& project_root, const component& value
);
std::vector<std::filesystem::path> component_test_sources(
    const std::filesystem::path& project_root, const component& value
);
std::vector<std::filesystem::path> component_benchmark_sources(
    const std::filesystem::path& project_root, const component& value
);
bool component_is_test_only(const component& value);
bool component_is_benchmark_only(const component& value);
std::optional<std::string>
component_generated_test_target(const component& value);
std::optional<std::filesystem::path> artifact_output_path(
    const std::filesystem::path& build_dir,
    const artifact& artifact_value,
    const std::string& output_name
);

std::vector<std::filesystem::path> format_candidate_files(
    const manifest& value, const std::filesystem::path& project_root,
    const std::optional<artifact_ref>& requested_artifact = std::nullopt
);
// All owned C/C++ inputs, including missing files so callers can diagnose them.
std::vector<std::filesystem::path> declared_cpp_files(
    const manifest& value, const std::filesystem::path& project_root,
    const std::optional<artifact_ref>& requested_artifact = std::nullopt
);
std::vector<cxx_analysis_source> cxx_analysis_sources(
    const manifest& value,
    const std::filesystem::path& project_root,
    const std::optional<std::string>& component_filter,
    bool include_tests,
    bool include_benchmarks
);
std::vector<std::filesystem::path> manifest_include_dirs(
    const manifest& value,
    const std::filesystem::path& project_root,
    const std::optional<std::string>& component_filter,
    bool include_tests,
    bool include_benchmarks
);

string_list missing_declared_files(const manifest& value, const std::filesystem::path& project_root);

std::vector<std::string> supported_build_profiles(const manifest& value);
std::vector<std::string> supported_check_profiles(const manifest& value);
std::vector<std::string> supported_report_kinds();
std::vector<std::string> supported_platforms(const manifest& value);
std::vector<artifact_ref> artifact_refs_for_kind(const manifest& value, const std::string& kind);
std::vector<artifact_ref> benchmark_artifact_refs(const manifest& value);

bool supports_build_profile(const manifest& value, const std::string& profile);
bool supports_check_profile(const manifest& value, const std::string& profile);
bool supports_report_kind(const std::string& kind);
bool manifest_has_stack_key(const manifest& value, const std::string& key);
bool has_tests_enabled(const manifest& value);
bool has_benchmarks_enabled(const manifest& value);

std::string cmake_target_name(const artifact_ref& ref);

}  // namespace ecosystem
