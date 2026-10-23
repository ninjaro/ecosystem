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
    const manifest& value, const std::filesystem::path& project_root = ".",
    const std::optional<artifact_ref>& requested_artifact = std::nullopt
);
// Operation-local model for the existing package roots and mandatory closure.
// Does not rewrite the authored manifest or change package identities.
manifest
distribution_build_manifest(const manifest& value, const artifact_ref& primary);
// Actor-owned SDK source: facade + declared distribution roots, without the
// unrelated developer graph. Configure with ECOSYSTEM_PROJECT_ROOT explicitly.
std::string generate_distribution_cmakelists(
    const manifest& value, const std::filesystem::path& project_root
);
// Uses the identical mandatory selection as developer CMake generation.
std::vector<const component*> developer_component_closure(
    const manifest& value, const std::optional<artifact_ref>& requested_artifact
);
std::vector<const component*>
distribution_component_closure(const manifest& value);
std::string generate_gitignore();
// Canonical generated configurations which may exist locally, but are ignored
// and are never required tracked surfaces. Paths are relative to the project.
std::vector<std::filesystem::path> ignored_local_surface_paths();
// The generation inventory also retains these optional local configurations.
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
