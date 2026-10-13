#pragma once

#include "packages/package_cache.hpp"
#include "packages/package_dependency.hpp"
#include "packages/package_group.hpp"

#include <string>
#include <vector>

namespace ecosystem {

struct package_line {
    std::string label;
    dependency_entry entry;
    bool include_values = false;
};

struct package_section {
    package_group group = package_group::required;
    std::vector<package_line> packages;
};

struct package_status_line {
    std::string label;
    std::string status;
};

struct package_status_section {
    package_group group = package_group::required;
    std::vector<package_status_line> packages;
};

std::optional<cmake_cache_snapshot>
load_cmake_cache(const std::filesystem::path& cache_path);
bool cmake_cache_has_found_value(
    const cmake_cache_snapshot& cache, const std::string& key
);
bool cmake_cache_flag_enabled(
    const cmake_cache_snapshot& cache, const std::string& key
);

std::vector<package_section> declared_package_sections(
    const dependency_summary& dependencies, bool kde_enabled_for_profile
);
std::vector<package_status_section> configured_package_sections(
    const dependency_summary& dependencies, const cmake_cache_snapshot& cache
);

} // namespace ecosystem
