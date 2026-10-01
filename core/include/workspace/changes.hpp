#pragma once

#include "manifest.hpp"

namespace ecosystem {

// Inspect the event record in .ecosystem/github/changes.json against local Git
// objects and manifest ownership. Missing/untrusted inputs yield a full plan.
// The returned plan describes relevance, never successful verification.
json change_report(
    const std::filesystem::path& project_root, const manifest& value
);

} // namespace ecosystem
