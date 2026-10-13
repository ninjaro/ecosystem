#pragma once

#include "manifest.hpp"

#include <filesystem>
#include <vector>

namespace ecosystem {

struct mutation_report {
    bool changed_manifest = false;
    std::vector<std::filesystem::path> written_files;
    string_list errors;
};

struct add_component_options {
    std::string template_kind = "static_lib";
    std::string artifact_id;
    std::vector<artifact_ref> artifact_links;
};

mutation_report add_component(
    const std::filesystem::path& project_root, manifest* value,
    const std::string& component_id
);
mutation_report add_component(
    const std::filesystem::path& project_root, manifest* value,
    const std::string& component_id, const std::string& template_kind
);
mutation_report add_component(
    const std::filesystem::path& project_root, manifest* value,
    const std::string& component_id, const add_component_options& options
);
mutation_report add_module(
    const std::filesystem::path& project_root, manifest* value,
    const std::string& component_id, const std::string& module_path
);
mutation_report add_files(
    const std::filesystem::path& project_root, const manifest& value,
    const std::string& component_id, const std::string& module_path
);
mutation_report add_file_unit(
    const std::filesystem::path& project_root, manifest* value,
    const std::string& component_id, const std::string& unit_id,
    const std::string& kind
);
mutation_report
set_facade_entry(manifest* value, const artifact_ref& entry_ref);

} // namespace ecosystem
