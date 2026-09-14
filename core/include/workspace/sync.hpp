#pragma once

#include "manifest.hpp"

#include <filesystem>
#include <vector>

namespace ecosystem {

struct sync_report {
    std::vector<std::filesystem::path> written_files;
    std::vector<std::filesystem::path> removed_files;
    string_list errors;
};

struct tracked_surface_file {
    std::filesystem::path relative_path;
    std::string contents;
};

bool artifact_has_cmake_export(const manifest& value, const artifact_ref& ref);

// String generators throw template_render_error on a required render failure.
// The tracked-surface generator translates that failure into errors when
// supplied.
std::string generate_cmakelists(
    const manifest& value, const std::filesystem::path& project_root = "."
);
std::string generate_developer_cmakelists(
    const manifest& value, const std::filesystem::path& project_root = "."
);
std::string generate_gitignore();
std::vector<tracked_surface_file> generate_tracked_surface_files(
    const manifest& value, const std::filesystem::path& project_root = ".",
    string_list* errors = nullptr
);
string_list tracked_surface_drift(
    const std::filesystem::path& project_root, const manifest& value
);
sync_report
sync_project(const std::filesystem::path& project_root, const manifest& value);

} // namespace ecosystem
