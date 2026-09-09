#pragma once

#include "packages/package_surface_render.hpp"

#include <string>

namespace ecosystem {

struct package_surface_options {
    bool support_targets = false;
    bool imported_support_targets = false;
    bool qt_automation = false;
    bool enable_testing = false;
    bool emit_profile_option_lines = false;
};

std::string render_package_surface(
    const dependency_summary& dependencies, const package_surface_options& options
);

}  // namespace ecosystem
