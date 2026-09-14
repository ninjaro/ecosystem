#include "workspace/read_commands.hpp"

#include "analysis/personal.hpp"
#include "command_internal.hpp"

#include <optional>
#include <string>

namespace ecosystem::command_support {

struct diagnostic_report_result {
    json report = json::object();
    command_error status = command_error::ok;
};

diagnostic_report_result build_diagnostic_report(
    const fs::path& root, const manifest& value, const std::string& kind,
    const std::optional<artifact_ref>& requested
) {
    if (kind == "naming" || kind == "style") {
        const auto report
            = analyze_personal_checks(value, root, requested, kind);
        return { to_json(report),
                 report.errors.empty() ? command_error::ok
                                       : command_error::task_failed };
    }
    cxx_analysis_report report;
    const auto resolved = resolve_artifact(value, requested);
    const bool benchmarks
        = resolved && component_is_benchmark_only(*resolved->component_value);
    report.project_id = value.id;
    report.requested_artifact = requested;
    report.cpp_standard = value.cpp_standard;
    report.tests_included = true;
    report.benchmarks_included = benchmarks;
    std::string configure_errors;
    const auto status = configure_build_tree(
        root, value, "debug", has_tests_enabled(value), false,
        benchmarks || has_benchmarks_enabled(value), &configure_errors, true
    );
    if (status != command_error::ok) {
        report.errors.push_back(
            "configure failed for C++ analysis: " + configure_errors
        );
        return { to_json(report), status };
    }
    report = analyze_project_sources(value, root, requested, true, benchmarks);
    return { to_json(report),
             report.errors.empty() ? command_error::ok
                                   : command_error::task_failed };
}

diagnostic_report_result build_workspace_diagnostic_report(
    const workspace_context& workspace, const workspace_scope& scope,
    const std::string& kind
) {
    diagnostic_report_result result;
    result.report["workspace_root"] = workspace.root.string();
    result.report["profile"] = kind;
    json projects = json::array();
    json errors = json::array();
    bool findings = false;
    for (const workspace_project* project :
         selected_workspace_projects(workspace, scope)) {
        for (const auto& requested :
             workspace_artifact_requests_for_project(scope, project)) {
            auto analysis = build_diagnostic_report(
                project->root, *project->manifest_value, kind, requested
            );
            result.status = combine_status(result.status, analysis.status);
            auto& report = analysis.report;
            report["project"] = project->identity();
            report["root"] = relative_workspace_path(workspace, project->root);
            findings = findings || !report.at("findings").empty();
            for (const auto& message : report.at("errors"))
                errors.push_back(
                    project->identity() + ": " + message.get<std::string>()
                );
            projects.push_back(std::move(report));
        }
    }
    result.report["projects"] = projects;
    result.report["errors"] = errors;
    result.report["status"] = result.status != command_error::ok ? "failed"
        : findings                                               ? "findings"
                                                                 : "clean";
    const json selection = workspace_scope_json(workspace, scope);
    if (!selection.is_null() && !selection.empty())
        result.report["selection"] = selection;
    return result;
}

command_error write_diagnostic_report(
    const diagnostic_report_result& result, const fs::path& root,
    const fs::path& path, const bool json_output, std::ostream& out,
    std::ostream& err
) {
    std::string error;
    if (!write_text_file(path, result.report.dump(2) + "\n", &error)) {
        print_error(err, command_error::task_failed, error);
        return command_error::task_failed;
    }
    if (json_output)
        out << result.report.dump(2) << "\n";
    else {
        render_diagnostic_report(result.report, out);
        out << "report: " << path.lexically_relative(root).generic_string()
            << "\n";
    }
    return result.status;
}

command_error run_report(
    const fs::path& project_root, const manifest& manifest_value,
    const std::string& kind,
    const std::optional<artifact_ref>& requested_artifact, std::ostream& out,
    std::ostream& err, const bool json_output
) {
    if (!contains_string(known_report_kinds, kind)) {
        print_error(
            err, command_error::invalid_request,
            "unknown report kind: " + kind
        );
        return command_error::invalid_request;
    }

    if (requested_artifact
        && !resolve_artifact(manifest_value, requested_artifact)) {
        print_error(
            err, command_error::invalid_request,
            "unknown artifact request: "
                + format_artifact_ref(*requested_artifact)
        );
        return command_error::invalid_request;
    }
    if (kind == "toolchains" && requested_artifact) {
        print_error(
            err, command_error::invalid_request,
            "toolchains report does not support artifact filters"
        );
        return command_error::invalid_request;
    }
    ensure_local_artifacts(project_root, false, false);

    if (kind == "toolchains") {
        const json report = toolchains_report();
        const fs::path report_path
            = local_report_dir(project_root) / "toolchains.json";
        std::string error_message;
        if (!write_text_file(
                report_path, report.dump(2) + "\n", &error_message
            )) {
            print_error(err, command_error::task_failed, error_message);
            return command_error::task_failed;
        }
        out << report.dump(2) << "\n";
        return command_error::ok;
    }

    if (kind == "matrix") {
        const json report = matrix_report(manifest_value, requested_artifact);
        const fs::path report_path = local_report_dir(project_root) / "matrix.json";
        std::string error_message;
        if (!write_text_file(
                report_path, report.dump(2) + "\n", &error_message
            )) {
            print_error(err, command_error::task_failed, error_message);
            return command_error::task_failed;
        }
        out << report.dump(2) << "\n";
        return command_error::ok;
    }

    const auto result = build_diagnostic_report(
        project_root, manifest_value, kind, requested_artifact
    );
    return write_diagnostic_report(
        result, project_root, local_report_dir(project_root) / (kind + ".json"),
        json_output, out, err
    );
}

command_error run_workspace_report(
    const workspace_context& workspace, const std::string& kind,
    const workspace_scope& scope, std::ostream& out, std::ostream& err,
    const bool json_output
) {
    if (!contains_string(known_report_kinds, kind)) {
        print_error(
            err, command_error::invalid_request,
            "unknown report kind: " + kind
        );
        return command_error::invalid_request;
    }

    if (kind == "cxx" || kind == "naming" || kind == "style") {
        const command_error validity
            = validate_workspace_scope(workspace, scope, err);
        if (validity != command_error::ok)
            return validity;
    }
    ensure_local_artifacts(workspace.root, false, false);

    if (kind == "toolchains") {
        if (workspace_artifact_filter_count(scope) > 0U) {
            print_error(
                err, command_error::invalid_request,
                "toolchains report does not support artifact filters"
            );
            return command_error::invalid_request;
        }
        json report = toolchains_report();
        report["workspace_root"] = workspace.root.string();
        report["projects"]
            = workspace_matrix_report(workspace, scope).at("projects");
        const json selection = workspace_scope_json(workspace, scope);
        if (!selection.is_null() && !selection.empty()) {
            report["selection"] = selection;
        }
        const fs::path report_path
            = local_report_dir(workspace.root) / "workspace_toolchains.json";
        std::string error_message;
        if (!write_text_file(
                report_path, report.dump(2) + "\n", &error_message
            )) {
            print_error(err, command_error::task_failed, error_message);
            return command_error::task_failed;
        }
        out << report.dump(2) << "\n";
        return command_error::ok;
    }

    if (kind == "matrix") {
        const json report = workspace_matrix_report(workspace, scope);
        const fs::path report_path
            = local_report_dir(workspace.root) / "workspace_matrix.json";
        std::string error_message;
        if (!write_text_file(
                report_path, report.dump(2) + "\n", &error_message
            )) {
            print_error(err, command_error::task_failed, error_message);
            return command_error::task_failed;
        }
        out << report.dump(2) << "\n";
        return command_error::ok;
    }

    const auto result
        = build_workspace_diagnostic_report(workspace, scope, kind);
    return write_diagnostic_report(
        result, workspace.root,
        local_report_dir(workspace.root) / ("workspace_" + kind + ".json"),
        json_output, out, err
    );
}

}  // namespace ecosystem::command_support

namespace ecosystem {

command_error run_report(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::string& kind,
    const std::optional<artifact_ref>& requested_artifact, std::ostream& out,
    std::ostream& err, const bool json_output
) {
    return command_support::run_report(
        project_root, manifest_value, kind, requested_artifact, out, err,
        json_output
    );
}

command_error run_workspace_report(
    const workspace_context& workspace, const std::string& kind,
    const workspace_scope& scope, std::ostream& out, std::ostream& err,
    const bool json_output
) {
    return command_support::run_workspace_report(
        workspace, kind, scope, out, err, json_output
    );
}

}  // namespace ecosystem
