#include "internal.hpp"

#include "packages/package_cache.hpp"

#include <algorithm>
#include <fstream>

namespace ecosystem::release_detail {

bool ensure_clean_directory(const fs::path& path, std::string* error_message) {
    std::error_code error;
    fs::remove_all(path, error);
    if (error) {
        *error_message
            = "unable to reset " + path.string() + ": " + error.message();
        return false;
    }
    fs::create_directories(path, error);
    if (error) {
        *error_message
            = "unable to create " + path.string() + ": " + error.message();
        return false;
    }
    return true;
}

bool copy_directory_tree(
    const fs::path& source, const fs::path& destination,
    std::string* error_message
) {
    std::error_code error;
    fs::remove_all(destination, error);
    error.clear();
    fs::create_directories(destination.parent_path(), error);
    if (error) {
        *error_message = "unable to create "
            + destination.parent_path().string() + ": " + error.message();
        return false;
    }
    fs::copy(source, destination, fs::copy_options::recursive, error);
    if (error) {
        *error_message
            = "unable to copy " + source.string() + ": " + error.message();
        return false;
    }
    return true;
}

std::string package_install_subdirectory(const artifact& artifact_value) {
    if (artifact_value.kind == "exe" || artifact_value.kind == "qt_app") {
        return "usr/bin";
    }
    if (artifact_value.kind == "shared_lib"
        || artifact_value.kind == "static_lib") {
        return "usr/lib";
    }
    return {};
}

bool project_has_assets_dir(const fs::path& project_root) {
    std::error_code error;
    return fs::exists(project_root / "assets", error) && !error;
}

std::uintmax_t directory_file_size(const fs::path& root) {
    std::uintmax_t total = 0U;
    std::error_code error;
    if (!fs::exists(root, error) || error) {
        return total;
    }
    fs::recursive_directory_iterator iterator(root, error);
    const fs::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error)) {
            total += iterator->file_size(error);
        }
        error.clear();
        iterator.increment(error);
    }
    return total;
}

std::vector<std::string> collect_relative_files(const fs::path& root) {
    std::vector<std::string> files;
    std::error_code error;
    if (!fs::exists(root, error) || error) {
        return files;
    }

    fs::recursive_directory_iterator iterator(root, error);
    const fs::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error)) {
            files.push_back(
                iterator->path().lexically_relative(root).generic_string()
            );
        }
        error.clear();
        iterator.increment(error);
    }

    std::sort(files.begin(), files.end());
    return files;
}

bool stage_payload(
    const fs::path& project_root, const resolved_artifact& resolved,
    const fs::path& built_artifact_path, const fs::path& stage_root,
    const bool install_assets, std::map<fs::path, fs::path>* payload_sources,
    std::string* error_message
) {
    const std::string install_subdirectory
        = package_install_subdirectory(*resolved.artifact_value);
    if (install_subdirectory.empty()) {
        *error_message
            = "prerelease packaging does not support interface libraries";
        return false;
    }

    std::error_code error;
    const fs::path install_dir = stage_root / install_subdirectory;
    fs::create_directories(install_dir, error);
    if (error) {
        *error_message = "unable to create " + install_dir.string() + ": "
            + error.message();
        return false;
    }

    const fs::path destination = install_dir / built_artifact_path.filename();
    fs::copy_file(
        built_artifact_path, destination, fs::copy_options::none, error
    );
    if (error) {
        if (error == std::errc::file_exists) {
            *error_message
                = "package payload collision: " + destination.string() + " for "
                + format_artifact_ref(resolved.ref);
        } else {
            *error_message = "unable to copy " + built_artifact_path.string()
                + " to " + destination.string() + ": " + error.message();
        }
        return false;
    }

    if ((resolved.artifact_value->kind == "exe"
         || resolved.artifact_value->kind == "qt_app")
        && install_assets && project_has_assets_dir(project_root)
        && !copy_directory_tree(
            project_root / "assets", install_dir / "assets", error_message
        )) {
        return false;
    }

    payload_sources->emplace(destination, built_artifact_path);
    return true;
}

bool stage_tooling_templates(
    const fs::path& project_root, const manifest& value,
    const artifact_ref& primary, const fs::path& stage_root,
    std::string* error_message
) {
    if (value.id != "manifesto")
        return true;
    bool has_actor = false;
    for (const auto& ref : distribution_artifacts(value, primary)) {
        const auto item = resolve_artifact(value, ref);
        if (!item || item->artifact_value->kind != "exe")
            continue;
        const auto name = artifact_output_name(
            value, *item->component_value, *item->artifact_value
        );
        has_actor = has_actor || name == "marx" || name == "engels";
    }
    if (!has_actor)
        return true;

    // Packages have a fixed /usr/bin + /usr/share layout. Reject a differently
    // configured tooling build instead of shipping data its actors cannot find.
    if (const auto cache
        = load_cmake_cache(local_build_cache_path(project_root, "release"))) {
        auto directory
            = [&](const std::string& key, const std::string& fallback) {
                  const auto entry = cache->entries.find(key);
                  return entry == cache->entries.end() || entry->second.empty()
                      ? fallback
                      : entry->second;
              };
        if (directory("CMAKE_INSTALL_BINDIR", "bin") != "bin"
            || directory(
                   "CMAKE_INSTALL_DATADIR",
                   directory("CMAKE_INSTALL_DATAROOTDIR", "share")
               ) != "share") {
            *error_message
                = "tooling prerelease requires CMAKE_INSTALL_BINDIR=bin and "
                  "CMAKE_INSTALL_DATADIR=share; "
                  "use those directories in the release build or use cmake "
                  "--install for a custom layout";
            return false;
        }
    }

    const auto source = project_root / "templates";
    if (!fs::is_directory(source) || fs::is_symlink(source)) {
        *error_message
            = "tooling package requires its source template bundle at "
            + source.string();
        return false;
    }
    bool has_files = false;
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        if (entry.is_symlink()
            || (!entry.is_directory() && !entry.is_regular_file())) {
            *error_message = "unsupported tooling template entry: "
                + entry.path().string();
            return false;
        }
        has_files = has_files || entry.is_regular_file();
    }
    if (!has_files) {
        *error_message = "tooling template bundle is empty: " + source.string();
        return false;
    }
    // Ship the built project's data, independently of execution-time template
    // overrides and install_assets. Hidden tracked/ directories are included.
    return copy_directory_tree(
        source, stage_root / "usr/share/manifesto/templates", error_message
    );
}

} // namespace ecosystem::release_detail
