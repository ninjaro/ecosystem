#include "workspace/workspace_scope.hpp"

#include "command_internal.hpp"

#include <algorithm>

namespace ecosystem::command_support {

bool workspace_project_id_less(
    const workspace_project& left, const workspace_project& right
) {
    return left.identity() < right.identity();
}

void append_workspace_project(
    std::vector<const workspace_project*>* projects,
    const workspace_project* project
) {
    if (std::find(projects->begin(), projects->end(), project)
        == projects->end()) {
        projects->push_back(project);
    }
}

void append_unique_string(string_list* values, const std::string& value) {
    if (std::find(values->begin(), values->end(), value) == values->end()) {
        values->push_back(value);
    }
}

bool workspace_artifact_filter_matches(
    const workspace_artifact_filter& filter, const workspace_project* project,
    const artifact_ref& artifact
) {
    return filter.project == project
        && filter.artifact.component_id == artifact.component_id
        && filter.artifact.artifact_id == artifact.artifact_id;
}

void append_workspace_artifact_filter(
    std::vector<workspace_artifact_filter>* filters,
    const workspace_project* project, const artifact_ref& artifact
) {
    for (const workspace_artifact_filter& filter : *filters) {
        if (workspace_artifact_filter_matches(filter, project, artifact)) {
            return;
        }
    }
    filters->push_back(workspace_artifact_filter { project, artifact });
}

json workspace_artifact_filter_json(const workspace_artifact_filter& filter) {
    return json::object({
        { "project", filter.project->identity() },
        { "artifact", format_artifact_ref(filter.artifact) },
    });
}

std::optional<qualified_workspace_artifact>
parse_qualified_workspace_artifact(const std::string& value) {
    const std::size_t slash = value.find('/');
    if (slash == std::string::npos) {
        return std::nullopt;
    }
    const std::optional<artifact_ref> artifact
        = parse_artifact_ref(value.substr(slash + 1U));
    if (!artifact.has_value()) {
        return std::nullopt;
    }
    return qualified_workspace_artifact { value.substr(0U, slash), *artifact };
}

} // namespace ecosystem::command_support

namespace ecosystem {

std::string workspace_project::identity() const {
    return manifest_value.has_value() && !manifest_value->id.empty()
        ? manifest_value->id
        : root.filename().generic_string();
}

std::optional<workspace_context>
discover_workspace(const std::filesystem::path& root) {
    std::vector<workspace_project> projects;
    std::error_code error;
    if (!std::filesystem::exists(root, error) || error) {
        return std::nullopt;
    }

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(root, error)) {
        if (error || !entry.is_directory()) {
            continue;
        }
        const std::string directory_name
            = entry.path().filename().generic_string();
        if (!directory_name.empty() && directory_name.front() == '.') {
            continue;
        }

        const manifest_report report
            = load_manifest(entry.path() / "manifest.json");
        if (!report.has_manifest) {
            continue;
        }
        projects.push_back(workspace_project { entry.path(), report.path,
                                               report.value, report.errors });
    }

    std::sort(
        projects.begin(), projects.end(),
        command_support::workspace_project_id_less
    );

    if (projects.empty()) {
        return std::nullopt;
    }
    workspace_context workspace {
        root, projects, {}, {}, {}, false,
    };
    const workspace_config_report config_report = load_workspace_config(root);
    workspace.config_path = config_report.path;
    workspace.has_config = config_report.has_config;
    workspace.errors = config_report.errors;
    if (config_report.value.has_value()) {
        workspace.config = *config_report.value;
        for (const workspace_group& group : workspace.config.groups) {
            for (const std::string& selector : group.project_selectors) {
                if (find_workspace_project(workspace, selector) == nullptr) {
                    workspace.errors.push_back(
                        "manifesto.workspace.json.groups." + group.id
                        + " references unknown project selector: " + selector
                    );
                }
            }
        }
    }
    return workspace;
}

void emit_workspace_errors(
    const workspace_context& workspace, std::ostream& err
) {
    for (const std::string& message : workspace.errors) {
        command_support::print_error(
            err, command_error::invalid_request, message
        );
    }
}

command_error validate_workspace_scope(
    const workspace_context& workspace, const workspace_scope& scope,
    std::ostream& err
) {
    command_error status = command_error::ok;
    for (const workspace_project* project :
         selected_workspace_projects(workspace, scope)) {
        if (project->valid()) {
            continue;
        }
        status = command_error::invalid_request;
        command_support::print_error(
            err, status,
            "invalid project: " + project->identity() + " ("
                + relative_workspace_path(workspace, project->manifest_path)
                + ")"
        );
        for (const std::string& message : project->errors) {
            command_support::print_error(err, status, message);
        }
    }
    return status;
}

json workspace_project_json(
    const workspace_context& workspace, const workspace_project& project
) {
    return json::object({
        { "project", project.identity() },
        { "root", relative_workspace_path(workspace, project.root) },
        { "manifest_path",
          relative_workspace_path(workspace, project.manifest_path) },
        { "valid", project.valid() },
        { "errors", project.errors },
    });
}

