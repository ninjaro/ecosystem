#include "packages/package_cache.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <string>

namespace ecosystem {
namespace package_cache_support {

    char uppercase_character(const unsigned char character) {
        return static_cast<char>(std::toupper(character));
    }

    bool string_is_truthy(std::string value) {
        std::transform(
            value.begin(), value.end(), value.begin(), uppercase_character
        );
        return value == "1" || value == "ON" || value == "TRUE"
            || value == "YES" || value == "Y";
    }

    bool is_notfound_value(const std::string& value) {
        return value.ends_with("-NOTFOUND");
    }

    std::optional<std::string> cmake_cache_value(
        const cmake_cache_snapshot& cache, const std::string& key
    ) {
        const auto entry = cache.entries.find(key);
        if (entry == cache.entries.end()) {
            return std::nullopt;
        }
        return entry->second;
    }

} // namespace package_cache_support

using namespace package_cache_support;

std::optional<cmake_cache_snapshot>
load_cmake_cache(const std::filesystem::path& cache_path) {
    std::ifstream file(cache_path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    cmake_cache_snapshot snapshot;
    snapshot.path = cache_path;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line.front() == '#' || line.rfind("//", 0U) == 0U) {
            continue;
        }
        const std::size_t colon = line.find(':');
        const std::size_t equals
            = line.find('=', colon == std::string::npos ? 0U : colon + 1U);
        if (colon == std::string::npos || equals == std::string::npos) {
            continue;
        }
        snapshot.entries.emplace(
            line.substr(0U, colon), line.substr(equals + 1U)
        );
    }

    return snapshot;
}

bool cmake_cache_has_found_value(
    const cmake_cache_snapshot& cache, const std::string& key
) {
    const std::optional<std::string> value = cmake_cache_value(cache, key);
    return value.has_value() && !value->empty() && !is_notfound_value(*value);
}

bool cmake_cache_has_all_found_values(
    const cmake_cache_snapshot& cache, const std::vector<std::string_view>& keys
) {
    return std::ranges::all_of(keys, [&](const std::string_view key) {
        return cmake_cache_has_found_value(cache, std::string(key));
    });
}

bool cmake_cache_flag_enabled(
    const cmake_cache_snapshot& cache, const std::string& key
) {
    const std::optional<std::string> value = cmake_cache_value(cache, key);
    return value.has_value() && string_is_truthy(*value);
}

} // namespace ecosystem
