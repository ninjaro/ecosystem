#include "packages/package_dependency.hpp"

namespace ecosystem {

const dependency_entry& dependency_entry_for_id(
    const dependency_summary& dependencies, const dependency_id id
) {
    switch (id) {
    case dependency_id::nlohmann_json:
        return dependencies.json;
    case dependency_id::llvm_clang:
        return dependencies.llvm_clang;
    case dependency_id::qt:
        return dependencies.qt;
    case dependency_id::kde:
        return dependencies.kde;
    case dependency_id::kdegames:
        return dependencies.kdegames;
    case dependency_id::opencv:
        return dependencies.opencv;
    case dependency_id::eigen:
        return dependencies.eigen;
    case dependency_id::jni:
        return dependencies.jni;
    case dependency_id::cxxopts:
        return dependencies.cxxopts;
    case dependency_id::gtest:
        return dependencies.gtest;
    case dependency_id::benchmark:
        return dependencies.benchmark;
    }

    return dependencies.json;
}

dependency_entry& dependency_entry_for_id(
    dependency_summary& dependencies, const dependency_id id
) {
    switch (id) {
    case dependency_id::nlohmann_json:
        return dependencies.json;
    case dependency_id::llvm_clang:
        return dependencies.llvm_clang;
    case dependency_id::qt:
        return dependencies.qt;
    case dependency_id::kde:
        return dependencies.kde;
    case dependency_id::kdegames:
        return dependencies.kdegames;
    case dependency_id::opencv:
        return dependencies.opencv;
    case dependency_id::eigen:
        return dependencies.eigen;
    case dependency_id::jni:
        return dependencies.jni;
    case dependency_id::cxxopts:
        return dependencies.cxxopts;
    case dependency_id::gtest:
        return dependencies.gtest;
    case dependency_id::benchmark:
        return dependencies.benchmark;
    }

    return dependencies.json;
}

} // namespace ecosystem
