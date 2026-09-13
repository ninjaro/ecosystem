#pragma once

#include "manifest.hpp"

#include <clang-c/CXCompilationDatabase.h>
#include <clang-c/Index.h>
#include <filesystem>
#include <vector>

namespace ecosystem::analysis_support {

std::string to_string(CXString value);
std::vector<std::string> fallback_args(
    const manifest& value,
    const std::vector<std::filesystem::path>& include_dirs
);
std::vector<std::string> detected_system_include_args();
std::vector<std::string> compilation_database_args(
    CXCompilationDatabase database, const std::filesystem::path& file
);

} // namespace ecosystem::analysis_support