std::string relative_workspace_path(
    const workspace_context& workspace, const std::filesystem::path& path
) {
    return path.lexically_relative(workspace.root).generic_string();
}

const workspace_project* find_workspace_project(
    const workspace_context& workspace, const std::string& selector
) {
    for (const workspace_project& project : workspace.projects) {
        if (project.identity() == selector
            || relative_workspace_path(workspace, project.root) == selector
            || project.root.filename().generic_string() == selector) {
            return &project;
        }
    }
    return nullptr;
}

std::vector<const workspace_project*> selected_workspace_projects(
    const workspace_context& workspace, const workspace_scope& scope
) {
    if (!scope.projects.empty()) {
        return scope.projects;
    }

    std::vector<const workspace_project*> selected;
    selected.reserve(workspace.projects.size());
    for (const workspace_project& project : workspace.projects) {
        selected.push_back(&project);
    }
    return selected;
}

const workspace_project*
single_selected_workspace_project(const workspace_scope& scope) {
    return scope.projects.size() == 1U ? scope.projects.front() : nullptr;
}

std::vector<artifact_ref> workspace_artifacts_for_project(
    const workspace_scope& scope, const workspace_project* project
) {
    std::vector<artifact_ref> artifacts;
    for (const workspace_artifact_filter& filter : scope.artifact_filters) {
        if (filter.project == project) {
            artifacts.push_back(filter.artifact);
        }
    }
    return artifacts;
}

std::vector<std::optional<artifact_ref>>
workspace_artifact_requests_for_project(
    const workspace_scope& scope, const workspace_project* project
) {
    const std::vector<artifact_ref> artifacts
        = workspace_artifacts_for_project(scope, project);
    if (artifacts.empty()) {
        return { std::nullopt };
    }

    std::vector<std::optional<artifact_ref>> requests;
    requests.reserve(artifacts.size());
    for (const artifact_ref& artifact : artifacts) {
        requests.emplace_back(artifact);
    }
    return requests;
}

std::optional<artifact_ref> workspace_single_artifact_for_project(
    const workspace_scope& scope, const workspace_project* project
) {
    if (scope.artifact_filters.size() != 1U) {
        return std::nullopt;
    }
    const workspace_artifact_filter& filter = scope.artifact_filters.front();
    return filter.project == project ? std::make_optional(filter.artifact)
                                     : std::nullopt;
}

std::size_t workspace_artifact_filter_count(const workspace_scope& scope) {
    return scope.artifact_filters.size();
}

