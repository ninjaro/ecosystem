#include "workspace/read_commands.hpp"

#include "analysis/personal.hpp"
#include "command_internal.hpp"
#include "workspace/template_text.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace ecosystem::command_support {

command_error run_check_personal(
    const fs::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    const std::string& profile, std::ostream& out, std::ostream& err
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
    const auto report = analyze_personal_checks(
        manifest_value, project_root, requested_artifact, profile
    );
    const auto report_path
        = local_report_dir(project_root) / (profile + ".json");
    std::string error;
    if (!write_text_file(report_path, to_json(report).dump(2) + "\n", &error)) {
        print_error(err, command_error::task_failed, error);
        return command_error::task_failed;
    }
    render_personal_report(report, out);
    out << profile << " report: "
        << report_path.lexically_relative(project_root).generic_string()
        << "\n";
    if (!report.errors.empty()) {
        for (const auto& message : report.errors)
            err << message << "\n";
        print_error(
            err, command_error::task_failed, profile + " analysis failed"
        );
        return command_error::task_failed;
    }
    return command_error::ok;
}

command_error run_check_leaks(
    const fs::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    std::ostream& out, std::ostream& err
) {
    if (process_is_traced()) {
        print_error(
            err, command_error::missing_local_tooling,
            "sanitizer-backed leak checks are unavailable while the "
            "process is being traced; rerun engels check leaks outside "
            "debugger, ptrace, or nested CTest wrappers"
        );
        return command_error::missing_local_tooling;
    }

    if (!has_tests_enabled(manifest_value)) {
        print_error(
            err, command_error::unsupported_by_manifest,
            "manifest does not declare test support"
        );
        return command_error::unsupported_by_manifest;
    }

    const std::vector<test_target> test_targets
        = collect_test_targets(manifest_value, requested_artifact);
    if (test_targets.empty()) {
        print_error(
            err, command_error::unsupported_by_manifest,
            "no test targets resolve for the requested artifact"
        );
        return command_error::unsupported_by_manifest;
    }

    ensure_local_artifacts(project_root, false, false, false);
    std::string error_message;
    const command_error surface_status = ensure_local_developer_surface(
        project_root, manifest_value, &error_message
    );
    if (surface_status != command_error::ok) {
        print_error(err, surface_status, error_message);
        return surface_status;
    }

    const std::string sanitizer_flags
        = "-fsanitize=address,undefined -fno-omit-frame-pointer "
          "-fno-sanitize-recover=all";
    const command_error configure_status = configure_cmake_source_tree(
        local_developer_source_dir(project_root),
        local_build_dir(project_root, "leaks"),
        {
            "-DECOSYSTEM_BUILD_TESTS=ON",
            "-DECOSYSTEM_BUILD_BENCHMARKS=OFF",
            "-DECOSYSTEM_ENABLE_COVERAGE=OFF",
            "-DECOSYSTEM_PROFILE_KDE=OFF",
            "-DECOSYSTEM_PROFILE_ANDROID=OFF",
            std::string("-DCMAKE_C_FLAGS=") + sanitizer_flags,
            std::string("-DCMAKE_CXX_FLAGS=") + sanitizer_flags,
            std::string("-DCMAKE_EXE_LINKER_FLAGS=") + sanitizer_flags,
            std::string("-DCMAKE_SHARED_LINKER_FLAGS=") + sanitizer_flags,
        },
        "Debug", &error_message
    );
    if (configure_status != command_error::ok) {
        print_error(err, configure_status, error_message);
        return configure_status;
    }

    command_error status = build_facade_entry_artifact(
        project_root, manifest_value, "leaks", err
    );
    if (status != command_error::ok) {
        return status;
    }
    status = build_test_targets(project_root, "leaks", test_targets, err);
    if (status != command_error::ok) {
        return status;
    }

    const tool_status ctest_tool = probe_tool("ctest");
    if (!ctest_tool.available) {
        print_error(
            err, command_error::missing_local_tooling, "ctest is not available"
        );
        return command_error::missing_local_tooling;
    }

    if (run_command(
            {
                ctest_tool.path,
                "--output-on-failure",
                "--no-tests=error",
                "-R",
                ctest_regex_for(test_targets),
            },
            local_build_dir(project_root, "leaks"),
            {
                { "ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1" },
                { "UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1" },
                { "LSAN_OPTIONS", "exitcode=1" },
            }
        )
        != 0) {
        print_error(
            err, command_error::task_failed,
            "sanitizer-backed leak checks failed"
        );
        return command_error::task_failed;
    }

    out << "leak check passed\n";
    return command_error::ok;
}

