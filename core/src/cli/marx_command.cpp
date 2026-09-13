#include "cli/marx_command.hpp"

#include "workspace/read_commands.hpp"
#include "workspace/write_commands.hpp"

namespace fs = std::filesystem;

namespace ecosystem::marx_command_support {

std::string expected_usage(const std::string& usage_tail) {
    return "expected: marx " + usage_tail;
}

bool is_engels_command(const std::string& command) {
    return command == "list" || command == "check" || command == "doctor"
        || command == "report";
}

void emit_manifest_report_errors(
    const manifest_report& report, std::ostream& err
) {
    for (const std::string& message : report.errors) {
        emit_command_error(err, command_error::invalid_request, message);
    }
}

std::string mutate_add_file_unit_usage() {
    return expected_usage(
        "mutate add file-unit <component-id> <module-id> --kind <kind>"
    );
}

std::string mutate_add_path_usage(const std::string& noun) {
    return expected_usage(
        "mutate add " + noun + " <component-id> <module-path>"
    );
}

std::string mutate_set_facade_entry_usage() {
    return expected_usage("mutate set facade-entry <component:artifact>");
}

int handle_mutate_command(
    const fs::path& project_root, const args_list& args, std::ostream& out,
    std::ostream& err
) {
    if (args.size() < 2U) {
        emit_command_error(
            err, command_error::invalid_request, "missing mutate subcommand"
        );
        return exit_code(command_error::invalid_request);
    }

    const args_list mutate_args(args.begin() + 1, args.end());
    if (mutate_args.size() < 2U) {
        emit_command_error(
            err, command_error::invalid_request, "missing mutate subcommand"
        );
        return exit_code(command_error::invalid_request);
    }

    if (mutate_args[0] == "add" && mutate_args[1] == "component") {
        std::string parse_error;
        const std::optional<add_component_options> options
            = parse_add_component_options("marx", mutate_args, &parse_error);
        if (!options.has_value()) {
            emit_command_error(
                err, command_error::invalid_request, parse_error
            );
            return exit_code(command_error::invalid_request);
        }
        return exit_code(run_add_component(
            project_root, "marx", mutate_args[2], *options, out, err
        ));
    }

    if (mutate_args[0] == "add" && mutate_args[1] == "module") {
        if (mutate_args.size() != 4U) {
            emit_command_error(
                err, command_error::invalid_request,
                mutate_add_path_usage("module")
            );
            return exit_code(command_error::invalid_request);
        }
        return exit_code(run_add_module(
            project_root, "marx", mutate_args[2], mutate_args[3], out, err
        ));
    }

    if (mutate_args[0] == "add" && mutate_args[1] == "files") {
        if (mutate_args.size() != 4U) {
            emit_command_error(
                err, command_error::invalid_request,
                mutate_add_path_usage("files")
            );
            return exit_code(command_error::invalid_request);
        }
        return exit_code(run_add_files(
            project_root, "marx", mutate_args[2], mutate_args[3], out, err
        ));
    }

    if (mutate_args[0] == "add" && mutate_args[1] == "file-unit") {
        if (mutate_args.size() != 6U || mutate_args[4] != "--kind") {
            emit_command_error(
                err, command_error::invalid_request,
                mutate_add_file_unit_usage()
            );
            return exit_code(command_error::invalid_request);
        }
        return exit_code(run_add_file_unit(
            project_root, "marx", mutate_args[2], mutate_args[3],
            mutate_args[5], out, err
        ));
    }

    if (mutate_args[0] == "set" && mutate_args[1] == "facade-entry") {
        if (mutate_args.size() != 3U) {
            emit_command_error(
                err, command_error::invalid_request,
                mutate_set_facade_entry_usage()
            );
            return exit_code(command_error::invalid_request);
        }
        const std::optional<artifact_ref> entry_ref
            = parse_artifact_ref(mutate_args[2]);
        if (!entry_ref.has_value()) {
            emit_command_error(
                err, command_error::invalid_request,
                "facade entry must use component:artifact form"
            );
            return exit_code(command_error::invalid_request);
        }
        return exit_code(
            run_set_facade_entry(project_root, "marx", *entry_ref, out, err)
        );
    }

    emit_command_error(
        err, command_error::invalid_request, "unsupported mutate operation"
    );
    return exit_code(command_error::invalid_request);
}

}  // namespace ecosystem::marx_command_support

