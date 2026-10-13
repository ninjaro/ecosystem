#pragma once

#include "manifest.hpp"
#include "packages/package_dependency.hpp"

#include <optional>
#include <vector>

namespace ecosystem {

enum class package_dependency_scope { all, production, tests, benchmarks };

dependency_summary summarize_dependencies(
    const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    package_dependency_scope scope = package_dependency_scope::all
);
bool dependency_summary_has_entries(const dependency_summary& dependencies);
std::vector<std::string> component_package_link_targets(
    const component& component_value,
    package_dependency_scope scope = package_dependency_scope::production
);

} // namespace ecosystem
