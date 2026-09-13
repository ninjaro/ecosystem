#include "workspace/read_commands.hpp"

#include "analysis/clang.hpp"
#include "analysis/tidy.hpp"
#include "command_internal.hpp"
#include "manifest.hpp"
#include "packages/package_catalog.hpp"
#include "packages/package_summary.hpp"
#include "workspace/benchmark.hpp"
#include "workspace/doctor.hpp"
#include "workspace/groups.hpp"
#include "workspace/mutation.hpp"
#include "workspace/project.hpp"
#include "workspace/release.hpp"
#include "workspace/repository.hpp"
#include "workspace/sync.hpp"
#include "workspace/tooling.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace ecosystem {
namespace command_support {

    const std::vector<std::string> known_build_profiles { "debug", "release",
                                                          "android", "kde" };
    const std::vector<std::string> known_check_profiles {
        "tests",  "coverage", "leaks", "java", "tidy",   "format",
        "naming", "style",    "repo",  "doxy", "sphinx", "ci",
    };
    const std::vector<std::string> known_report_kinds { "cxx", "toolchains",
                                                        "matrix", "naming",
                                                        "style" };

    command_error run_format_files(
        const fs::path& project_root, const manifest& manifest_value,
        const std::optional<artifact_ref>& requested_artifact, const bool apply,
        std::ostream& out, std::ostream& err
    ) {
        if (requested_artifact
            && !resolve_artifact(manifest_value, requested_artifact)) {
            print_error(
                err, command_error::invalid_request,
                "unknown artifact request: "
                    + format_artifact_ref(*requested_artifact)
            );
            return command_error::invalid_request;
        }
        const auto files = format_candidate_files(
            manifest_value, project_root, requested_artifact
        );
        std::vector<fs::path> relative_paths;
        for (const auto& file : files)
            relative_paths.push_back(file.lexically_relative(project_root));
        const auto path_errors
            = validate_project_paths(project_root, relative_paths);
        if (!path_errors.empty()) {
            print_error(
                err, command_error::invalid_request, path_errors.front()
            );
            return command_error::invalid_request;
        }
        const tool_status tool = probe_tool("clang-format");
        if (!tool.available) {
            print_error(
                err, command_error::missing_local_tooling,
                "clang-format is not available"
            );
            return command_error::missing_local_tooling;
        }
        ensure_local_artifacts(project_root, true, false, false);
        if (files.empty()) {
            out << "format: no files\n";
            return command_error::ok;
        }
        std::vector<std::string> args { tool.path, apply ? "-i" : "--dry-run" };
        if (!apply)
            args.push_back("--Werror");
        args.push_back("-style=file");
        for (const auto& file : files)
            args.push_back(file.string());
        if (run_command(args, project_root) != 0) {
            print_error(
                err, command_error::task_failed,
                apply ? "clang-format failed while formatting selected files"
                      : "clang-format reported formatting drift"
            );
            return command_error::task_failed;
        }
        if (apply)
            out << "formatted " << files.size() << " file(s)\n";
        else
            out << "format check passed\n";
        return command_error::ok;
    }

    bool contains_string(
        const std::vector<std::string>& values, const std::string& candidate
    ) {
        return std::find(values.begin(), values.end(), candidate)
            != values.end();
    }

    bool path_is_executable(const fs::path& path);
    bool path_exists(const fs::path& path);
    command_error build_target(
        const fs::path& project_root, const std::string& profile,
        const std::string& target, std::ostream& err
    );
    std::optional<fs::path> runnable_binary_path(
        const fs::path& project_root, const manifest& manifest_value,
        const std::string& profile, const resolved_artifact& resolved
    );

    fs::path java_project_dir(const fs::path& project_root) {
        return project_root / "java";
    }

    bool has_java_gradle_surface(const fs::path& project_root) {
        const fs::path gradle_dir = java_project_dir(project_root);
        return path_exists(gradle_dir / "build.gradle")
            || path_exists(gradle_dir / "build.gradle.kts");
    }

    bool project_has_docs_surface(const fs::path& project_root) {
        if (!path_exists(project_root / "docs")) {
            return false;
        }
        return path_exists(project_root / "docs" / "index.md")
            || path_exists(project_root / "docs" / "index.rst");
    }

    bool project_supports_java_check(
        const fs::path& project_root, const manifest& manifest_value
    ) {
        return manifest_has_stack_key(manifest_value, "jni")
            && has_java_gradle_surface(project_root);
    }

    std::vector<std::pair<std::string, std::string>>
    sphinx_environment(const std::optional<std::string>& sphinx_theme) {
        if (!sphinx_theme.has_value()) {
            return {};
        }
        return {
            { "MANIFESTO_SPHINX_THEME", *sphinx_theme },
            { "ECOSYSTEM_SPHINX_THEME", *sphinx_theme },
        };
    }

    gradle_command resolve_gradle_command(const fs::path& project_root) {
        const fs::path wrapper_path
            = java_project_dir(project_root) / "gradlew";
        if (path_is_executable(wrapper_path)) {
            return gradle_command { true, wrapper_path.string() };
        }

        const tool_status gradle_tool = probe_tool("gradle");
        if (gradle_tool.available) {
            return gradle_command { true, gradle_tool.path };
        }

        return gradle_command {};
    }