namespace ecosystem {

int run_marx_command(
    const args_list& args, std::ostream& out, std::ostream& err
) {
    if (args.empty()) {
        emit_command_error(
            err, command_error::invalid_request, "missing marx command"
        );
        return exit_code(command_error::invalid_request);
    }

    if (marx_command_support::is_engels_command(args.front())) {
        emit_command_error(
            err,
            command_error::invalid_request,
            "command '" + args.front()
                + "' is available via engels; run `engels " + args.front()
                + "`"
        );
        return exit_code(command_error::invalid_request);
    }

    const fs::path project_root = fs::current_path();
    const manifest_report current_manifest_report
        = load_manifest(project_root / "manifest.json");
    const bool has_project_manifest = current_manifest_report.errors.empty()
        && current_manifest_report.value.has_value();
    const std::optional<workspace_context> workspace
        = (!has_project_manifest && !current_manifest_report.has_manifest)
        ? discover_workspace(project_root)
        : std::nullopt;
    const bool workspace_mode = workspace.has_value();

    if (workspace_mode && !workspace->errors.empty()) {
        emit_workspace_errors(*workspace, err);
        return exit_code(command_error::invalid_request);
    }

    if (args[0] == "sync") {
        if (workspace_mode) {
            workspace_scope scope;
            const command_error parse_status = parse_workspace_scope(
                *workspace, args_list(args.begin() + 1, args.end()), false,
                &scope, err
            );
            if (parse_status != command_error::ok) {
                return exit_code(parse_status);
            }
            return exit_code(run_workspace_sync(*workspace, scope, out, err));
        }
        if (args.size() != 1U) {
            emit_command_error(
                err, command_error::invalid_request,
                marx_command_support::expected_usage("sync")
            );
            return exit_code(command_error::invalid_request);
        }
        if (!has_project_manifest) {
            marx_command_support::emit_manifest_report_errors(
                current_manifest_report, err
            );
            return exit_code(command_error::invalid_request);
        }
        return exit_code(run_sync(project_root, *current_manifest_report.value, out, err));
    }

    if (args[0] == "format") {
        if (workspace_mode) {
            workspace_scope scope;
            const auto status = parse_workspace_scope(
                *workspace, args_list(args.begin() + 1, args.end()), true,
                &scope, err
            );
            if (status != command_error::ok)
                return exit_code(status);
            return exit_code(run_workspace_format(*workspace, scope, out, err));
        }
        if (!has_project_manifest) {
            marx_command_support::emit_manifest_report_errors(
                current_manifest_report, err
            );
            return exit_code(command_error::invalid_request);
        }
        const auto requested
            = args.size() == 2 ? parse_artifact_ref(args[1]) : std::nullopt;
        if (args.size() > 2 || (args.size() == 2 && !requested)) {
            emit_command_error(
                err, command_error::invalid_request,
                marx_command_support::expected_usage(
                    "format [component:artifact]"
                )
            );
            return exit_code(command_error::invalid_request);
        }
        return exit_code(run_format(
            project_root, *current_manifest_report.value, requested, out, err
        ));
    }

    if (args[0] == "mutate") {
        if (workspace_mode) {
            emit_command_error(
                err,
                command_error::invalid_request,
                "mutate requires a managed project directory, not a workspace "
                "root"
            );
            return exit_code(command_error::invalid_request);
        }
        if (!has_project_manifest) {
            marx_command_support::emit_manifest_report_errors(
                current_manifest_report, err
            );
            return exit_code(command_error::invalid_request);
        }
        return marx_command_support::handle_mutate_command(
            project_root, args, out, err
        );
    }

    if (!has_project_manifest && !workspace_mode) {
        marx_command_support::emit_manifest_report_errors(
            current_manifest_report, err
        );
        return exit_code(command_error::invalid_request);
    }

    if (args[0] == "build") {
        if (workspace_mode) {
            if (args.size() < 2U) {
                emit_command_error(
                    err,
                    command_error::invalid_request,
                    marx_command_support::expected_usage(
                        "build <profile> [--project <project>] "
                        "[--group <group>] "
                        "[component:artifact|project/component:artifact]"
                    )
                );
                return exit_code(command_error::invalid_request);
            }
            workspace_scope scope;
            const command_error parse_status = parse_workspace_scope(
                *workspace, args_list(args.begin() + 2, args.end()), true,
                &scope, err
            );
            if (parse_status != command_error::ok) {
                return exit_code(parse_status);
            }
            return exit_code(
                run_workspace_build(*workspace, args[1], scope, out, err)
            );
        }
        if (args.size() < 2U || args.size() > 3U) {
            emit_command_error(
                err,
                command_error::invalid_request,
                marx_command_support::expected_usage(
                    "build <profile> [component:artifact]"
                )
            );
            return exit_code(command_error::invalid_request);
        }
        const std::optional<artifact_ref> requested_artifact
            = args.size() == 3U ? parse_artifact_ref(args[2]) : std::nullopt;
        if (args.size() == 3U && !requested_artifact.has_value()) {
            emit_command_error(
                err,
                command_error::invalid_request,
                "artifact must use component:artifact form"
            );
            return exit_code(command_error::invalid_request);
        }
        return exit_code(run_build(
            project_root, *current_manifest_report.value, args[1],
            requested_artifact, out, err
        ));
    }

    if (args[0] == "benchmark") {
        std::string benchmark_parse_error;
        const std::optional<benchmark_request> request = parse_benchmark_request(
            "marx", args_list(args.begin() + 1, args.end()),
            &benchmark_parse_error
        );
        if (!request.has_value()) {
            emit_command_error(
                err, command_error::invalid_request, benchmark_parse_error
            );
            return exit_code(command_error::invalid_request);
        }
        if (workspace_mode) {
            workspace_scope scope;
            const command_error parse_status = parse_workspace_scope(
                *workspace, request->scope_args, true, &scope, err
            );
            if (parse_status != command_error::ok) {
                return exit_code(parse_status);
            }
            return exit_code(run_workspace_benchmark(
                *workspace, scope, request->passthrough_args, out, err
            ));
        }
        if (request->scope_args.size() > 1U) {
            emit_command_error(
                err,
                command_error::invalid_request,
                marx_command_support::expected_usage(
                    "benchmark [component:artifact] [-- <arg> ...]"
                )
            );
            return exit_code(command_error::invalid_request);
        }
        const std::optional<artifact_ref> requested_artifact
            = request->scope_args.empty()
            ? std::nullopt
            : parse_artifact_ref(request->scope_args.front());
        if (!request->scope_args.empty() && !requested_artifact.has_value()) {
            emit_command_error(
                err,
                command_error::invalid_request,
                "artifact must use component:artifact form"
            );
            return exit_code(command_error::invalid_request);
        }
        return exit_code(run_benchmark(
            project_root, *current_manifest_report.value, requested_artifact,
            request->passthrough_args, out, err
        ));
    }

    if (args[0] == "prerelease") {
        std::string prerelease_parse_error;
        const std::optional<prerelease_request> request
            = parse_prerelease_request(
                "marx", args_list(args.begin() + 1, args.end()),
                &prerelease_parse_error
            );
        if (!request.has_value()) {
            emit_command_error(
                err, command_error::invalid_request, prerelease_parse_error
            );
            return exit_code(command_error::invalid_request);
        }
        if (workspace_mode) {
            workspace_scope scope;
            const command_error parse_status = parse_workspace_scope(
                *workspace, request->scope_args, true, &scope, err
            );
            if (parse_status != command_error::ok) {
                return exit_code(parse_status);
            }
            return exit_code(run_workspace_prerelease(
                *workspace, scope, request->version_base,
                request->signing_options, out, err
            ));
        }
        if (request->scope_args.size() > 1U) {
            emit_command_error(
                err,
                command_error::invalid_request,
                marx_command_support::expected_usage(
                    "prerelease [component:artifact] [--version-base "
                    "<major.minor.patch>] [--sign] [--sign-key <key-id>]"
                )
            );
            return exit_code(command_error::invalid_request);
        }
        const std::optional<artifact_ref> requested_artifact
            = request->scope_args.empty()
            ? std::nullopt
            : parse_artifact_ref(request->scope_args.front());
        if (!request->scope_args.empty() && !requested_artifact.has_value()) {
            emit_command_error(
                err,
                command_error::invalid_request,
                "artifact must use component:artifact form"
            );
            return exit_code(command_error::invalid_request);
        }
        return exit_code(run_prerelease(
            project_root, *current_manifest_report.value, requested_artifact,
            request->version_base, request->signing_options, out, err
        ));
    }

    if (args[0] == "run") {
        if (args.size() < 2U) {
            emit_command_error(
                err,
                command_error::invalid_request,
                marx_command_support::expected_usage(
                    "run <profile> [component:artifact] [--android-mode "
                    "<auto|emulator|device>] [-- <arg> ...]"
                )
            );
            return exit_code(command_error::invalid_request);
        }

        std::string run_parse_error;
        const std::optional<run_request> request = parse_run_request(
            "marx", args_list(args.begin() + 2, args.end()), &run_parse_error
        );
        if (!request.has_value()) {
            emit_command_error(
                err, command_error::invalid_request, run_parse_error
            );
            return exit_code(command_error::invalid_request);
        }
        if (workspace_mode) {
            workspace_scope scope;
            const command_error parse_status = parse_workspace_scope(
                *workspace, request->command_args, true, &scope, err
            );
            if (parse_status != command_error::ok) {
                return exit_code(parse_status);
            }
            return exit_code(run_workspace_run(
                *workspace, args[1], scope, request->android_mode,
                request->passthrough_args, out, err
            ));
        }

        if (request->command_args.size() > 1U) {
            emit_command_error(
                err,
                command_error::invalid_request,
                marx_command_support::expected_usage(
                    "run <profile> [component:artifact] [--android-mode "
                    "<auto|emulator|device>] [-- <arg> ...]"
                )
            );
            return exit_code(command_error::invalid_request);
        }
        const std::optional<artifact_ref> requested_artifact
            = request->command_args.empty()
            ? std::nullopt
            : parse_artifact_ref(request->command_args.front());
        if (!request->command_args.empty() && !requested_artifact.has_value()) {
            emit_command_error(
                err,
                command_error::invalid_request,
                "artifact must use component:artifact form"
            );
            return exit_code(command_error::invalid_request);
        }
        return exit_code(run_run(
            project_root, *current_manifest_report.value, args[1],
            requested_artifact, request->android_mode,
            request->passthrough_args, out, err
        ));
    }

    emit_command_error(
        err, command_error::invalid_request, "unknown command: " + args[0]
    );
    return exit_code(command_error::invalid_request);
}

}  // namespace ecosystem