command_error run_check_tests(
    const fs::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    std::ostream& out, std::ostream& err
) {
    if (!has_tests_enabled(manifest_value)) {
        print_error(
            err, command_error::unsupported_by_manifest,
            "manifest does not declare test support"
        );
        return command_error::unsupported_by_manifest;
    }

    const std::vector<test_target> test_targets
        = collect_test_targets(manifest_value, requested_artifact);
    if (test_targets.empty()) {
        print_error(
            err, command_error::unsupported_by_manifest,
            "no test targets resolve for the requested artifact"
        );
        return command_error::unsupported_by_manifest;
    }

    command_error status = run_configure_build_tree(
        project_root, manifest_value, "debug", true, false, false, err
    );
    if (status != command_error::ok) {
        return status;
    }
    status = build_facade_entry_artifact(project_root, manifest_value, "debug", err);
    if (status != command_error::ok) {
        return status;
    }
    status = build_test_targets(project_root, "debug", test_targets, err);
    if (status != command_error::ok) {
        return status;
    }

    const tool_status ctest_tool = probe_tool("ctest");
    if (!ctest_tool.available) {
        print_error(
            err, command_error::missing_local_tooling, "ctest is not available"
        );
        return command_error::missing_local_tooling;
    }

    std::vector<std::string> ctest_args {
        ctest_tool.path,
        "--output-on-failure",
        "--no-tests=error",
        "-R",
        ctest_regex_for(test_targets),
    };

    const int test_status
        = run_command(ctest_args, local_build_dir(project_root, "debug"));
    if (test_status != 0) {
        print_error(err, command_error::task_failed, "tests failed");
        return command_error::task_failed;
    }

    out << "tests passed\n";
    return command_error::ok;
}

