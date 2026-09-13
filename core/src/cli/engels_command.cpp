#include "cli/engels_command.hpp"

#include "workspace/read_commands.hpp"

#include <algorithm>

namespace fs = std::filesystem;

namespace ecosystem::engels_command_support {

std::string expected_usage(const std::string& usage_tail) {
    return "expected: engels " + usage_tail;
}

bool is_marx_command(const std::string& command) {
    return command == "sync" || command == "format" || command == "mutate"
        || command == "build" || command == "benchmark" || command == "run"
        || command == "prerelease";
}

void emit_manifest_report_errors(
    const manifest_report& report, std::ostream& err
) {
    for (const std::string& message : report.errors) {
        emit_command_error(err, command_error::invalid_request, message);
    }
}

}  // namespace ecosystem::engels_command_support

namespace ecosystem {

int run_engels_command(
    const args_list& args, std::ostream& out, std::ostream& err
) {
    if (args.empty()) {
        emit_command_error(
            err, command_error::invalid_request, "missing engels command"
        );
        return exit_code(command_error::invalid_request);
    }

    if (engels_command_support::is_marx_command(args.front())) {
        emit_command_error(
            err,
            command_error::invalid_request,
            "command '" + args.front() + "' is available via marx; run `marx "
                + args.front() + "`"
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
    if (!has_project_manifest && !workspace_mode) {
        engels_command_support::emit_manifest_report_errors(
            current_manifest_report, err
        );
        return exit_code(command_error::invalid_request);
    }

    if (args[0] == "list") {
        const std::optional<std::string> target
            = args.size() > 1U ? std::make_optional(args[1]) : std::nullopt;
        const command_error status = workspace_mode
            ? run_workspace_list(*workspace, target, out)
            : run_list(*current_manifest_report.value, target, out);
        if (status == command_error::invalid_request) {
            emit_command_error(err, status, "unknown list target");
        }
        return exit_code(status);
    }

    if (args[0] == "check") {
        if (args.size() < 2U) {
            emit_command_error(
                err,
                command_error::invalid_request,
                workspace_mode
                    ? engels_command_support::expected_usage(
                          "check <profile> [--project <project>] "
                          "[--group <group>] "
                          "[component:artifact|project/component:artifact] "
                          "[--theme <theme>]"
                      )
                    : engels_command_support::expected_usage(
                          "check <profile> [component:artifact] "
                          "[--theme <theme>]"
                      )
            );
            return exit_code(command_error::invalid_request);
        }
        std::string check_parse_error;
        const std::optional<check_request> request = parse_check_request(
            "engels", args_list(args.begin() + 2, args.end()),
            &check_parse_error
        );
        if (!request.has_value()) {
            emit_command_error(
                err, command_error::invalid_request, check_parse_error
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
            return exit_code(run_workspace_check(
                *workspace, args[1], scope, request->sphinx_theme, out, err
            ));
        }
        if (request->scope_args.size() > 1U) {
            emit_command_error(
                err,
                command_error::invalid_request,
                engels_command_support::expected_usage(
                    "check <profile> [component:artifact] [--theme <theme>]"
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
        return exit_code(run_check(
            project_root, *current_manifest_report.value, args[1],
            requested_artifact, request->sphinx_theme, out, err
        ));
    }

    if (args[0] == "doctor") {
        if (workspace_mode) {
            std::optional<std::string> profile;
            workspace_scope scope;
            const command_error parse_status = parse_workspace_doctor_request(
                *workspace, args_list(args.begin() + 1, args.end()), &profile,
                &scope, err
            );
            if (parse_status != command_error::ok) {
                return exit_code(parse_status);
            }
            return exit_code(run_workspace_doctor(*workspace, profile, scope, out, err));
        }
        std::optional<std::string> profile;
        std::optional<artifact_ref> requested_artifact;
        for (std::size_t index = 1U; index < args.size(); ++index) {
            if (args[index] == "--profile") {
                if (index + 1U >= args.size()) {
                    emit_command_error(
                        err, command_error::invalid_request,
                        "--profile requires a value"
                    );
                    return exit_code(command_error::invalid_request);
                }
                profile = args[index + 1U];
                ++index;
                continue;
            }
            if (args[index].find(':') != std::string::npos) {
                requested_artifact = parse_artifact_ref(args[index]);
                if (!requested_artifact.has_value()) {
                    emit_command_error(
                        err,
                        command_error::invalid_request,
                        "artifact must use component:artifact form"
                    );
                    return exit_code(command_error::invalid_request);
                }
                continue;
            }
            profile = args[index];
        }
        return exit_code(run_doctor(
            project_root, *current_manifest_report.value, profile,
            requested_artifact, out, err
        ));
    }

    if (args[0] == "report") {
        if (args.size() < 2U) {
            emit_command_error(
                err, command_error::invalid_request,
                "expected: report <kind> [artifact] [--json]"
            );
            return exit_code(command_error::invalid_request);
        }
        args_list report_args(args.begin() + 2, args.end());
        const auto json_flags = std::erase(report_args, "--json");
        if (json_flags > 1) {
            emit_command_error(
                err, command_error::invalid_request,
                "--json may be specified only once"
            );
            return exit_code(command_error::invalid_request);
        }
        const bool json_output = json_flags == 1;
        if (workspace_mode) {
            workspace_scope scope;
            const command_error parse_status = parse_workspace_scope(
                *workspace, report_args, true, &scope, err
            );
            if (parse_status != command_error::ok) {
                return exit_code(parse_status);
            }
            return exit_code(run_workspace_report(
                *workspace, args[1], scope, out, err, json_output
            ));
        }
        if (report_args.size() > 1U) {
            emit_command_error(
                err, command_error::invalid_request,
                engels_command_support::expected_usage(
                    "report <kind> [component:artifact] [--json]"
                )
            );
            return exit_code(command_error::invalid_request);
        }
        const std::optional<artifact_ref> requested_artifact
            = report_args.size() == 1U ? parse_artifact_ref(report_args[0])
                                       : std::nullopt;
        if (report_args.size() == 1U && !requested_artifact.has_value()) {
            emit_command_error(
                err,
                command_error::invalid_request,
                "artifact must use component:artifact form"
            );
            return exit_code(command_error::invalid_request);
        }
        return exit_code(run_report(
            project_root, *current_manifest_report.value, args[1],
            requested_artifact, out, err, json_output
        ));
    }

    emit_command_error(
        err, command_error::invalid_request, "unknown command: " + args[0]
    );
    return exit_code(command_error::invalid_request);
}

}  // namespace ecosystem
