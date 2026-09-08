#include "workspace/write_commands.hpp"

#include "command_internal.hpp"

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace ecosystem {

namespace write_commands_support {

bool load_mutable_manifest(
    const std::filesystem::path& project_root, manifest* manifest_value,
    std::ostream& err
) {
    int load_status = 0;
    *manifest_value = command_support::load_current_manifest(
        project_root, err, &load_status
    );
    return load_status == 0;
}

command_error emit_mutation_report(
    const std::filesystem::path& project_root, const std::string& actor_name,
    manifest* manifest_value, const mutation_report& report, std::ostream& out,
    std::ostream& err
) {
    if (!report.errors.empty()) {
        for (const std::string& message : report.errors) {
            command_support::print_error(
                err, command_error::invalid_request, message
            );
        }
        return command_error::invalid_request;
    }

    if (report.changed_manifest) {
        const string_list validation_errors = validate_manifest(*manifest_value);
        if (!validation_errors.empty()) {
            for (const std::string& message : validation_errors) {
                command_support::print_error(
                    err, command_error::invalid_request, message
                );
            }
            return command_error::invalid_request;
        }

        std::string error_message;
        if (!save_manifest(
                project_root / "manifest.json", *manifest_value, &error_message
            )) {
            command_support::print_error(
                err, command_error::task_failed, error_message
            );
            return command_error::task_failed;
        }
        out << "updated manifest.json\n";
    }

    for (const std::filesystem::path& written_file : report.written_files) {
        out << "wrote "
            << written_file.lexically_relative(project_root).generic_string()
            << "\n";
    }

    if (report.changed_manifest) {
        out << "next: " << actor_name << " sync\n";
    }
    return command_error::ok;
}

}  // namespace write_commands_support

command_error run_sync(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    std::ostream& out, std::ostream& err
) {
    const sync_report report = sync_project(project_root, manifest_value);
    if (!report.errors.empty()) {
        for (const std::string& message : report.errors) {
            command_support::print_error(
                err, command_error::task_failed, message
            );
        }
        return command_error::task_failed;
    }
    for (const std::filesystem::path& written_file : report.written_files) {
        out << "wrote "
            << written_file.lexically_relative(project_root).generic_string()
            << "\n";
    }
    for (const std::filesystem::path& removed_file : report.removed_files) {
        out << "removed "
            << removed_file.lexically_relative(project_root).generic_string()
            << "\n";
    }
    return command_error::ok;
}

command_error run_workspace_build(
    const workspace_context& workspace, const std::string& profile,
    const workspace_scope& scope, std::ostream& out, std::ostream& err
) {
    const command_error validity
        = validate_workspace_scope(workspace, scope, err);
    if (validity != command_error::ok) {
        return validity;
    }

    if (!command_support::contains_string(
            command_support::known_build_profiles, profile
        )) {
        command_support::print_error(
            err, command_error::invalid_request,
            "unknown build profile: " + profile
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
            && !supports_build_profile(*project->manifest_value, profile)) {
            out << project->identity() << ": skipped (profile unsupported)\n";
            continue;
        }

        std::ostringstream project_out;
        std::ostringstream project_err;
        if (requested_artifacts.empty()) {
            status = command_support::combine_status(
                status,
                run_build(
                    project->root, *project->manifest_value, profile,
                    std::nullopt, project_out, project_err
                )
            );
        } else {
            for (const artifact_ref& requested_artifact : requested_artifacts) {
                status = command_support::combine_status(
                    status,
                    run_build(
                        project->root, *project->manifest_value, profile,
                        requested_artifact, project_out, project_err
                    )
                );
                command_support::ensure_stream_trailing_newline(&project_out);
                command_support::ensure_stream_trailing_newline(&project_err);
            }
        }
        command_support::emit_workspace_project_output(
            workspace, project, project_out, project_err, out, err
        );
    }
    return status;
}

command_error run_workspace_prerelease(
    const workspace_context& workspace, const workspace_scope& scope,
    const std::optional<std::string>& version_base,
    const prerelease_signing_options& signing_options, std::ostream& out,
    std::ostream& err
) {
    const command_error validity
        = validate_workspace_scope(workspace, scope, err);
    if (validity != command_error::ok) {
        return validity;
    }

    command_error status = command_error::ok;
    for (const workspace_project* project :
         selected_workspace_projects(workspace, scope)) {
        const std::vector<artifact_ref> requested_artifacts
            = workspace_artifacts_for_project(scope, project);
        std::ostringstream project_out;
        std::ostringstream project_err;
        if (requested_artifacts.empty()) {
            status = command_support::combine_status(
                status,
                run_prerelease(
                    project->root, *project->manifest_value, std::nullopt,
                    version_base, signing_options, project_out, project_err
                )
            );
        } else {
            for (const artifact_ref& requested_artifact : requested_artifacts) {
                status = command_support::combine_status(
                    status,
                    run_prerelease(
                        project->root, *project->manifest_value,
                        requested_artifact, version_base, signing_options,
                        project_out, project_err
                    )
                );
                command_support::ensure_stream_trailing_newline(&project_out);
                command_support::ensure_stream_trailing_newline(&project_err);
            }
        }
        command_support::emit_workspace_project_output(
            workspace, project, project_out, project_err, out, err
        );
    }
    return status;
}

command_error run_workspace_benchmark(
    const workspace_context& workspace, const workspace_scope& scope,
    const string_list& passthrough_args, std::ostream& out, std::ostream& err
) {
    const command_error validity
        = validate_workspace_scope(workspace, scope, err);
    if (validity != command_error::ok) {
        return validity;
    }

    const bool explicit_project_selection = !scope.projects.empty();
    command_error status = command_error::ok;
    for (const workspace_project* project :
         selected_workspace_projects(workspace, scope)) {
        const std::vector<artifact_ref> requested_artifacts
            = workspace_artifacts_for_project(scope, project);
        if (!explicit_project_selection && requested_artifacts.empty()
            && !has_benchmarks_enabled(*project->manifest_value)) {
            out << project->identity()
                << ": skipped (no benchmarks declared)\n";
            continue;
        }

        std::ostringstream project_out;
        std::ostringstream project_err;
        if (requested_artifacts.empty()) {
            status = command_support::combine_status(
                status,
                run_benchmark(
                    project->root, *project->manifest_value, std::nullopt,
                    passthrough_args, project_out, project_err
                )
            );
        } else {
            for (const artifact_ref& requested_artifact : requested_artifacts) {
                status = command_support::combine_status(
                    status,
                    run_benchmark(
                        project->root, *project->manifest_value,
                        requested_artifact, passthrough_args, project_out,
                        project_err
                    )
                );
                command_support::ensure_stream_trailing_newline(&project_out);
                command_support::ensure_stream_trailing_newline(&project_err);
            }
        }
        command_support::emit_workspace_project_output(
            workspace, project, project_out, project_err, out, err
        );
    }
    return status;
}

command_error run_workspace_run(
    const workspace_context& workspace, const std::string& profile,
    const workspace_scope& scope,
    const std::optional<std::string>& android_mode,
    const string_list& passthrough_args, std::ostream& out, std::ostream& err
) {
    const command_error validity
        = validate_workspace_scope(workspace, scope, err);
    if (validity != command_error::ok) {
        return validity;
    }

    if (!command_support::contains_string(
            command_support::known_build_profiles, profile
        )) {
        command_support::print_error(
            err, command_error::invalid_request,
            "unknown build profile: " + profile
        );
        return command_error::invalid_request;
    }
    if (workspace_artifact_filter_count(scope) > 1U) {
        command_support::print_error(
            err, command_error::invalid_request,
            "run supports at most one workspace artifact filter"
        );
        return command_error::invalid_request;
    }

    const std::vector<const workspace_project*> selected
        = selected_workspace_projects(workspace, scope);
    if (selected.size() != 1U) {
        command_support::print_error(
            err, command_error::invalid_request,
            "run requires exactly one selected workspace project"
        );
        return command_error::invalid_request;
    }

    const workspace_project* project = selected.front();
    out << "== " << project->identity() << " ("
        << relative_workspace_path(workspace, project->root) << ") ==\n";
    out.flush();
    return run_run(
        project->root, *project->manifest_value, profile,
        workspace_single_artifact_for_project(scope, project), android_mode,
        passthrough_args, out, err
    );
}

command_error run_workspace_sync(
    const workspace_context& workspace, const workspace_scope& scope,
    std::ostream& out, std::ostream& err
) {
    const command_error validity
        = validate_workspace_scope(workspace, scope, err);
    if (validity != command_error::ok) {
        return validity;
    }

    command_error status = command_error::ok;
    for (const workspace_project* project :
         selected_workspace_projects(workspace, scope)) {
        const sync_report report
            = sync_project(project->root, *project->manifest_value);
        if (!report.errors.empty()) {
            status = command_support::combine_status(
                status, command_error::task_failed
            );
            for (const std::string& message : report.errors) {
                command_support::print_error(
                    err, command_error::task_failed,
                    project->identity() + ": " + message
                );
            }
            continue;
        }
        for (const std::filesystem::path& written_file : report.written_files) {
            out << project->identity() << ": wrote "
                << written_file.lexically_relative(workspace.root)
                       .generic_string()
                << "\n";
        }
        for (const std::filesystem::path& removed_file : report.removed_files) {
            out << project->identity() << ": removed "
                << removed_file.lexically_relative(workspace.root)
                       .generic_string()
                << "\n";
        }
    }
    return status;
}

command_error run_add_component(
    const std::filesystem::path& project_root, const std::string& actor_name,
    const std::string& component_id, const add_component_options& options,
    std::ostream& out, std::ostream& err
) {
    manifest manifest_value;
    if (!write_commands_support::load_mutable_manifest(
            project_root, &manifest_value, err
        )) {
        return command_error::invalid_request;
    }

    const mutation_report report
        = add_component(project_root, &manifest_value, component_id, options);
    return write_commands_support::emit_mutation_report(
        project_root, actor_name, &manifest_value, report, out, err
    );
}

command_error run_add_module(
    const std::filesystem::path& project_root, const std::string& actor_name,
    const std::string& component_id, const std::string& module_path,
    std::ostream& out, std::ostream& err
) {
    manifest manifest_value;
    if (!write_commands_support::load_mutable_manifest(
            project_root, &manifest_value, err
        )) {
        return command_error::invalid_request;
    }

    const mutation_report report = add_module(
        project_root, &manifest_value, component_id, module_path
    );
    return write_commands_support::emit_mutation_report(
        project_root, actor_name, &manifest_value, report, out, err
    );
}

command_error run_add_files(
    const std::filesystem::path& project_root, const std::string& actor_name,
    const std::string& component_id, const std::string& module_path,
    std::ostream& out, std::ostream& err
) {
    manifest manifest_value;
    if (!write_commands_support::load_mutable_manifest(
            project_root, &manifest_value, err
        )) {
        return command_error::invalid_request;
    }

    const mutation_report report
        = add_files(project_root, manifest_value, component_id, module_path);
    return write_commands_support::emit_mutation_report(
        project_root, actor_name, &manifest_value, report, out, err
    );
}

command_error run_add_file_unit(
    const std::filesystem::path& project_root, const std::string& actor_name,
    const std::string& component_id, const std::string& unit_id,
    const std::string& kind, std::ostream& out, std::ostream& err
) {
    manifest manifest_value;
    if (!write_commands_support::load_mutable_manifest(
            project_root, &manifest_value, err
        )) {
        return command_error::invalid_request;
    }

    const mutation_report report = add_file_unit(
        project_root, &manifest_value, component_id, unit_id, kind
    );
    return write_commands_support::emit_mutation_report(
        project_root, actor_name, &manifest_value, report, out, err
    );
}

command_error run_set_facade_entry(
    const std::filesystem::path& project_root, const std::string& actor_name,
    const artifact_ref& entry_ref, std::ostream& out, std::ostream& err
) {
    manifest manifest_value;
    if (!write_commands_support::load_mutable_manifest(
            project_root, &manifest_value, err
        )) {
        return command_error::invalid_request;
    }

    const mutation_report report = set_facade_entry(&manifest_value, entry_ref);
    return write_commands_support::emit_mutation_report(
        project_root, actor_name, &manifest_value, report, out, err
    );
}

command_error run_build(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::string& profile,
    const std::optional<artifact_ref>& requested_artifact,
    std::ostream& out, std::ostream& err
) {
    if (!command_support::contains_string(
            command_support::known_build_profiles, profile
        )) {
        command_support::print_error(
            err, command_error::invalid_request,
            "unknown build profile: " + profile
        );
        return command_error::invalid_request;
    }
    if (!supports_build_profile(manifest_value, profile)) {
        command_support::print_error(
            err, command_error::unsupported_by_manifest,
            "manifest does not support build profile " + profile
        );
        err << "valid build profiles:";
        for (const std::string& supported :
             supported_build_profiles(manifest_value)) {
            err << " " << supported;
        }
        err << "\n";
        return command_error::unsupported_by_manifest;
    }

    const std::optional<resolved_artifact> resolved
        = resolve_artifact(manifest_value, requested_artifact);
    if (!resolved.has_value()) {
        command_support::print_error(
            err, command_error::invalid_request, "unknown artifact request"
        );
        return command_error::invalid_request;
    }

    const bool with_tests = component_is_test_only(*resolved->component_value);
    const bool with_benchmarks
        = component_is_benchmark_only(*resolved->component_value);
    command_error status = command_support::run_configure_build_tree(
        project_root, manifest_value, profile, with_tests, false,
        with_benchmarks, err
    );
    if (status != command_error::ok) {
        return status;
    }
    status = command_support::build_target(
        project_root, profile, cmake_target_name(resolved->ref), err
    );
    if (status != command_error::ok) {
        return status;
    }

    out << "built " << format_artifact_ref(resolved->ref) << " in "
        << local_build_dir(project_root, profile)
               .lexically_relative(project_root)
               .generic_string()
        << "\n";
    return command_error::ok;
}

command_error run_prerelease(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    const std::optional<std::string>& version_base,
    const prerelease_signing_options& signing_options, std::ostream& out,
    std::ostream& err
) {
    const std::optional<resolved_artifact> resolved
        = resolve_artifact(manifest_value, requested_artifact);
    if (!resolved.has_value()) {
        command_support::print_error(
            err, command_error::invalid_request, "unknown artifact request"
        );
        return command_error::invalid_request;
    }
    if (resolved->artifact_value->kind == "interface_lib") {
        command_support::print_error(
            err, command_error::invalid_request,
            "prerelease packaging does not support interface libraries"
        );
        return command_error::invalid_request;
    }

    const bool with_tests = component_is_test_only(*resolved->component_value);
    const bool with_benchmarks
        = component_is_benchmark_only(*resolved->component_value);
    command_error status = command_support::run_configure_build_tree(
        project_root, manifest_value, "release", with_tests, false,
        with_benchmarks, err
    );
    if (status != command_error::ok) {
        return status;
    }
    status = command_support::build_target(
        project_root, "release", cmake_target_name(resolved->ref), err
    );
    if (status != command_error::ok) {
        return status;
    }

    const std::string output_name = artifact_output_name(
        manifest_value, *resolved->component_value, *resolved->artifact_value
    );
    const std::optional<std::filesystem::path> built_path = artifact_output_path(
        local_build_dir(project_root, "release"), *resolved->artifact_value,
        output_name
    );
    if (!built_path.has_value()) {
        command_support::print_error(
            err, command_error::task_failed, "built artifact output is missing"
        );
        return command_error::task_failed;
    }

    prerelease_version version;
    prerelease_artifacts artifacts;
    for (const artifact_ref& companion :
         distribution_artifacts(manifest_value, resolved->ref)) {
        if (format_artifact_ref(companion)
            == format_artifact_ref(resolved->ref)) {
            continue;
        }
        status = command_support::build_target(
            project_root, "release", cmake_target_name(companion), err
        );
        if (status != command_error::ok) {
            return status;
        }
    }
    std::string error_message;
    status = create_prerelease_packages(
        project_root, manifest_value, *resolved, *built_path, version_base,
        signing_options, &version, &artifacts, &error_message
    );
    if (status != command_error::ok) {
        if (!error_message.empty()) {
            command_support::print_error(err, status, error_message);
        }
        return status;
    }

    out << "prerelease " << format_artifact_ref(resolved->ref)
        << " version " << version.logical_version << "\n";
    out << "debian repo: "
        << artifacts.deb_repo_dir.lexically_relative(project_root)
               .generic_string()
        << "\n";
    out << "debian package: "
        << artifacts.deb_package_path.lexically_relative(project_root)
               .generic_string()
        << "\n";
    out << "pacman repo: "
        << artifacts.pacman_repo_dir.lexically_relative(project_root)
               .generic_string()
        << "\n";
    out << "pacman package: "
        << artifacts.pacman_package_path.lexically_relative(project_root)
               .generic_string()
        << "\n";
    if (signing_options.sign) {
        out << "signed prerelease metadata\n";
    }
    return command_error::ok;
}

command_error run_benchmark(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    const string_list& passthrough_args, std::ostream& out, std::ostream& err
) {
    resolved_artifact resolved;
    command_error status = command_support::resolve_benchmark_artifact(
        manifest_value, requested_artifact, &resolved, err
    );
    if (status != command_error::ok) {
        return status;
    }

    status = command_support::run_configure_build_tree(
        project_root, manifest_value, "release", false, false, true, err
    );
    if (status != command_error::ok) {
        return status;
    }
    status = command_support::build_target(
        project_root, "release", cmake_target_name(resolved.ref), err
    );
    if (status != command_error::ok) {
        return status;
    }

    const std::optional<std::filesystem::path> binary_path
        = command_support::runnable_binary_path(
            project_root, manifest_value, "release", resolved
        );
    if (!binary_path.has_value()) {
        command_support::print_error(
            err, command_error::task_failed,
            "built benchmark executable is missing"
        );
        return command_error::task_failed;
    }

    const std::filesystem::path output_dir
        = local_benchmark_dir(project_root) / benchmark_output_stem(resolved.ref);
    std::error_code fs_error;
    std::filesystem::create_directories(output_dir, fs_error);
    if (fs_error) {
        command_support::print_error(
            err, command_error::task_failed, fs_error.message()
        );
        return command_error::task_failed;
    }

    std::vector<std::string> command {
        binary_path->string(),
        "--benchmark_color=false",
    };
    command.insert(
        command.end(), passthrough_args.begin(), passthrough_args.end()
    );
    const captured_command benchmark_result = capture_command_result(
        command, local_build_dir(project_root, "release")
    );

    const std::filesystem::path log_path = output_dir / "bench_log.txt";
    std::string error_message;
    if (!write_text_file(log_path, benchmark_result.output, &error_message)) {
        command_support::print_error(
            err, command_error::task_failed, error_message
        );
        return command_error::task_failed;
    }
    if (benchmark_result.exit_code != 0) {
        command_support::print_error(
            err, command_error::task_failed, "benchmark executable failed"
        );
        return command_error::task_failed;
    }

    out << "benchmarked " << format_artifact_ref(resolved.ref) << "\n";
    out << "benchmark log: "
        << log_path.lexically_relative(project_root).generic_string() << "\n";

    benchmark_summary summary;
    parse_benchmark_log(benchmark_result.output, &summary, &error_message);
    if (summary.series.empty()) {
        out << "benchmark plot: skipped (no FLOPs series recognized)\n";
        return command_error::ok;
    }

    const std::filesystem::path summary_path = output_dir / "summary.json";
    if (!write_text_file(
            summary_path, to_json(summary).dump(2) + "\n", &error_message
        )) {
        command_support::print_error(
            err, command_error::task_failed, error_message
        );
        return command_error::task_failed;
    }

    const std::filesystem::path plot_path = output_dir / "bench_plot.svg";
    const std::string title
        = format_artifact_ref(resolved.ref) + " GFLOPs/s vs n";
    const std::string plot_contents
        = render_benchmark_svg(summary, title, &error_message);
    if (!error_message.empty()) {
        command_support::print_error(
            err, command_error::task_failed, error_message
        );
        return command_error::task_failed;
    }
    if (!write_text_file(plot_path, plot_contents, &error_message)) {
        command_support::print_error(
            err, command_error::task_failed, error_message
        );
        return command_error::task_failed;
    }

    out << "benchmark summary: "
        << summary_path.lexically_relative(project_root).generic_string()
        << "\n";
    out << "benchmark plot: "
        << plot_path.lexically_relative(project_root).generic_string() << "\n";
    return command_error::ok;
}

command_error run_run(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::string& profile,
    const std::optional<artifact_ref>& requested_artifact,
    const std::optional<std::string>& android_mode,
    const string_list& passthrough_args, std::ostream& out, std::ostream& err
) {
    if (!command_support::contains_string(
            command_support::known_build_profiles, profile
        )) {
        command_support::print_error(
            err, command_error::invalid_request,
            "unknown build profile: " + profile
        );
        return command_error::invalid_request;
    }
    if (!supports_build_profile(manifest_value, profile)) {
        command_support::print_error(
            err, command_error::unsupported_by_manifest,
            "manifest does not support build profile " + profile
        );
        err << "valid build profiles:";
        for (const std::string& supported :
             supported_build_profiles(manifest_value)) {
            err << " " << supported;
        }
        err << "\n";
        return command_error::unsupported_by_manifest;
    }

    const std::optional<resolved_artifact> resolved
        = resolve_artifact(manifest_value, requested_artifact);
    if (!resolved.has_value()) {
        command_support::print_error(
            err, command_error::invalid_request, "unknown artifact request"
        );
        return command_error::invalid_request;
    }
    if (!command_support::is_runnable_artifact(*resolved->artifact_value)) {
        command_support::print_error(
            err, command_error::invalid_request,
            "requested artifact is not runnable"
        );
        return command_error::invalid_request;
    }
    if (profile == "android") {
        return command_support::run_android_artifact(
            project_root, manifest_value, *resolved, android_mode,
            passthrough_args, out, err
        );
    }
    if (android_mode.has_value()) {
        command_support::print_error(
            err, command_error::invalid_request,
            "--android-mode is only valid for marx run android"
        );
        return command_error::invalid_request;
    }

    const bool with_tests = component_is_test_only(*resolved->component_value);
    const bool with_benchmarks
        = component_is_benchmark_only(*resolved->component_value);
    command_error status = command_support::run_configure_build_tree(
        project_root, manifest_value, profile, with_tests, false,
        with_benchmarks, err
    );
    if (status != command_error::ok) {
        return status;
    }
    status = command_support::build_target(
        project_root, profile, cmake_target_name(resolved->ref), err
    );
    if (status != command_error::ok) {
        return status;
    }

    const std::optional<std::filesystem::path> binary_path
        = command_support::runnable_binary_path(
            project_root, manifest_value, profile, *resolved
        );
    if (!binary_path.has_value()) {
        command_support::print_error(
            err, command_error::task_failed,
            "built artifact is missing its executable output"
        );
        return command_error::task_failed;
    }

    status = command_support::run_runnable_binary(
        local_build_dir(project_root, profile), *binary_path,
        passthrough_args, err
    );
    if (status != command_error::ok) {
        return status;
    }

    out << "ran " << format_artifact_ref(resolved->ref) << " from "
        << binary_path->lexically_relative(project_root).generic_string()
        << "\n";
    return command_error::ok;
}

}  // namespace ecosystem
