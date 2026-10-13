#pragma once

#include "packages/package_surface_rule.hpp"

#include <string>

namespace ecosystem {

struct dependency_summary;
struct package_surface_options;

std::string render_package_surface_rule(
    const package_surface_rule& rule, const dependency_summary& dependencies,
    const package_surface_options& options
);

std::string render_package_surface_block(
    package_surface_block block, const dependency_summary& dependencies,
    const package_surface_options& options
);

} // namespace ecosystem
