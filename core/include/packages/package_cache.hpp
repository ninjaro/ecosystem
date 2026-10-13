#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ecosystem {

struct cmake_cache_snapshot {
    std::filesystem::path path;
    std::map<std::string, std::string> entries;
};

std::optional<cmake_cache_snapshot>
load_cmake_cache(const std::filesystem::path& cache_path);
bool cmake_cache_has_found_value(
    const cmake_cache_snapshot& cache, const std::string& key
);
bool cmake_cache_has_all_found_values(
    const cmake_cache_snapshot& cache, const std::vector<std::string_view>& keys
);
bool cmake_cache_flag_enabled(
    const cmake_cache_snapshot& cache, const std::string& key
);

} // namespace ecosystem