command_error run_check_coverage(
    const fs::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    std::ostream& out, std::ostream& err
) {
    if (!has_tests_enabled(manifest_value)) {
        print_error(
            err, command_error::unsupported_by_manifest,
            "manifest does not declare test support"
        );
        return command_error::unsupported_by_manifest;
    }

    const std::vector<test_target> test_targets
        = collect_test_targets(manifest_value, requested_artifact);
    if (test_targets.empty()) {
        print_error(
            err, command_error::unsupported_by_manifest,
            "no test targets resolve for the requested artifact"
        );
        return command_error::unsupported_by_manifest;
    }

    const tool_status ctest_tool = probe_tool("ctest");
    const tool_status profdata_tool = probe_tool("llvm-profdata");
    const tool_status cov_tool = probe_tool("llvm-cov");
    if (!ctest_tool.available || !profdata_tool.available
        || !cov_tool.available) {
        print_error(
            err, command_error::missing_local_tooling,
            "coverage requires ctest, llvm-profdata, and llvm-cov"
        );
        return command_error::missing_local_tooling;
    }

    command_error status = run_configure_build_tree(
        project_root, manifest_value, "coverage", true, true, false, err
    );
    if (status != command_error::ok) {
        return status;
    }
    status = build_facade_entry_artifact(
        project_root, manifest_value, "coverage", err
    );
    if (status != command_error::ok) {
        return status;
    }
    status = build_test_targets(project_root, "coverage", test_targets, err);
    if (status != command_error::ok) {
        return status;
    }

    const fs::path build_dir = local_build_dir(project_root, "coverage");
    const fs::path profile_dir = build_dir / "profiles";
    std::error_code fs_error;
    fs::create_directories(profile_dir, fs_error);

    const int test_status = run_command(
        {
            ctest_tool.path,
            "--output-on-failure",
            "--no-tests=error",
            "-R",
            ctest_regex_for(test_targets),
        },
        build_dir,
        {
            { "LLVM_PROFILE_FILE", (profile_dir / "%p-%m.profraw").string() },
        }
    );
    if (test_status != 0) {
        print_error(
            err, command_error::task_failed, "coverage test run failed"
        );
        return command_error::task_failed;
    }

    std::vector<std::string> merge_args { profdata_tool.path, "merge",
                                          "-sparse" };
    for (const fs::directory_entry& entry : fs::directory_iterator(profile_dir)) {
        if (entry.path().extension() == ".profraw") {
            merge_args.push_back(entry.path().string());
        }
    }
    if (merge_args.size() == 3U) {
        print_error(
            err, command_error::task_failed,
            "no coverage profiles were generated"
        );
        return command_error::task_failed;
    }

    const fs::path profile_data = profile_dir / "coverage.profdata";
    merge_args.push_back("-o");
    merge_args.push_back(profile_data.string());
    if (run_command(merge_args, build_dir) != 0) {
        print_error(
            err, command_error::task_failed, "llvm-profdata merge failed"
        );
        return command_error::task_failed;
    }

    ensure_local_artifacts(project_root, false, false, false);
    const fs::path report_path = local_report_dir(project_root) / "coverage.json";
    std::vector<std::string> export_args {
        cov_tool.path,
        "export",
        (build_dir / test_targets.front().binary_name).string(),
        "-instr-profile",
        profile_data.string(),
        "-format=text",
    };
    for (std::size_t index = 1U; index < test_targets.size(); ++index) {
        export_args.push_back("-object");
        export_args.push_back(
            (build_dir / test_targets[index].binary_name).string()
        );
    }
    const std::string export_json = capture_command(export_args, build_dir);
    std::string error_message;
    if (!write_text_file(report_path, export_json, &error_message)) {
        print_error(err, command_error::task_failed, error_message);
        return command_error::task_failed;
    }
    out << "coverage report: "
        << report_path.lexically_relative(project_root).generic_string()
        << "\n";
    return command_error::ok;
}

command_error run_check_java(
    const fs::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    std::ostream& out, std::ostream& err
) {
    if (requested_artifact.has_value()) {
        print_error(
            err, command_error::invalid_request,
            "java check does not support artifact filters"
        );
        return command_error::invalid_request;
    }
    if (!manifest_has_stack_key(manifest_value, "jni")) {
        print_error(
            err, command_error::unsupported_by_manifest,
            "manifest does not declare JNI support"
        );
        return command_error::unsupported_by_manifest;
    }
    if (!has_java_gradle_surface(project_root)) {
        print_error(
            err, command_error::unsupported_by_manifest,
            "project does not define a java/ Gradle test surface"
        );
        return command_error::unsupported_by_manifest;
    }

    const tool_status java_tool = probe_tool("java");
    const tool_status javac_tool = probe_tool("javac");
    const gradle_command gradle_tool = resolve_gradle_command(project_root);
    if (!java_tool.available || !javac_tool.available
        || !gradle_tool.available) {
        print_error(
            err, command_error::missing_local_tooling,
            "java checks require java, javac, and gradle (or java/gradlew)"
        );
        return command_error::missing_local_tooling;
    }

    const std::vector<artifact_ref> shared_library_refs
        = artifact_refs_for_kind(manifest_value, "shared_lib");
    if (shared_library_refs.empty()) {
        print_error(
            err, command_error::unsupported_by_manifest,
            "java checks require at least one shared library artifact"
        );
        return command_error::unsupported_by_manifest;
    }

    command_error status = run_configure_build_tree(
        project_root, manifest_value, "debug", false, false, false, err
    );
    if (status != command_error::ok) {
        return status;
    }
    status = build_artifacts(project_root, "debug", shared_library_refs, err);
    if (status != command_error::ok) {
        return status;
    }

    if (run_command(
            {
                gradle_tool.path,
                "test",
                "--no-daemon",
                "--console=plain",
                std::string("-PnativeLibraryPath=")
                    + local_build_dir(project_root, "debug").string(),
            },
            java_project_dir(project_root)
        )
        != 0) {
        print_error(err, command_error::task_failed, "java tests failed");
        return command_error::task_failed;
    }

    out << "java tests passed\n";
    return command_error::ok;
}

