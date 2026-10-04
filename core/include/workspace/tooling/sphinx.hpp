#pragma once

#include <filesystem>
#include <string>

namespace ecosystem {

bool write_local_sphinx_conf(
    const std::filesystem::path& project_root, const std::string& project_name,
    std::string* error_message
);
bool prepare_local_sphinx_output(
    const std::filesystem::path& project_root, std::string* error_message
);
bool publish_local_sphinx_output(
    const std::filesystem::path& project_root, std::string* error_message
);

} // namespace ecosystem
