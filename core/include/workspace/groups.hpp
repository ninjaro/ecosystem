#pragma once

#include "manifest.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace ecosystem {

struct workspace_group {
    std::string id;
    string_list project_selectors;
};

struct workspace_config {
    std::vector<workspace_group> groups;
};

struct workspace_config_report {
    std::filesystem::path path;
    std::optional<workspace_config> value;
    string_list errors;
    bool has_config = false;
};

std::filesystem::path
workspace_config_path(const std::filesystem::path& workspace_root);

workspace_config_report
load_workspace_config(const std::filesystem::path& workspace_root);

const workspace_group*
find_workspace_group(const workspace_config& value, const std::string& id);

json to_json(const workspace_config& value);

} // namespace ecosystem