command_error run_check_tidy(
    const fs::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    std::ostream& out, std::ostream& err
) {
    const std::optional<resolved_artifact> resolved
        = resolve_artifact(manifest_value, requested_artifact);
    if (requested_artifact && !resolved) {
        print_error(
            err, command_error::invalid_request,
            "unknown artifact request: "
                + format_artifact_ref(*requested_artifact)
        );
        return command_error::invalid_request;
    }
    ensure_local_artifacts(project_root, false, true, false);
    const bool include_benchmarks = resolved.has_value()
        && component_is_benchmark_only(*resolved->component_value);
    if (probe_tool("clang-tidy").available) {
        const command_error status = run_configure_build_tree(
            project_root, manifest_value, "debug",
            has_tests_enabled(manifest_value), false, include_benchmarks, err
        );
        if (status != command_error::ok) {
            return status;
        }
    }
    const tidy_check_report report = run_tidy_check(
        manifest_value, project_root, requested_artifact, true,
        include_benchmarks, "debug"
    );
    const fs::path report_path = local_report_dir(project_root) / "tidy.json";
    std::string error_message;
    if (!write_text_file(
            report_path, to_json(report).dump(2) + "\n", &error_message
        )) {
        print_error(err, command_error::task_failed, error_message);
        return command_error::task_failed;
    }
    out << "tidy report: "
        << report_path.lexically_relative(project_root).generic_string()
        << "\n";
    if (!report.clang_tidy_used) {
        const auto failure = report.clang_tidy_available
            ? command_error::task_failed
            : command_error::missing_local_tooling;
        print_error(
            err, failure,
            "required tidy check did not run: " + report.clang_tidy_skip_reason
        );
        return failure;
    }
    if (report.clang_tidy_exit_code != 0) {
        for (const auto& line : report.clang_tidy_output)
            err << line << "\n";
        print_error(
            err, command_error::task_failed, "clang-tidy reported diagnostics"
        );
        return command_error::task_failed;
    }
    if (!report.analysis.errors.empty() || report.analysis.total_errors > 0
        || report.analysis.total_warnings > 0) {
        for (const auto& message : report.analysis.errors)
            err << message << "\n";
        for (const auto& source : report.analysis.sources)
            for (const auto& diagnostic : source.diagnostics)
                err << diagnostic << "\n";
        print_error(
            err, command_error::task_failed,
            "Clang analysis reported diagnostics"
        );
        return command_error::task_failed;
    }
    return command_error::ok;
}

command_error run_check_format(
    const fs::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact, std::ostream& out,
    std::ostream& err
) {
    return run_format_files(
        project_root, manifest_value, requested_artifact, false, out, err
    );
}

command_error run_check_doxy(
    const fs::path& project_root, std::ostream& out, std::ostream& err
) {
    const tool_status doxygen_tool = probe_tool("doxygen");
    if (!doxygen_tool.available) {
        print_error(
            err, command_error::missing_local_tooling,
            "doxygen is not available"
        );
        return command_error::missing_local_tooling;
    }
    try {
        ensure_local_artifacts(project_root, false, false, true);
    } catch (const template_render_error& error) {
        print_error(err, command_error::task_failed, error.what());
        return command_error::task_failed;
    }
    if (run_command({ doxygen_tool.path, "Doxyfile" }, project_root) != 0) {
        print_error(err, command_error::task_failed, "doxygen failed");
        return command_error::task_failed;
    }
    out << "doxygen generated in .ecosystem/doxygen\n";
    return command_error::ok;
}