command_error parse_workspace_scope(
    const workspace_context& workspace, const string_list& args,
    const bool allow_artifact, workspace_scope* scope, std::ostream& err
) {
    std::vector<const workspace_project*> explicit_projects;
    std::vector<command_support::unresolved_workspace_artifact_filter>
        requested_artifacts;
    scope->projects.clear();
    scope->groups.clear();
    scope->artifact_filters.clear();

    for (std::size_t index = 0U; index < args.size(); ++index) {
        if (args[index] == "--project") {
            if (index + 1U >= args.size()) {
                command_support::print_error(
                    err, command_error::invalid_request,
                    "--project requires a value"
                );
                return command_error::invalid_request;
            }
            const workspace_project* project
                = find_workspace_project(workspace, args[index + 1U]);
            if (project == nullptr) {
                command_support::print_error(
                    err, command_error::invalid_request,
                    "unknown project: " + args[index + 1U]
                );
                return command_error::invalid_request;
            }
            command_support::append_workspace_project(
                &explicit_projects, project
            );
            ++index;
            continue;
        }
        if (args[index] == "--group") {
            if (index + 1U >= args.size()) {
                command_support::print_error(
                    err, command_error::invalid_request,
                    "--group requires a value"
                );
                return command_error::invalid_request;
            }
            const workspace_group* group
                = find_workspace_group(workspace.config, args[index + 1U]);
            if (group == nullptr) {
                command_support::print_error(
                    err, command_error::invalid_request,
                    "unknown workspace group: " + args[index + 1U]
                );
                return command_error::invalid_request;
            }
            command_support::append_unique_string(&scope->groups, group->id);
            for (const std::string& selector : group->project_selectors) {
                const workspace_project* project
                    = find_workspace_project(workspace, selector);
                if (project == nullptr) {
                    command_support::print_error(
                        err, command_error::invalid_request,
                        "workspace group " + group->id
                            + " references unknown project selector: "
                            + selector
                    );
                    return command_error::invalid_request;
                }
                command_support::append_workspace_project(
                    &explicit_projects, project
                );
            }
            ++index;
            continue;
        }

        if (args[index].find(':') != std::string::npos) {
            if (!allow_artifact) {
                command_support::print_error(
                    err, command_error::invalid_request,
                    "artifact filters are not supported for this command"
                );
                return command_error::invalid_request;
            }
            if (const std::optional<
                    command_support::qualified_workspace_artifact>
                    qualified
                = command_support::parse_qualified_workspace_artifact(
                    args[index]
                );
                qualified.has_value()) {
                requested_artifacts.push_back(
                    command_support::unresolved_workspace_artifact_filter {
                        qualified->project_selector,
                        qualified->artifact,
                    }
                );
                continue;
            }

            const std::optional<artifact_ref> requested_artifact
                = parse_artifact_ref(args[index]);
            if (!requested_artifact.has_value()) {
                command_support::print_error(
                    err, command_error::invalid_request,
                    "artifact must use component:artifact or "
                    "project/component:artifact form"
                );
                return command_error::invalid_request;
            }
            requested_artifacts.push_back(
                command_support::unresolved_workspace_artifact_filter {
                    std::nullopt,
                    *requested_artifact,
                }
            );
            continue;
        }

        command_support::print_error(
            err, command_error::invalid_request,
            "unexpected workspace argument: " + args[index]
        );
        return command_error::invalid_request;
    }

    if (!explicit_projects.empty()) {
        scope->projects = explicit_projects;
    }

    std::vector<command_support::unresolved_workspace_artifact_filter>
        unqualified_artifacts;
    for (const command_support::unresolved_workspace_artifact_filter& filter :
         requested_artifacts) {
        if (!filter.project_selector.has_value()) {
            unqualified_artifacts.push_back(filter);
            continue;
        }

        const workspace_project* artifact_project
            = find_workspace_project(workspace, *filter.project_selector);
        if (artifact_project == nullptr) {
            command_support::print_error(
                err, command_error::invalid_request,
                "unknown artifact project: " + *filter.project_selector
            );
            return command_error::invalid_request;
        }
        if (!artifact_project->valid()) {
            workspace_scope invalid_scope;
            invalid_scope.projects = { artifact_project };
            return validate_workspace_scope(workspace, invalid_scope, err);
        }

        if (std::find(
                scope->projects.begin(), scope->projects.end(), artifact_project
            )
            == scope->projects.end()) {
            if (!explicit_projects.empty()) {
                command_support::print_error(
                    err, command_error::invalid_request,
                    "artifact project does not match the selected "
                    "workspace projects"
                );
                return command_error::invalid_request;
            }
            command_support::append_workspace_project(
                &scope->projects, artifact_project
            );
        }

        if (!resolve_artifact(
                 *artifact_project->manifest_value, filter.artifact
            )
                 .has_value()) {
            command_support::print_error(
                err, command_error::invalid_request, "unknown artifact request"
            );
            return command_error::invalid_request;
        }
        command_support::append_workspace_artifact_filter(
            &scope->artifact_filters, artifact_project, filter.artifact
        );
    }

    if (!unqualified_artifacts.empty() && scope->projects.empty()) {
        command_support::print_error(
            err, command_error::invalid_request,
            "workspace artifact filters require --project <project>, "
            "--group <group>, or project/component:artifact form"
        );
        return command_error::invalid_request;
    }
    if (!unqualified_artifacts.empty() && scope->projects.size() != 1U) {
        command_support::print_error(
            err, command_error::invalid_request,
            "unqualified workspace artifact filters require exactly one "
            "selected workspace project"
        );
        return command_error::invalid_request;
    }

    if (!unqualified_artifacts.empty()) {
        const workspace_project* project = scope->projects.front();
        if (!project->valid()) {
            return validate_workspace_scope(workspace, *scope, err);
        }
        for (const command_support::unresolved_workspace_artifact_filter&
                 filter : unqualified_artifacts) {
            if (!resolve_artifact(*project->manifest_value, filter.artifact)
                     .has_value()) {
                command_support::print_error(
                    err, command_error::invalid_request,
                    "unknown artifact request"
                );
                return command_error::invalid_request;
            }
            command_support::append_workspace_artifact_filter(
                &scope->artifact_filters, project, filter.artifact
            );
        }
    }

    return command_error::ok;
}

json workspace_scope_json(
    const workspace_context& workspace, const workspace_scope& scope
) {
    if (scope.projects.empty() && scope.groups.empty()
        && scope.artifact_filters.empty()) {
        return {};
    }

    json selection = json::object();
    if (scope.groups.size() == 1U) {
        selection["group"] = scope.groups.front();
    } else if (!scope.groups.empty()) {
        selection["groups"] = scope.groups;
    }
    if (scope.projects.size() == 1U) {
        selection["project"] = scope.projects.front()->identity();
        selection["root"]
            = relative_workspace_path(workspace, scope.projects.front()->root);
    } else if (!scope.projects.empty()) {
        json selected_projects = json::array();
        for (const workspace_project* project : scope.projects) {
            selected_projects.push_back(json::object({
                { "project", project->identity() },
                { "root", relative_workspace_path(workspace, project->root) },
            }));
        }
        selection["projects"] = selected_projects;
    }
    if (scope.artifact_filters.size() == 1U) {
        selection["artifact"]
            = format_artifact_ref(scope.artifact_filters.front().artifact);
    } else if (!scope.artifact_filters.empty()) {
        json selected_artifacts = json::array();
        for (const workspace_artifact_filter& filter : scope.artifact_filters) {
            selected_artifacts.push_back(
                command_support::workspace_artifact_filter_json(filter)
            );
        }
        selection["artifacts"] = selected_artifacts;
    }
    return selection;
}

} // namespace ecosystem