    command_error build_artifacts(
        const fs::path& project_root, const std::string& profile,
        const std::vector<artifact_ref>& refs, std::ostream& err
    ) {
        std::set<std::string> built_targets;
        for (const artifact_ref& ref : refs) {
            const std::string target_name = cmake_target_name(ref);
            if (!built_targets.insert(target_name).second) {
                continue;
            }

            const command_error status
                = build_target(project_root, profile, target_name, err);
            if (status != command_error::ok) {
                return status;
            }
        }
        return command_error::ok;
    }

    std::string join_set(
        const std::set<std::string>& values, const std::string& separator
    ) {
        std::ostringstream stream;
        bool first = true;
        for (const std::string& value : values) {
            if (!first) {
                stream << separator;
            }
            stream << value;
            first = false;
        }
        return stream.str();
    }

    std::string
    join_strings(const string_list& values, const std::string& separator) {
        std::ostringstream stream;
        bool first = true;
        for (const std::string& value : values) {
            if (!first) {
                stream << separator;
            }
            stream << value;
            first = false;
        }
        return stream.str();
    }

    command_error
    combine_status(const command_error current, const command_error next) {
        return exit_code(next) > exit_code(current) ? next : current;
    }

    void print_error(
        std::ostream& err, const command_error error_class,
        const std::string& message
    ) {
        switch (error_class) {
        case command_error::invalid_request:
            err << "error[invalid_request]: " << message << "\n";
            break;
        case command_error::unsupported_by_manifest:
            err << "error[unsupported_by_manifest]: " << message << "\n";
            break;
        case command_error::missing_local_tooling:
            err << "error[missing_local_tooling]: " << message << "\n";
            break;
        case command_error::task_failed:
            err << "error[task_failed]: " << message << "\n";
            break;
        case command_error::ok:
            break;
        }
    }

    manifest load_current_manifest(
        const fs::path& project_root, std::ostream& err, int* exit_status
    ) {
        const manifest_report report
            = load_manifest(project_root / "manifest.json");
        if (!report.errors.empty() || !report.value.has_value()) {
            for (const std::string& message : report.errors) {
                print_error(err, command_error::invalid_request, message);
            }
            *exit_status = exit_code(command_error::invalid_request);
            return manifest {};
        }
        *exit_status = 0;
        return *report.value;
    }

    void
    emit_manifest_errors(const manifest_report& report, std::ostream& err) {
        for (const std::string& message : report.errors) {
            print_error(err, command_error::invalid_request, message);
        }
    }

