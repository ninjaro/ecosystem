#pragma once

#include "packages/package_metadata.hpp"

#include <span>
#include <vector>

namespace ecosystem {

const std::vector<package_descriptor>& package_descriptors();
std::span<const package_descriptor> registered_packages();
std::vector<const package_descriptor*>
enabled_packages(const dependency_summary& dependencies);
bool has_enabled_packages(const dependency_summary& dependencies);
const package_descriptor* find_package_descriptor(package_kind kind);

} // namespace ecosystem
