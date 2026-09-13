#pragma once

#include "workspace/mutation.hpp"
#include "workspace/project.hpp"
#include "workspace/release.hpp"
#include "workspace/workspace_scope.hpp"

#include <iosfwd>
#include <optional>
#include <string>

namespace ecosystem {

command_error run_format(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact, std::ostream& out,
    std::ostream& err
);
command_error run_workspace_format(
    const workspace_context& workspace, const workspace_scope& scope,
    std::ostream& out, std::ostream& err
);
command_error run_sync(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    std::ostream& out, std::ostream& err
);
command_error run_workspace_sync(
    const workspace_context& workspace, const workspace_scope& scope,
    std::ostream& out, std::ostream& err
);
command_error run_add_component(
    const std::filesystem::path& project_root, const std::string& actor_name,
    const std::string& component_id, const add_component_options& options,
    std::ostream& out, std::ostream& err
);
command_error run_add_module(
    const std::filesystem::path& project_root, const std::string& actor_name,
    const std::string& component_id, const std::string& module_path,
    std::ostream& out, std::ostream& err
);
command_error run_add_files(
    const std::filesystem::path& project_root, const std::string& actor_name,
    const std::string& component_id, const std::string& module_path,
    std::ostream& out, std::ostream& err
);
command_error run_add_file_unit(
    const std::filesystem::path& project_root, const std::string& actor_name,
    const std::string& component_id, const std::string& unit_id,
    const std::string& kind, std::ostream& out, std::ostream& err
);
command_error run_set_facade_entry(
    const std::filesystem::path& project_root, const std::string& actor_name,
    const artifact_ref& entry_ref, std::ostream& out, std::ostream& err
);
command_error run_build(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::string& profile,
    const std::optional<artifact_ref>& requested_artifact,
    std::ostream& out, std::ostream& err
);
command_error run_workspace_build(
    const workspace_context& workspace, const std::string& profile,
    const workspace_scope& scope, std::ostream& out, std::ostream& err
);
command_error run_prerelease(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    const std::optional<std::string>& version_base,
    const prerelease_signing_options& signing_options, std::ostream& out,
    std::ostream& err
);
command_error run_workspace_prerelease(
    const workspace_context& workspace, const workspace_scope& scope,
    const std::optional<std::string>& version_base,
    const prerelease_signing_options& signing_options, std::ostream& out,
    std::ostream& err
);
command_error run_benchmark(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    const string_list& passthrough_args, std::ostream& out, std::ostream& err
);
command_error run_workspace_benchmark(
    const workspace_context& workspace, const workspace_scope& scope,
    const string_list& passthrough_args, std::ostream& out, std::ostream& err
);
command_error run_run(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::string& profile,
    const std::optional<artifact_ref>& requested_artifact,
    const std::optional<std::string>& android_mode,
    const string_list& passthrough_args, std::ostream& out, std::ostream& err
);
command_error run_workspace_run(
    const workspace_context& workspace, const std::string& profile,
    const workspace_scope& scope,
    const std::optional<std::string>& android_mode,
    const string_list& passthrough_args, std::ostream& out, std::ostream& err
);

}  // namespace ecosystem
