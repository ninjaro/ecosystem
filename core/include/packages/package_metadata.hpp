#pragma once

#include "packages/package_dependency_id.hpp"
#include "packages/package_group.hpp"
#include "packages/package_kind.hpp"
#include "packages/package_rule.hpp"
#include "packages/package_surface_block.hpp"

#include <string_view>
#include <vector>

namespace ecosystem {

struct dependency_entry;
struct dependency_summary;

struct package_descriptor {
    package_kind kind;
    dependency_id dependency;
    std::vector<package_summary_rule> summary_rules;
    std::vector<package_link_rule> link_rules;
    package_status_rule status_rule;
    package_group group;
    package_surface_block surface_block;
    std::string_view label;
    bool include_values = false;
    bool promote_to_required_for_kde_profile = false;
};

const dependency_entry& package_dependency_entry(
    const dependency_summary& dependencies, const package_descriptor& descriptor
);
package_group effective_package_group(
    const package_descriptor& descriptor, bool kde_enabled_for_profile
);
std::vector<package_surface_block>
enabled_surface_blocks(const dependency_summary& dependencies);

} // namespace ecosystem
