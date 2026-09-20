#pragma once

#include "analysis/clang.hpp"
#include "analysis/personal.hpp"
#include "analysis/tidy.hpp"
#include "manifest.hpp"
#include "packages/package_catalog.hpp"
#include "packages/package_metadata.hpp"
#include "packages/package_registry.hpp"
#include "packages/package_rule.hpp"
#include "packages/package_status.hpp"
#include "packages/package_summary.hpp"
#include "packages/package_surface.hpp"
#include "workspace/benchmark.hpp"
#include "workspace/doctor.hpp"
#include "workspace/doxygen.hpp"
#include "workspace/mutation.hpp"
#include "workspace/project.hpp"
#include "workspace/release.hpp"
#include "workspace/repository.hpp"
#include "workspace/sync.hpp"
#include "workspace/template_text.hpp"
#include "workspace/tooling.hpp"
#include "workspace/workspace_scope.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <tuple>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using ecosystem::json;

namespace ecosystem_test_support {

class temp_dir {
public:
    temp_dir() {
        const fs::path base = fs::temp_directory_path();
        for (int attempt = 0; attempt < 128; ++attempt) {
            const fs::path candidate = base
                / ("ecosystem-tests-" + std::to_string(::getpid()) + "-"
                   + std::to_string(attempt));
            std::error_code error;
            if (fs::create_directories(candidate, error)) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("unable to create temporary directory");
    }

    ~temp_dir() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

class scoped_env {
public:
    scoped_env(const std::string& name, const std::string& value)
        : name_(name) {
        const char* previous = std::getenv(name.c_str());
        if (previous != nullptr) {
            had_previous_ = true;
            previous_value_ = previous;
        }
        ::setenv(name.c_str(), value.c_str(), 1);
    }

    ~scoped_env() {
        if (had_previous_) {
            ::setenv(name_.c_str(), previous_value_.c_str(), 1);
        } else {
            ::unsetenv(name_.c_str());
        }
    }

private:
    std::string name_;
    bool had_previous_ = false;
    std::string previous_value_;
};

struct cli_result {
    int exit_code = 0;
    std::string output;
};

void require_true(const bool condition, const std::string& message);

void require_contains(
    const std::string& value, const std::string& expected,
    const std::string& message
);

void require_not_contains(
    const std::string& value, const std::string& unexpected,
    const std::string& message
);

std::string join_lines(const ecosystem::string_list& lines);

const ecosystem::package_section* find_package_section(
    const std::vector<ecosystem::package_section>& sections,
    const ecosystem::package_group group
);

const ecosystem::package_status_section* find_package_status_section(
    const std::vector<ecosystem::package_status_section>& sections,
    const ecosystem::package_group group
);

const ecosystem::package_line* find_package_line(
    const ecosystem::package_section& section, const std::string& label
);

const ecosystem::component* find_component_value(
    const ecosystem::manifest& manifest_value, const std::string& component_id
);

const ecosystem::tracked_surface_file* find_tracked_surface_file(
    const std::vector<ecosystem::tracked_surface_file>& files,
    const std::string& relative_path
);

const ecosystem::package_status_line* find_package_status_line(
    const ecosystem::package_status_section& section, const std::string& label
);

std::string read_text(const fs::path& path);

void write_text(const fs::path& path, const std::string& contents);

const ecosystem::source_analysis* find_source_analysis(
    const ecosystem::cxx_analysis_report& report, const std::string& file
);

const ecosystem::component_analysis_summary* find_component_analysis_summary(
    const ecosystem::cxx_analysis_report& report,
    const std::string& component_id
);

void write_executable_script(
    const fs::path& path, const std::string& contents
);

std::string current_path_env();

void write_fake_configure_and_build_cmake(const fs::path& path);

void write_fake_gpg_tool(const fs::path& path);

void write_fake_configure_cmake_with_compile_database(const fs::path& path);

void write_fake_clang_tidy_tool(const fs::path& path);

ecosystem::manifest sample_manifest();

ecosystem::manifest sample_build_manifest(const std::string& project_id);

void write_sample_build_project(
    const fs::path& project_root, const std::string& project_id,
    const std::string& main_source = "int main() { return 0; }\n"
);

ecosystem::manifest sample_library_manifest();

ecosystem::manifest sample_external_project_manifest();

ecosystem::manifest sample_multi_library_manifest();

ecosystem::manifest sample_facade_library_closure_manifest();

ecosystem::manifest sample_facade_component_library_manifest();

ecosystem::manifest sample_interface_library_manifest();

ecosystem::manifest sample_dual_run_manifest();

ecosystem::manifest sample_shared_runtime_manifest();

ecosystem::manifest sample_leak_check_manifest();

ecosystem::manifest sample_java_binding_manifest();

ecosystem::manifest sample_cxxopts_manifest();

ecosystem::manifest sample_json_manifest();

ecosystem::manifest sample_scoped_probe_manifest();

ecosystem::manifest sample_qttest_manifest();

ecosystem::manifest sample_dependency_manifest();

void write_sample_workspace_project(
    const fs::path& workspace_root, const std::string& project_id
);

void write_sample_dual_run_workspace_project(
    const fs::path& workspace_root, const std::string& project_id
);

void write_sample_workspace_config(const fs::path& workspace_root);

void write_sample_dependency_project(const fs::path& project_root);

void write_sample_dual_run_project(const fs::path& project_root);

void write_sample_leak_check_project(const fs::path& project_root);

void write_sample_java_binding_project(const fs::path& project_root);

void write_sample_sphinx_project(const fs::path& project_root);

fs::path first_recursive_file_with_suffix(
    const fs::path& root, const std::string& suffix
);

fs::path
first_recursive_file_named(const fs::path& root, const std::string& filename);

void write_sample_json_project(const fs::path& project_root);

void write_sample_scoped_probe_project(const fs::path& project_root);

void write_sample_cmake_cache(
    const fs::path& project_root, const std::string& profile,
    const std::string& contents
);

void age_file_by_seconds(
    const fs::path& path, const std::chrono::seconds delta
);

std::string relative_build_cache_path(
    const fs::path& project_root, const std::string& profile
);

std::string
relative_build_dir(const fs::path& project_root, const std::string& profile);

std::string relative_doctor_probe_cache_path(
    const fs::path& project_root,
    const std::optional<ecosystem::artifact_ref>& requested_artifact,
    const std::string& profile
);

cli_result run_cli_with_binary(
    const fs::path& binary_path, const fs::path& working_directory,
    const std::string& arguments
);

fs::path test_cli_build_dir();

fs::path engels_binary_path();

fs::path marx_binary_path();

cli_result run_engels_cli(
    const fs::path& working_directory, const std::string& arguments
);

cli_result
run_marx_cli(const fs::path& working_directory, const std::string& arguments);

cli_result
run_cli(const fs::path& working_directory, const std::string& arguments);

void require_sync_success(
    const fs::path& project_root, const std::string& message
);

void write_source_dependency_fixture(const fs::path& root);

ecosystem::string_list workspace_conformance_errors(const fs::path& root);

std::string github_shell_program(
    const std::string& yaml, const std::string& step_name,
    const std::size_t indentation
);

std::string github_substitute(
    std::string text, const std::string& expression, const std::string& value
);

void write_fake_doxygen_tool(const fs::path& path);

void write_doxygen_project(const fs::path& root, const std::string& id);

void write_runtime_package_project(const fs::path& project);

void write_cxx_report_project(const fs::path& root, const std::string& id);

} // namespace ecosystem_test_support
