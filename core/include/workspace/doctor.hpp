#pragma once

#include "manifest.hpp"
#include "packages/package_summary.hpp"
#include "workspace/tooling.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace ecosystem {

struct doctor_cache_refresh_result {
    command_error status = command_error::ok;
    build_cache_status cache_status;
    bool refreshed = false;
    std::string error_message;
};

std::string doctor_cache_profile(const std::optional<std::string>& profile);

std::filesystem::path local_doctor_dir(const std::filesystem::path& project_root
);
std::string
doctor_probe_scope_key(const std::optional<artifact_ref>& requested_artifact);

doctor_cache_refresh_result refresh_doctor_cache(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const dependency_summary& dependencies,
    const std::optional<artifact_ref>& requested_artifact,
    const std::optional<std::string>& profile
);

} // namespace ecosystem
