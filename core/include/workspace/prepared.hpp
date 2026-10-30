#pragma once

#include "workspace/project.hpp"
#include "workspace/tooling.hpp"

namespace ecosystem {

struct prepared_verification {
    command_error status = command_error::task_failed;
    json report;
};

// Checks an existing Marx handoff. Never builds or modifies its payload.
// A successful local check is not publication authorization.
prepared_verification verify_prepared_distribution(
    const std::filesystem::path& project_root,
    const std::filesystem::path& input_file,
    const std::optional<artifact_ref>& requested_artifact = std::nullopt
);

} // namespace ecosystem
