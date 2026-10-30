#pragma once

#include "manifest.hpp"
#include "packages/package_dependency_id.hpp"

#include <set>
#include <string>

namespace ecosystem {

struct dependency_entry {
    bool enabled = false;
    std::set<std::string> owners;
    std::set<std::string> values;
};

struct dependency_summary {
    std::optional<qt_requirement> qt_versions;
    dependency_entry json;
    dependency_entry llvm_clang;
    dependency_entry qt;
    dependency_entry kde;
    dependency_entry kdegames;
    dependency_entry opencv;
    dependency_entry eigen;
    dependency_entry jni;
    dependency_entry cxxopts;
    dependency_entry zlib;
    dependency_entry gtest;
    dependency_entry benchmark;
    dependency_entry openssl;
};

const dependency_entry& dependency_entry_for_id(
    const dependency_summary& dependencies, dependency_id id
);
dependency_entry&
dependency_entry_for_id(dependency_summary& dependencies, dependency_id id);

} // namespace ecosystem