    std::string trim_copy(const std::string& value) {
        const std::size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }
        const std::size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1U);
    }

    bool starts_with(const std::string& value, const std::string& prefix) {
        return value.rfind(prefix, 0U) == 0U;
    }

    bool path_is_executable(const fs::path& path) {
        return !path.empty() && ::access(path.c_str(), X_OK) == 0;
    }

    bool path_exists(const fs::path& path) {
        if (path.empty()) {
            return false;
        }
        std::error_code error;
        return fs::exists(path, error) && !error;
    }

    void append_unique_paths(
        std::vector<fs::path>* files, const std::vector<fs::path>& candidates
    ) {
        std::set<std::string> seen;
        for (const fs::path& existing : *files) {
            seen.insert(existing.lexically_normal().generic_string());
        }

        for (const fs::path& candidate : candidates) {
            const std::string key
                = candidate.lexically_normal().generic_string();
            if (seen.insert(key).second) {
                files->push_back(candidate);
            }
        }
    }

    std::optional<int> parse_positive_int(const std::string& value) {
        if (value.empty()) {
            return std::nullopt;
        }
        for (const char character : value) {
            if (!std::isdigit(static_cast<unsigned char>(character))) {
                return std::nullopt;
            }
        }

        const int parsed = std::stoi(value);
        return parsed > 0 ? std::make_optional(parsed) : std::nullopt;
    }

    bool process_is_traced() {
        std::ifstream status("/proc/self/status", std::ios::binary);
        if (!status.is_open()) {
            return false;
        }

        std::string line;
        while (std::getline(status, line)) {
            if (!starts_with(line, "TracerPid:")) {
                continue;
            }
            const std::optional<int> tracer_pid
                = parse_positive_int(trim_copy(line.substr(10U)));
            return tracer_pid.has_value();
        }
        return false;
    }

    command_error spawn_background_process(
        const std::vector<std::string>& args, const fs::path& log_path,
        std::ostream& err
    ) {
        if (args.empty()) {
            print_error(
                err, command_error::invalid_request,
                "background command is empty"
            );
            return command_error::invalid_request;
        }

        std::error_code fs_error;
        if (!log_path.parent_path().empty()) {
            fs::create_directories(log_path.parent_path(), fs_error);
            if (fs_error) {
                print_error(
                    err, command_error::task_failed,
                    "unable to prepare android log directory: "
                        + fs_error.message()
                );
                return command_error::task_failed;
            }
        }

        const pid_t child = ::fork();
        if (child < 0) {
            print_error(
                err, command_error::task_failed,
                "unable to launch background android emulator"
            );
            return command_error::task_failed;
        }
        if (child == 0) {
            const int log_fd
                = ::open(log_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
            if (log_fd >= 0) {
                ::dup2(log_fd, STDOUT_FILENO);
                ::dup2(log_fd, STDERR_FILENO);
                if (log_fd > STDERR_FILENO) {
                    ::close(log_fd);
                }
            }
            ::setsid();

            std::vector<char*> argv;
            argv.reserve(args.size() + 1U);
            for (const std::string& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);
            ::execv(args.front().c_str(), argv.data());
            ::_exit(127);
        }

        return command_error::ok;
    }

    void emit_dependency_line(std::ostream& out, const package_line& line) {
        out << "    " << line.label;
        if (line.include_values && !line.entry.values.empty()) {
            out << ": " << join_set(line.entry.values, " ");
        }
        if (!line.entry.owners.empty()) {
            out << " (components: " << join_set(line.entry.owners, ", ") << ")";
        }
        out << "\n";
    }

    void emit_declared_dependencies(
        std::ostream& out, const manifest& manifest_value,
        const std::optional<artifact_ref>& requested_artifact,
        const std::optional<std::string>& profile
    ) {
        const dependency_summary dependencies
            = summarize_dependencies(manifest_value, requested_artifact);
        const bool kde_enabled_for_profile
            = profile.has_value() && *profile == "kde";
        const std::vector<package_section> sections
            = declared_package_sections(dependencies, kde_enabled_for_profile);

        if (sections.empty()) {
            out << "declared dependencies: none\n";
            return;
        }

        out << "declared dependencies:\n";
        for (const package_section& section : sections) {
            out << "  " << package_group_heading(section.group) << ":\n";
            for (const package_line& line : section.packages) {
                emit_dependency_line(out, line);
            }
        }
    }

    void emit_configured_package_line(
        std::ostream& out, const package_status_line& line
    ) {
        out << "    " << line.label << ": " << line.status << "\n";
    }

    void emit_configured_package_state(
        std::ostream& out, const fs::path& project_root,
        const manifest& manifest_value,
        const std::optional<artifact_ref>& requested_artifact,
        const build_cache_status& cache_status, const bool refreshed
    ) {
        const dependency_summary dependencies
            = summarize_dependencies(manifest_value, requested_artifact);
        if (!dependency_summary_has_entries(dependencies)) {
            return;
        }

        const std::optional<cmake_cache_snapshot> cache
            = load_cmake_cache(cache_status.cache_path);
        if (!cache.has_value()) {
            out << "configured package state: unavailable ("
                << cache_status.cache_path.lexically_relative(project_root)
                       .generic_string()
                << " missing)\n";
            return;
        }
        if (cache_status.stale) {
            out << "configured package state: unavailable ("
                << cache_status.cache_path.lexically_relative(project_root)
                       .generic_string()
                << " stale)\n";
            return;
        }
        const std::vector<package_status_section> sections
            = configured_package_sections(dependencies, *cache);

        out << "configured package state: "
            << cache->path.lexically_relative(project_root).generic_string();
        if (refreshed) {
            out << " (refreshed)";
        }
        out << "\n";
        for (const package_status_section& section : sections) {
            out << "  " << package_group_heading(section.group) << ":\n";
            for (const package_status_line& line : section.packages) {
                emit_configured_package_line(out, line);
            }
        }
    }

    bool artifact_ref_matches_filter(
        const artifact_ref& ref, const artifact_ref& requested_artifact
    ) {
        return ref.component_id == requested_artifact.component_id
            && ref.artifact_id == requested_artifact.artifact_id;
    }

    bool artifact_ref_matches_any_filter(
        const artifact_ref& ref, const std::vector<artifact_ref>& requested
    ) {
        if (requested.empty()) {
            return true;
        }
        for (const artifact_ref& filter : requested) {
            if (artifact_ref_matches_filter(ref, filter)) {
                return true;
            }
        }
        return false;
    }

    json matrix_report(
        const manifest& manifest_value,
        const std::vector<artifact_ref>& requested_artifacts
    ) {
        json report = json::object();
        report["project"] = manifest_value.id;
        report["facade_entry"] = manifest_value.facade_entry_artifact;

        json artifacts = json::array();
        for (const component& component_value : manifest_value.components) {
            for (const artifact& artifact_value : component_value.artifacts) {
                const artifact_ref ref { component_value.id,
                                         artifact_value.id };
                if (!artifact_ref_matches_any_filter(
                        ref, requested_artifacts
                    )) {
                    continue;
                }
                json artifact_json = json::object();
                artifact_json["ref"] = format_artifact_ref(ref);
                artifact_json["kind"] = artifact_value.kind;

                json build_profiles = json::object();
                for (const std::string& profile : known_build_profiles) {
                    build_profiles[profile]
                        = supports_build_profile(manifest_value, profile);
                }
                artifact_json["build_profiles"] = build_profiles;

                json check_profiles = json::object();
                for (const std::string& profile : known_check_profiles) {
                    check_profiles[profile]
                        = supports_check_profile(manifest_value, profile);
                }
                artifact_json["check_profiles"] = check_profiles;

                json reports = json::object();
                for (const std::string& kind : known_report_kinds) {
                    reports[kind] = supports_report_kind(kind);
                }
                artifact_json["reports"] = reports;
                artifacts.push_back(artifact_json);
            }
        }
        report["artifacts"] = artifacts;
        return report;
    }

    json matrix_report(
        const manifest& manifest_value,
        const std::optional<artifact_ref>& requested_artifact
    ) {
        if (!requested_artifact.has_value()) {
            return matrix_report(manifest_value, std::vector<artifact_ref> {});
        }
        return matrix_report(
            manifest_value, std::vector<artifact_ref> { *requested_artifact }
        );
    }

    json workspace_matrix_report(
        const workspace_context& workspace, const workspace_scope& scope
    ) {
        json report = json::object();
        report["workspace_root"] = workspace.root.string();

        json projects = json::array();
        for (const workspace_project* project :
             selected_workspace_projects(workspace, scope)) {
            if (!project->valid()) {
                projects.push_back(workspace_project_json(workspace, *project));
                continue;
            }
            json project_json = matrix_report(
                *project->manifest_value,
                workspace_artifacts_for_project(scope, project)
            );
            project_json.update(workspace_project_json(workspace, *project));
            projects.push_back(project_json);
        }
        report["projects"] = projects;
        const json selection = workspace_scope_json(workspace, scope);
        if (!selection.is_null() && !selection.empty()) {
            report["selection"] = selection;
        }
        return report;
    }

    void ensure_stream_trailing_newline(std::ostringstream* stream) {
        const std::string value = stream->str();
        if (!value.empty() && value.back() != '\n') {
            *stream << "\n";
        }
    }

    void emit_workspace_project_output(
        const workspace_context& workspace, const workspace_project* project,
        const std::ostringstream& project_out,
        const std::ostringstream& project_err, std::ostream& out,
        std::ostream& err
    ) {
        out << "== " << project->identity() << " ("
            << relative_workspace_path(workspace, project->root) << ") ==\n";
        out << project_out.str();
        if (!project_out.str().empty() && project_out.str().back() != '\n') {
            out << "\n";
        }
        if (!project_err.str().empty()) {
            err << "== " << project->identity() << " ("
                << relative_workspace_path(workspace, project->root)
                << ") ==\n";
            err << project_err.str();
            if (project_err.str().back() != '\n') {
                err << "\n";
            }
        }
    }

    bool
    check_profile_supports_multi_artifact_filters(const std::string& profile) {
        return profile == "tests" || profile == "coverage" || profile == "leaks"
            || profile == "tidy" || profile == "format" || profile == "naming"
            || profile == "style";
    }

    bool is_runnable_artifact(const artifact& artifact_value) {
        return artifact_value.kind == "exe" || artifact_value.kind == "qt_app";
    }

    command_error build_target(
        const fs::path& project_root, const std::string& profile,
        const std::string& target, std::ostream& err
    );

    command_error run_build(
        const fs::path& project_root, const manifest& manifest_value,
        const std::string& profile,
        const std::optional<artifact_ref>& requested_artifact,
        std::ostream& out, std::ostream& err
    );

    command_error run_run(
        const fs::path& project_root, const manifest& manifest_value,
        const std::string& profile,
        const std::optional<artifact_ref>& requested_artifact,
        const std::optional<std::string>& android_mode,
        const string_list& passthrough_args, std::ostream& out,
        std::ostream& err
    );

    command_error run_prerelease(
        const fs::path& project_root, const manifest& manifest_value,
        const std::optional<artifact_ref>& requested_artifact,
        const std::optional<std::string>& version_base,
        const prerelease_signing_options& signing_options, std::ostream& out,
        std::ostream& err
    );

    command_error run_benchmark(
        const fs::path& project_root, const manifest& manifest_value,
        const std::optional<artifact_ref>& requested_artifact,
        const string_list& passthrough_args, std::ostream& out,
        std::ostream& err
    );

    std::vector<test_target> collect_test_targets(
        const manifest& manifest_value,
        const std::optional<artifact_ref>& requested_artifact
    ) {
        std::vector<test_target> targets;

        for (const component& component_value : manifest_value.components) {
            if (requested_artifact.has_value()
                && component_value.id != requested_artifact->component_id) {
                continue;
            }

            if (component_is_test_only(component_value)) {
                for (const artifact& artifact_value :
                     component_value.artifacts) {
                    if (!is_runnable_artifact(artifact_value)) {
                        continue;
                    }
                    if (requested_artifact.has_value()
                        && artifact_value.id
                            != requested_artifact->artifact_id) {
                        continue;
                    }
                    const artifact_ref ref { component_value.id,
                                             artifact_value.id };
                    targets.push_back(
                        {
                            cmake_target_name(ref),
                            component_value.ownership
                                ? cmake_target_name(ref)
                                : component_value.id + "_" + artifact_value.id,
                            artifact_output_name(
                                manifest_value, component_value, artifact_value
                            ),
                        }
                    );
                }
                continue;
            }

            const auto target
                = component_generated_test_target(component_value);
            if (!target
                || (requested_artifact && component_value.ownership
                    && component_value.artifacts.front().id
                        != requested_artifact->artifact_id))
                continue;
            targets.push_back(
                { *target,
                  component_value.ownership ? *target
                                            : component_value.id + "_tests",
                  *target }
            );
        }

        return targets;
    }

    command_error build_facade_entry_artifact(
        const fs::path& project_root, const manifest& manifest_value,
        const std::string& profile, std::ostream& err
    ) {
        const std::optional<resolved_artifact> entry_artifact
            = resolve_artifact(manifest_value, std::nullopt);
        if (!entry_artifact.has_value()) {
            print_error(
                err, command_error::invalid_request,
                "manifest facade entry artifact is invalid"
            );
            return command_error::invalid_request;
        }
        return build_target(
            project_root, profile, cmake_target_name(entry_artifact->ref), err
        );
    }

    std::string ctest_regex_for(const std::vector<test_target>& targets) {
        std::string regex = "^(";
        for (std::size_t index = 0; index < targets.size(); ++index) {
            if (index > 0U) {
                regex += "|";
            }
            regex += targets[index].test_name;
        }
        regex += ")$";
        return regex;
    }

    command_error build_test_targets(
        const fs::path& project_root, const std::string& profile,
        const std::vector<test_target>& targets, std::ostream& err
    ) {
        for (const test_target& target : targets) {
            const command_error status
                = build_target(project_root, profile, target.build_target, err);
            if (status != command_error::ok) {
                return status;
            }
        }
        return command_error::ok;
    }

    command_error run_configure_build_tree(
        const fs::path& project_root, const manifest& manifest_value,
        const std::string& profile, const bool with_tests,
        const bool with_coverage, const bool with_benchmarks, std::ostream& err
    ) {
        std::string error_message;
        const command_error status = configure_build_tree(
            project_root, manifest_value, profile, with_tests, with_coverage,
            with_benchmarks, &error_message
        );
        if (status != command_error::ok && !error_message.empty()) {
            print_error(err, status, error_message);
        }
        return status;
    }

    command_error build_target(
        const fs::path& project_root, const std::string& profile,
        const std::string& target, std::ostream& err
    ) {
        const tool_status cmake_tool = probe_tool("cmake");
        if (!cmake_tool.available) {
            print_error(
                err, command_error::missing_local_tooling,
                "cmake is not available"
            );
            return command_error::missing_local_tooling;
        }
        const fs::path build_dir = local_build_dir(project_root, profile);
        const int build_status = run_command(
            {
                cmake_tool.path,
                "--build",
                build_dir.string(),
                "--target",
                target,
            },
            project_root
        );
        if (build_status != 0) {
            print_error(
                err, command_error::task_failed,
                "cmake build failed for target " + target
            );
            return command_error::task_failed;
        }
        return command_error::ok;
    }

    command_error resolve_benchmark_artifact(
        const manifest& manifest_value,
        const std::optional<artifact_ref>& requested_artifact,
        resolved_artifact* resolved, std::ostream& err
    ) {
        if (requested_artifact.has_value()) {
            const std::optional<resolved_artifact> direct
                = resolve_artifact(manifest_value, requested_artifact);
            if (!direct.has_value()) {
                print_error(
                    err, command_error::invalid_request,
                    "unknown artifact request"
                );
                return command_error::invalid_request;
            }
            if (!component_is_benchmark_only(*direct->component_value)) {
                print_error(
                    err, command_error::invalid_request,
                    "benchmark requires a benchmark component artifact"
                );
                return command_error::invalid_request;
            }
            if (direct->artifact_value->kind != "exe"
                && direct->artifact_value->kind != "qt_app") {
                print_error(
                    err, command_error::invalid_request,
                    "benchmark requires a runnable executable artifact"
                );
                return command_error::invalid_request;
            }
            *resolved = *direct;
            return command_error::ok;
        }

        const std::vector<artifact_ref> benchmark_refs
            = benchmark_artifact_refs(manifest_value);
        if (benchmark_refs.empty()) {
            print_error(
                err, command_error::unsupported_by_manifest,
                "manifest does not declare any benchmark artifacts"
            );
            return command_error::unsupported_by_manifest;
        }
        if (benchmark_refs.size() > 1U) {
            print_error(
                err, command_error::invalid_request,
                "multiple benchmark artifacts are available; select one "
                "explicitly"
            );
            return command_error::invalid_request;
        }

        const std::optional<resolved_artifact> direct = resolve_artifact(
            manifest_value, std::make_optional(benchmark_refs.front())
        );
        if (!direct.has_value()) {
            print_error(
                err, command_error::invalid_request,
                "unknown benchmark artifact request"
            );
            return command_error::invalid_request;
        }
        *resolved = *direct;
        return command_error::ok;
    }

    command_error run_list(
        const manifest& manifest_value,
        const std::optional<std::string>& target, std::ostream& out
    ) {
        if (!target.has_value()) {
            out << "project: " << manifest_value.id << "\n";
            out << "description: " << manifest_value.description << "\n";
            out << "facade entry: " << manifest_value.facade_entry_artifact
                << "\n";
            out << "components: " << manifest_value.components.size() << "\n";
            out << "artifacts:\n";
            for (const component& component_value : manifest_value.components) {
                for (const artifact& artifact_value :
                     component_value.artifacts) {
                    out << "  " << component_value.id << ":"
                        << artifact_value.id << " (" << artifact_value.kind
                        << ")\n";
                }
            }
            return command_error::ok;
        }

        if (*target == "components") {
            for (const component& component_value : manifest_value.components) {
                out << component_value.id << " : "
                    << component_value.description << "\n";
            }
            return command_error::ok;
        }
        if (*target == "artifacts") {
            for (const component& component_value : manifest_value.components) {
                for (const artifact& artifact_value :
                     component_value.artifacts) {
                    out << component_value.id << ":" << artifact_value.id
                        << " : " << artifact_value.kind << "\n";
                }
            }
            return command_error::ok;
        }
        if (*target == "profiles") {
            out << "build:\n";
            for (const std::string& profile : known_build_profiles) {
                out << "  " << profile << " : "
                    << (supports_build_profile(manifest_value, profile)
                            ? "supported"
                            : "unsupported")
                    << "\n";
            }
            out << "check:\n";
            for (const std::string& profile : known_check_profiles) {
                out << "  " << profile << " : "
                    << (supports_check_profile(manifest_value, profile)
                            ? "supported"
                            : "unsupported")
                    << "\n";
            }
            out << "report:\n";
            for (const std::string& kind : known_report_kinds) {
                out << "  " << kind << " : supported\n";
            }
            return command_error::ok;
        }
        if (*target == "platforms") {
            out << "native\n";
            if (supports_build_profile(manifest_value, "android")) {
                out << "android\n";
            }
            if (supports_build_profile(manifest_value, "kde")) {
                out << "kde\n";
            }
            return command_error::ok;
        }
        if (*target == "matrix") {
            out << matrix_report(manifest_value, std::nullopt).dump(2) << "\n";
            return command_error::ok;
        }

        return command_error::invalid_request;
    }

    bool emit_invalid_workspace_project(
        const workspace_context& workspace, const workspace_project& project,
        std::ostream& out
    ) {
        if (project.valid()) {
            return false;
        }
        out << project.identity() << " : "
            << relative_workspace_path(workspace, project.manifest_path)
            << " [invalid]\n";
        for (const std::string& message : project.errors) {
            out << "  " << message << "\n";
        }
        return true;
    }

    command_error run_workspace_list(
        const workspace_context& workspace,
        const std::optional<std::string>& target, std::ostream& out
    ) {
        if (!target.has_value()) {
            out << "workspace: " << workspace.root.filename().generic_string()
                << "\n";
            out << "projects: " << workspace.projects.size() << "\n";
            for (const workspace_project& project : workspace.projects) {
                if (emit_invalid_workspace_project(workspace, project, out)) {
                    continue;
                }
                out << "  " << project.identity() << " : "
                    << relative_workspace_path(workspace, project.root) << "\n";
            }
            if (!workspace.config.groups.empty()) {
                out << "groups: " << workspace.config.groups.size() << "\n";
                for (const workspace_group& group : workspace.config.groups) {
                    out << "  " << group.id << " : "
                        << join_strings(group.project_selectors, " ") << "\n";
                }
            }
            return command_error::ok;
        }

        if (*target == "projects") {
            for (const workspace_project& project : workspace.projects) {
                if (emit_invalid_workspace_project(workspace, project, out)) {
                    continue;
                }
                out << project.identity() << " : "
                    << relative_workspace_path(workspace, project.root) << "\n";
            }
            return command_error::ok;
        }
        if (*target == "components") {
            for (const workspace_project& project : workspace.projects) {
                if (emit_invalid_workspace_project(workspace, project, out)) {
                    continue;
                }
                for (const component& component_value :
                     project.manifest_value->components) {
                    out << project.identity() << "/" << component_value.id
                        << " : " << component_value.description << "\n";
                }
            }
            return command_error::ok;
        }
        if (*target == "artifacts") {
            for (const workspace_project& project : workspace.projects) {
                if (emit_invalid_workspace_project(workspace, project, out)) {
                    continue;
                }
                for (const component& component_value :
                     project.manifest_value->components) {
                    for (const artifact& artifact_value :
                         component_value.artifacts) {
                        out << project.identity() << "/" << component_value.id
                            << ":" << artifact_value.id << " : "
                            << artifact_value.kind << "\n";
                    }
                }
            }
            return command_error::ok;
        }
        if (*target == "profiles") {
            for (const workspace_project& project : workspace.projects) {
                if (emit_invalid_workspace_project(workspace, project, out)) {
                    continue;
                }
                out << project.identity() << "\n";
                out << "  build:";
                for (const std::string& profile :
                     supported_build_profiles(*project.manifest_value)) {
                    out << " " << profile;
                }
                out << "\n";
                out << "  check:";
                for (const std::string& profile :
                     supported_check_profiles(*project.manifest_value)) {
                    out << " " << profile;
                }
                out << "\n";
            }
            return command_error::ok;
        }
        if (*target == "platforms") {
            for (const workspace_project& project : workspace.projects) {
                if (emit_invalid_workspace_project(workspace, project, out)) {
                    continue;
                }
                out << project.identity() << " :";
                for (const std::string& platform :
                     supported_platforms(*project.manifest_value)) {
                    out << " " << platform;
                }
                out << "\n";
            }
            return command_error::ok;
        }
        if (*target == "matrix") {
            out << workspace_matrix_report(workspace, workspace_scope {})
                       .dump(2)
                << "\n";
            return command_error::ok;
        }
        if (*target == "groups") {
            for (const workspace_group& group : workspace.config.groups) {
                out << group.id << " : "
                    << join_strings(group.project_selectors, " ") << "\n";
            }
            return command_error::ok;
        }

        return command_error::invalid_request;
    }

    std::optional<fs::path> runnable_binary_path(
        const fs::path& project_root, const manifest& manifest_value,
        const std::string& profile, const resolved_artifact& resolved
    ) {
        const fs::path build_dir = local_build_dir(project_root, profile);
        const std::string output_name = artifact_output_name(
            manifest_value, *resolved.component_value, *resolved.artifact_value
        );
        return artifact_output_path(
            build_dir, *resolved.artifact_value, output_name
        );
    }

    command_error run_runnable_binary(
        const fs::path& build_dir, const fs::path& binary_path,
        const string_list& passthrough_args, std::ostream& err
    ) {
        std::vector<std::string> args { binary_path.string() };
        args.insert(
            args.end(), passthrough_args.begin(), passthrough_args.end()
        );
        if (run_command(args, build_dir) != 0) {
            print_error(
                err, command_error::task_failed,
                "runnable artifact exited with a failure status"
            );
            return command_error::task_failed;
        }
        return command_error::ok;
    }

    std::string android_serial_from_devices_output(
        const std::string& devices_output, const bool emulator
    ) {
        std::istringstream stream(devices_output);
        std::string line;
        while (std::getline(stream, line)) {
            const std::string trimmed = trim_copy(line);
            if (trimmed.empty()
                || starts_with(trimmed, "List of devices attached")) {
                continue;
            }
            const std::string serial
                = trimmed.substr(0U, trimmed.find_first_of(" \t"));
            if (serial.empty()) {
                continue;
            }
            const bool is_emulator = starts_with(serial, "emulator-");
            if (is_emulator == emulator) {
                return serial;
            }
        }
        return {};
    }

    std::string android_emulator_serial(const std::string& adb_path) {
        return android_serial_from_devices_output(
            capture_command({ adb_path, "devices", "-l" }), true
        );
    }

    std::string android_device_serial(const std::string& adb_path) {
        return android_serial_from_devices_output(
            capture_command({ adb_path, "devices", "-l" }), false
        );
    }

    std::optional<fs::path> android_apk_path(
        const fs::path& build_dir, const std::string& target_name
    ) {
        std::vector<fs::path> candidates;
        std::error_code error;
        if (!fs::exists(build_dir, error) || error) {
            return std::nullopt;
        }

        const fs::path target_apk
            = build_dir / "android-build" / (target_name + ".apk");
        if (fs::is_regular_file(target_apk, error) && !error) {
            return target_apk;
        }
        error.clear();

        const std::string preferred_abi = []() {
            const char* value = std::getenv("ANDROID_ABI");
            return value == nullptr ? std::string() : std::string(value);
        }();
        const std::string path_hint = []() {
            const char* value = std::getenv("ANDROID_APK_PATH_HINT");
            return value == nullptr ? std::string() : std::string(value);
        }();

        fs::recursive_directory_iterator iterator(build_dir, error);
        const fs::recursive_directory_iterator end;
        while (!error && iterator != end) {
            if (iterator->is_regular_file(error)) {
                const std::string filename
                    = iterator->path().filename().generic_string();
                if (iterator->path().extension() == ".apk"
                    && filename.find("-debug.apk") != std::string::npos) {
                    candidates.push_back(iterator->path());
                }
            }
            error.clear();
            iterator.increment(error);
        }

        std::sort(candidates.begin(), candidates.end());
        if (candidates.empty()) {
            return std::nullopt;
        }

        const auto narrow_candidates = [&candidates](const std::string& hint) {
            if (hint.empty()) {
                return;
            }

            std::vector<fs::path> matches;
            for (const fs::path& candidate : candidates) {
                if (candidate.generic_string().find(hint)
                    != std::string::npos) {
                    matches.push_back(candidate);
                }
            }
            if (!matches.empty()) {
                candidates = std::move(matches);
            }
        };
        narrow_candidates(target_name);
        narrow_candidates(path_hint);
        narrow_candidates(preferred_abi);

        return candidates.size() == 1U ? std::make_optional(candidates.front())
                                       : std::nullopt;
    }

    std::optional<std::string> android_apk_package_name(
        const android_environment& environment, const fs::path& apk_path
    ) {
        if (!path_is_executable(environment.aapt_bin)) {
            return std::nullopt;
        }

        const std::string badging = capture_command(
            {
                environment.aapt_bin,
                "dump",
                "badging",
                apk_path.string(),
            }
        );
        const std::string prefix = "package: name='";
        const std::size_t start = badging.find(prefix);
        if (start == std::string::npos) {
            return std::nullopt;
        }
        const std::size_t value_start = start + prefix.size();
        const std::size_t value_end = badging.find('\'', value_start);
        if (value_end == std::string::npos || value_end <= value_start) {
            return std::nullopt;
        }
        return badging.substr(value_start, value_end - value_start);
    }

    bool android_wait_for_boot(
        const std::string& adb_path, const std::string& serial,
        const int timeout_seconds
    ) {
        if (run_command(
                { adb_path, "-s", serial, "wait-for-device" }, fs::path()
            )
            != 0) {
            return false;
        }

        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::seconds(timeout_seconds);
        while (std::chrono::steady_clock::now() < deadline) {
            if (trim_copy(capture_command(
                    { adb_path, "-s", serial, "shell", "getprop",
                      "sys.boot_completed" }
                ))
                == "1") {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return false;
    }

    command_error resolve_android_serial(
        const fs::path& project_root, const android_environment& environment,
        const std::string& android_mode, std::string* serial, std::ostream& out,
        std::ostream& err
    ) {
        if (!path_is_executable(environment.adb_bin)) {
            print_error(
                err, command_error::missing_local_tooling,
                "android run requires adb; set ADB_BIN or ANDROID_SDK_ROOT"
            );
            return command_error::missing_local_tooling;
        }

        if (android_mode == "device") {
            *serial = android_device_serial(environment.adb_bin);
            if (serial->empty()) {
                print_error(
                    err, command_error::task_failed,
                    "no physical Android device detected; connect a device or "
                    "use --android-mode emulator"
                );
                return command_error::task_failed;
            }
            return command_error::ok;
        }

        *serial = android_emulator_serial(environment.adb_bin);
        if (!serial->empty()) {
            return command_error::ok;
        }

        if (android_mode == "auto") {
            *serial = android_device_serial(environment.adb_bin);
            if (!serial->empty()) {
                return command_error::ok;
            }
        }

        if (!path_is_executable(environment.emulator_bin)) {
            print_error(
                err, command_error::missing_local_tooling,
                "android emulator support requires ANDROID_EMULATOR_BIN or an "
                "installed Android emulator"
            );
            return command_error::missing_local_tooling;
        }

        const std::optional<int> boot_timeout = parse_positive_int([]() {
            const char* value = std::getenv("ANDROID_EMULATOR_BOOT_TIMEOUT");
            return value == nullptr ? std::string() : std::string(value);
        }());
        const int timeout_seconds = boot_timeout.value_or(300);
        const fs::path log_path = local_state_dir(project_root) / "android"
            / ("emulator-" + environment.avd_name + ".log");
        out << "starting android emulator " << environment.avd_name << " (log: "
            << log_path.lexically_relative(project_root).generic_string()
            << ")\n";

        const command_error launch_status = spawn_background_process(
            {
                environment.emulator_bin,
                "-avd",
                environment.avd_name,
                "-netdelay",
                "none",
                "-netspeed",
                "full",
                "-gpu",
                "host",
                "-no-boot-anim",
                "-no-snapshot",
            },
            log_path, err
        );
        if (launch_status != command_error::ok) {
            return launch_status;
        }

        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::seconds(timeout_seconds);
        while (std::chrono::steady_clock::now() < deadline) {
            *serial = android_emulator_serial(environment.adb_bin);
            if (!serial->empty()) {
                return command_error::ok;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        print_error(
            err, command_error::task_failed,
            "android emulator did not appear within the boot timeout; see "
                + log_path.lexically_relative(project_root).generic_string()
        );
        return command_error::task_failed;
    }

    command_error run_android_artifact(
        const fs::path& project_root, const manifest& manifest_value,
        const resolved_artifact& resolved,
        const std::optional<std::string>& requested_mode,
        const string_list& passthrough_args, std::ostream& out,
        std::ostream& err
    ) {
        if (!passthrough_args.empty()) {
            print_error(
                err, command_error::invalid_request,
                "android run does not accept child arguments"
            );
            return command_error::invalid_request;
        }
        if (resolved.artifact_value->kind != "qt_app") {
            print_error(
                err, command_error::invalid_request,
                "android run currently requires a qt_app artifact"
            );
            return command_error::invalid_request;
        }

        const std::string android_mode = requested_mode.value_or("auto");
        if (android_mode != "auto" && android_mode != "emulator"
            && android_mode != "device") {
            print_error(
                err, command_error::invalid_request,
                "android mode must be one of: auto, emulator, device"
            );
            return command_error::invalid_request;
        }

        command_error status = run_configure_build_tree(
            project_root, manifest_value, "android",
            component_is_test_only(*resolved.component_value), false,
            component_is_benchmark_only(*resolved.component_value), err
        );
        if (status != command_error::ok) {
            return status;
        }

        status = build_target(
            project_root, "android", cmake_target_name(resolved.ref), err
        );
        if (status != command_error::ok) {
            return status;
        }

        status = build_target(project_root, "android", "apk", err);
        if (status != command_error::ok) {
            return status;
        }
        const std::optional<fs::path> apk_path = android_apk_path(
            local_build_dir(project_root, "android"),
            cmake_target_name(resolved.ref)
        );
        if (!apk_path.has_value()) {
            print_error(
                err, command_error::task_failed,
                "android build did not produce a debug APK"
            );
            return command_error::task_failed;
        }

        const android_environment environment = detect_android_environment();
        if (!path_is_executable(environment.aapt_bin)) {
            print_error(
                err, command_error::missing_local_tooling,
                "android run requires aapt; set AAPT_BIN or install Android "
                "build-tools"
            );
            return command_error::missing_local_tooling;
        }

        const std::optional<std::string> package_name
            = android_apk_package_name(environment, *apk_path);
        if (!package_name.has_value()) {
            print_error(
                err, command_error::task_failed,
                "unable to derive Android package name from "
                    + apk_path->generic_string()
            );
            return command_error::task_failed;
        }

        std::string serial;
        status = resolve_android_serial(
            project_root, environment, android_mode, &serial, out, err
        );
        if (status != command_error::ok) {
            return status;
        }
        if (!android_wait_for_boot(
                environment.adb_bin, serial,
                parse_positive_int([]() {
                    const char* value
                        = std::getenv("ANDROID_EMULATOR_BOOT_TIMEOUT");
                    return value == nullptr ? std::string()
                                            : std::string(value);
                }())
                    .value_or(300)
            )) {
            print_error(
                err, command_error::task_failed,
                "android device " + serial
                    + " did not finish booting before deployment"
            );
            return command_error::task_failed;
        }

        const fs::path build_dir = local_build_dir(project_root, "android");
        if (run_command(
                {
                    environment.adb_bin,
                    "-s",
                    serial,
                    "install",
                    "-r",
                    apk_path->string(),
                },
                build_dir
            )
            != 0) {
            print_error(err, command_error::task_failed, "adb install failed");
            return command_error::task_failed;
        }

        if (run_command(
                {
                    environment.adb_bin,
                    "-s",
                    serial,
                    "shell",
                    "monkey",
                    "-p",
                    *package_name,
                    "-c",
                    "android.intent.category.LAUNCHER",
                    "1",
                },
                build_dir
            )
            != 0) {
            print_error(err, command_error::task_failed, "adb launch failed");
            return command_error::task_failed;
        }

        out << "ran " << format_artifact_ref(resolved.ref) << " from "
            << apk_path->lexically_relative(project_root).generic_string()
            << " on android device " << serial << "\n";
        return command_error::ok;
    }

} // namespace command_support

command_error run_list(
    const manifest& manifest_value, const std::optional<std::string>& target,
    std::ostream& out
) {
    return command_support::run_list(manifest_value, target, out);
}

command_error run_workspace_list(
    const workspace_context& workspace,
    const std::optional<std::string>& target, std::ostream& out
) {
    return command_support::run_workspace_list(workspace, target, out);
}

} // namespace ecosystem