command_error run_check_sphinx(
    const fs::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    const std::optional<std::string>& sphinx_theme, std::ostream& out,
    std::ostream& err
) {
    if (requested_artifact.has_value()) {
        print_error(
            err, command_error::invalid_request,
            "sphinx check does not support artifact filters"
        );
        return command_error::invalid_request;
    }
    if (!project_has_docs_surface(project_root)) {
        print_error(
            err, command_error::unsupported_by_manifest,
            "project does not define docs/index.md or docs/index.rst for "
            "sphinx"
        );
        return command_error::unsupported_by_manifest;
    }

    const tool_status sphinx_tool = probe_tool("sphinx-build");
    if (!sphinx_tool.available) {
        print_error(
            err, command_error::missing_local_tooling,
            "sphinx-build is not available"
        );
        return command_error::missing_local_tooling;
    }

    std::string error_message;
    if (!write_local_sphinx_conf(project_root, manifest_value.id, &error_message)) {
        print_error(err, command_error::task_failed, error_message);
        return command_error::task_failed;
    }

    const fs::path output_dir = local_sphinx_dir(project_root) / "html";
    std::error_code fs_error;
    fs::create_directories(output_dir, fs_error);
    if (fs_error) {
        print_error(err, command_error::task_failed, fs_error.message());
        return command_error::task_failed;
    }

    if (run_command(
            {
                sphinx_tool.path,
                "-b",
                "html",
                "-c",
                local_sphinx_dir(project_root).string(),
                (project_root / "docs").string(),
                output_dir.string(),
            },
            project_root, sphinx_environment(sphinx_theme)
        )
        != 0) {
        print_error(err, command_error::task_failed, "sphinx failed");
        return command_error::task_failed;
    }

    out << "sphinx generated in .ecosystem/sphinx/html\n";
    return command_error::ok;
}

command_error run_check_repo(
    const fs::path& project_root, const manifest& manifest_value,
    std::ostream& out, std::ostream& err
) {
    const string_list issues = forbidden_repository_entries(project_root);
    if (!issues.empty()) {
        err << "forbidden repository entries:\n";
        for (const std::string& issue : issues) {
            err << "  " << issue << "\n";
        }
        print_error(
            err, command_error::task_failed,
            "managed repositories must not commit legacy scripts, "
            "Makefiles, or extra CMake scaffolding"
        );
        return command_error::task_failed;
    }

    const string_list drift = tracked_surface_drift(project_root, manifest_value);
    if (!drift.empty()) {
        err << "tracked surfaces out of sync:\n";
        for (const std::string& issue : drift) {
            err << "  " << issue << "\n";
        }
        print_error(
            err, command_error::task_failed,
            "tracked surfaces are out of sync; run `marx sync`"
        );
        return command_error::task_failed;
    }
    out << "repository check passed\n";
    return command_error::ok;
}

command_error run_check(
    const fs::path& project_root, const manifest& manifest_value,
    const std::string& profile,
    const std::optional<artifact_ref>& requested_artifact,
    const std::optional<std::string>& sphinx_theme, std::ostream& out,
    std::ostream& err
) {
    if (!contains_string(known_check_profiles, profile)) {
        print_error(
            err, command_error::invalid_request,
            "unknown check profile: " + profile
        );
        return command_error::invalid_request;
    }
    if (sphinx_theme.has_value() && profile != "sphinx") {
        print_error(
            err, command_error::invalid_request,
            "--theme is only supported for check sphinx"
        );
        return command_error::invalid_request;
    }
    if (!supports_check_profile(manifest_value, profile)) {
        print_error(
            err, command_error::unsupported_by_manifest,
            "manifest does not support check profile " + profile
        );
        return command_error::unsupported_by_manifest;
    }

    if (profile == "tests") {
        return run_check_tests(
            project_root, manifest_value, requested_artifact, out, err
        );
    }
    if (profile == "coverage") {
        return run_check_coverage(
            project_root, manifest_value, requested_artifact, out, err
        );
    }
    if (profile == "leaks") {
        return run_check_leaks(
            project_root, manifest_value, requested_artifact, out, err
        );
    }
    if (profile == "java") {
        return run_check_java(
            project_root, manifest_value, requested_artifact, out, err
        );
    }
    if (profile == "tidy") {
        return run_check_tidy(
            project_root, manifest_value, requested_artifact, out, err
        );
    }
    if (profile == "format") {
        return run_check_format(
            project_root, manifest_value, requested_artifact, out, err
        );
    }
    if (profile == "naming" || profile == "style") {
        return run_check_personal(
            project_root, manifest_value, requested_artifact, profile, out, err
        );
    }
    if (profile == "repo") {
        return run_check_repo(project_root, manifest_value, out, err);
    }
    if (profile == "doxy") {
        return run_check_doxy(project_root, out, err);
    }
    if (profile == "sphinx") {
        return run_check_sphinx(
            project_root, manifest_value, requested_artifact, sphinx_theme,
            out, err
        );
    }

    command_error status
        = run_check_repo(project_root, manifest_value, out, err);
    if (status != command_error::ok) {
        return status;
    }
    if (has_tests_enabled(manifest_value)) {
        status = run_check_tests(
            project_root, manifest_value, requested_artifact, out, err
        );
        if (status != command_error::ok) {
            return status;
        }
    }
    if (!requested_artifact.has_value()
        && project_supports_java_check(project_root, manifest_value)) {
        status
            = run_check_java(project_root, manifest_value, std::nullopt, out, err);
        if (status != command_error::ok) {
            return status;
        }
    }
    status = run_check_tidy(
        project_root, manifest_value, requested_artifact, out, err
    );
    if (status != command_error::ok) {
        return status;
    }
    status = run_check_format(
        project_root, manifest_value, requested_artifact, out, err
    );
    if (status != command_error::ok) {
        return status;
    }
    out << "ci checks passed\n";
    return command_error::ok;
}

