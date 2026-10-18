#include "packages/package_surface_block.hpp"

#include <vector>

namespace ecosystem {

const std::vector<package_surface_block>& package_surface_block_order() {
    static const std::vector<package_surface_block> order {
        package_surface_block::nlohmann_json,
        package_surface_block::llvm_clang,
        package_surface_block::qt,
        package_surface_block::kde,
        package_surface_block::opencv,
        package_surface_block::eigen,
        package_surface_block::jni,
        package_surface_block::cxxopts,
        package_surface_block::zlib,
        package_surface_block::gtest,
        package_surface_block::benchmark,
    };
    return order;
}

} // namespace ecosystem
