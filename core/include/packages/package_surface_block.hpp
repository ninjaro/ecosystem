#pragma once

#include <vector>

namespace ecosystem {

enum class package_surface_block {
    nlohmann_json,
    llvm_clang,
    qt,
    kde,
    opencv,
    eigen,
    jni,
    cxxopts,
    gtest,
    benchmark,
};

const std::vector<package_surface_block>& package_surface_block_order();

} // namespace ecosystem
