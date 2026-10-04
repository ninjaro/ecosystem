#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace ecosystem::tooling_detail {

inline std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

inline void set_error(std::string* error_message, const std::string& message) {
    if (error_message != nullptr) {
        *error_message = message;
    }
}

inline std::string env_value(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

inline fs::path configure_stamp(const fs::path& build_dir) {
    return build_dir / ".ecosystem_configured.stamp";
}

/** Discard the build tree when CMake records a different source directory. */
bool reset_source(
    const fs::path& source_dir, const fs::path& build_dir,
    std::string* error_message
);

} // namespace ecosystem::tooling_detail
