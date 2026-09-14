#pragma once

#include "manifest.hpp"

namespace ecosystem {

std::filesystem::path local_doxygen_dir(
    const std::filesystem::path& project_root,
    const std::optional<artifact_ref>& requested_artifact = std::nullopt
);
bool write_local_doxygen_config(
    const std::filesystem::path& project_root, const manifest& value,
    const std::optional<artifact_ref>& requested_artifact,
    std::string* error_message
);
bool prepare_local_doxygen_output(
    const std::filesystem::path& project_root,
    const std::optional<artifact_ref>& requested_artifact,
    std::string* error_message
);

} // namespace ecosystem
