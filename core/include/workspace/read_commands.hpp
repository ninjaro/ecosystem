#pragma once

#include "workspace/project.hpp"
#include "workspace/workspace_scope.hpp"

#include <iosfwd>
#include <optional>
#include <string>

namespace ecosystem {

command_error run_list(
    const manifest& manifest_value,
    const std::optional<std::string>& target, std::ostream& out
);
command_error run_workspace_list(
    const workspace_context& workspace,
    const std::optional<std::string>& target, std::ostream& out
);
command_error run_check(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::string& profile,
    const std::optional<artifact_ref>& requested_artifact,
    const std::optional<std::string>& sphinx_theme, std::ostream& out,
    std::ostream& err
);
command_error run_workspace_check(
    const workspace_context& workspace, const std::string& profile,
    const workspace_scope& scope,
    const std::optional<std::string>& sphinx_theme, std::ostream& out,
    std::ostream& err
);
command_error run_doctor(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    std::optional<std::string> profile,
    std::optional<artifact_ref> requested_artifact, std::ostream& out,
    std::ostream& err
);
command_error run_workspace_doctor(
    const workspace_context& workspace,
    const std::optional<std::string>& profile, const workspace_scope& scope,
    std::ostream& out, std::ostream& err
);
command_error run_report(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::string& kind,
    const std::optional<artifact_ref>& requested_artifact, std::ostream& out,
    std::ostream& err, bool json_output = false
);
command_error run_workspace_report(
    const workspace_context& workspace, const std::string& kind,
    const workspace_scope& scope, std::ostream& out, std::ostream& err,
    bool json_output = false
);
command_error parse_workspace_doctor_request(
    const workspace_context& workspace, const string_list& args,
    std::optional<std::string>* profile, workspace_scope* scope,
    std::ostream& err
);

}  // namespace ecosystem
