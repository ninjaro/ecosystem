#pragma once

#include "workspace/project.hpp"
#include "workspace/tooling.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace ecosystem {

std::filesystem::path
local_prerelease_dir(const std::filesystem::path& project_root);
std::string prerelease_package_name(
    const manifest& manifest_value, const artifact_ref& ref,
    const artifact& artifact_value
);
// Strict hashes and a fresh, complete staged payload; no archives or
// publication.
std::string prepared_file_sha256(
    const std::filesystem::path& path, std::string* error_message
);
command_error export_prepared_release_input(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const resolved_artifact& resolved,
    const std::filesystem::path& built_artifact_path,
    const std::filesystem::path& preparation_dir,
    const std::string& expected_manifest_sha256,
    const std::optional<std::string>& version_base,
    std::filesystem::path* input_file, std::string* error_message
);

} // namespace ecosystem
