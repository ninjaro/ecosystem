#pragma once

#include "manifest.hpp"
#include "workspace/groups.hpp"
#include "workspace/tooling.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace ecosystem {

struct workspace_project {
    std::filesystem::path root;
    std::filesystem::path manifest_path;
    std::optional<manifest> manifest_value;
    string_list errors;

    bool valid() const { return manifest_value.has_value() && errors.empty(); }

    std::string identity() const;
};

struct workspace_context {
    std::filesystem::path root;
    std::vector<workspace_project> projects;
    workspace_config config;
    std::filesystem::path config_path;
    string_list errors;
    bool has_config = false;
};

struct workspace_artifact_filter {
    const workspace_project* project = nullptr;
    artifact_ref artifact;
};

struct workspace_scope {
    std::vector<const workspace_project*> projects;
    string_list groups;
    std::vector<workspace_artifact_filter> artifact_filters;
};

std::optional<workspace_context>
discover_workspace(const std::filesystem::path& root);
void emit_workspace_errors(
    const workspace_context& workspace, std::ostream& err
);
command_error validate_workspace_scope(
    const workspace_context& workspace, const workspace_scope& scope,
    std::ostream& err
);
json workspace_project_json(
    const workspace_context& workspace, const workspace_project& project
);
std::string relative_workspace_path(
    const workspace_context& workspace, const std::filesystem::path& path
);
const workspace_project* find_workspace_project(
    const workspace_context& workspace, const std::string& selector
);
std::vector<const workspace_project*> selected_workspace_projects(
    const workspace_context& workspace, const workspace_scope& scope
);
const workspace_project*
single_selected_workspace_project(const workspace_scope& scope);
std::vector<artifact_ref> workspace_artifacts_for_project(
    const workspace_scope& scope, const workspace_project* project
);
std::vector<std::optional<artifact_ref>>
workspace_artifact_requests_for_project(
    const workspace_scope& scope, const workspace_project* project
);
std::optional<artifact_ref> workspace_single_artifact_for_project(
    const workspace_scope& scope, const workspace_project* project
);
std::size_t workspace_artifact_filter_count(const workspace_scope& scope);
command_error parse_workspace_scope(
    const workspace_context& workspace, const string_list& args,
    bool allow_artifact, workspace_scope* scope, std::ostream& err
);
json workspace_scope_json(
    const workspace_context& workspace, const workspace_scope& scope
);

} // namespace ecosystem
