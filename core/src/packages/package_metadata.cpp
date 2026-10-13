#include "packages/package_metadata.hpp"

#include "packages/package_dependency.hpp"
#include "packages/package_registry.hpp"

#include <set>
#include <vector>

namespace ecosystem {

const dependency_entry& package_dependency_entry(
    const dependency_summary& dependencies, const package_descriptor& descriptor
) {
    return dependency_entry_for_id(dependencies, descriptor.dependency);
}

package_group effective_package_group(
    const package_descriptor& descriptor, const bool kde_enabled_for_profile
) {
    if (descriptor.promote_to_required_for_kde_profile
        && kde_enabled_for_profile) {
        return package_group::required;
    }
    return descriptor.group;
}

std::vector<package_surface_block>
enabled_surface_blocks(const dependency_summary& dependencies) {
    std::vector<package_surface_block> blocks;
    std::set<package_surface_block> enabled_blocks;

    for (const package_descriptor* descriptor :
         enabled_packages(dependencies)) {
        if (descriptor == nullptr) {
            continue;
        }
        enabled_blocks.insert(descriptor->surface_block);
    }

    for (const package_surface_block block : package_surface_block_order()) {
        if (enabled_blocks.erase(block) > 0) {
            blocks.push_back(block);
        }
    }
    for (const package_surface_block block : enabled_blocks) {
        blocks.push_back(block);
    }

    return blocks;
}

} // namespace ecosystem