command_error run_workspace_check(
    const workspace_context& workspace, const std::string& profile,
    const workspace_scope& scope,
    const std::optional<std::string>& sphinx_theme, std::ostream& out,
    std::ostream& err
) {
    const command_error validity = validate_workspace_scope(workspace, scope, err);
    if (validity != command_error::ok) {
        return validity;
    }

    if (!contains_string(known_check_profiles, profile)) {
        print_error(
            err, command_error::invalid_request,
            "unknown check profile: " + profile
        );
        return command_error::invalid_request;
    }
    if (workspace_artifact_filter_count(scope) > 1U
        && !check_profile_supports_multi_artifact_filters(profile)) {
        print_error(
            err, command_error::invalid_request,
            "check " + profile
                + " supports at most one workspace artifact filter"
        );
        return command_error::invalid_request;
    }

    const bool explicit_project_selection = !scope.projects.empty();
    command_error status = command_error::ok;
    for (const workspace_project* project :
         selected_workspace_projects(workspace, scope)) {
        const std::vector<artifact_ref> requested_artifacts
            = workspace_artifacts_for_project(scope, project);
        if (!explicit_project_selection && requested_artifacts.empty()
            && !supports_check_profile(*project->manifest_value, profile)) {
            out << project->identity() << ": skipped (profile unsupported)\n";
            continue;
        }

        std::ostringstream project_out;
        std::ostringstream project_err;
        if (requested_artifacts.empty()) {
            status = combine_status(
                status,
                command_support::run_check(
                    project->root, *project->manifest_value, profile,
                    std::nullopt, sphinx_theme, project_out, project_err
                )
            );
        } else {
            for (const artifact_ref& requested_artifact : requested_artifacts) {
                status = combine_status(
                    status,
                    command_support::run_check(
                        project->root, *project->manifest_value, profile,
                        requested_artifact, sphinx_theme, project_out,
                        project_err
                    )
                );
                ensure_stream_trailing_newline(&project_out);
                ensure_stream_trailing_newline(&project_err);
            }
        }
        emit_workspace_project_output(
            workspace, project, project_out, project_err, out, err
        );
    }
    return status;
}

}  // namespace ecosystem::command_support

namespace ecosystem {

command_error run_check(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::string& profile,
    const std::optional<artifact_ref>& requested_artifact,
    const std::optional<std::string>& sphinx_theme, std::ostream& out,
    std::ostream& err
) {
    return command_support::run_check(
        project_root, manifest_value, profile, requested_artifact,
        sphinx_theme, out, err
    );
}

command_error run_workspace_check(
    const workspace_context& workspace, const std::string& profile,
    const workspace_scope& scope,
    const std::optional<std::string>& sphinx_theme, std::ostream& out,
    std::ostream& err
) {
    return command_support::run_workspace_check(
        workspace, profile, scope, sphinx_theme, out, err
    );
}

}  // namespace ecosystem
