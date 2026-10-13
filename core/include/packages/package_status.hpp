#pragma once

#include "packages/package_kind.hpp"

#include <string>

namespace ecosystem {

struct cmake_cache_snapshot;
struct dependency_summary;

std::string configured_status_for_package(
    package_kind kind, const dependency_summary& dependencies,
    const cmake_cache_snapshot& cache
);

} // namespace ecosystem
