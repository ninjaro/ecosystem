#pragma once

#include "analysis/diagnostic.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace ecosystem {

struct source_analysis {
    std::string file;
    std::string component_id;
    std::string category;
    std::string artifact;
    int warning_count = 0;
    int error_count = 0;
    int function_count = 0;
    int class_count = 0;
    int namespace_count = 0;
    string_list diagnostics;
    std::vector<diagnostic_finding> findings;
};

struct component_analysis_summary {
    std::string component_id;
    int files_analyzed = 0;
    int core_files = 0;
    int runtime_files = 0;
    int test_files = 0;
    int benchmark_files = 0;
    int total_warnings = 0;
    int total_errors = 0;
    int function_count = 0;
    int class_count = 0;
    int namespace_count = 0;
};

struct cxx_analysis_report {
    std::string project_id;
    std::optional<artifact_ref> requested_artifact;
    int cpp_standard = 20;
    bool tests_included = false;
    bool benchmarks_included = false;
    int files_analyzed = 0;
    int core_files = 0;
    int runtime_files = 0;
    int test_files = 0;
    int benchmark_files = 0;
    int total_warnings = 0;
    int total_errors = 0;
    int total_functions = 0;
    int total_classes = 0;
    int total_namespaces = 0;
    std::vector<source_analysis> sources;
    std::vector<component_analysis_summary> component_summaries;
    string_list errors;
};

cxx_analysis_report analyze_project_sources(
    const manifest& value, const std::filesystem::path& project_root,
    const std::optional<artifact_ref>& requested_artifact, bool include_tests,
    bool include_benchmarks
);

json to_json(const cxx_analysis_report& value);

}  // namespace ecosystem
