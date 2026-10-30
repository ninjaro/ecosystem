#include "internal.hpp"
#include "workspace/assets.hpp"

#include "packages/package_cache.hpp"

#include <algorithm>
#include <fstream>

namespace ecosystem::release_detail {
namespace {

    bool copy_runtime_files(
        const fs::path& project_root,
        const std::vector<std::string>& categories, const fs::path& source_base,
        const fs::path& destination, std::string* error_message
    ) {
        try {
            const auto base = fs::absolute(source_base).lexically_normal();
            for (const auto& file :
                 runtime_asset_files(project_root, categories)) {
                const auto output = destination / file.lexically_relative(base);
                fs::create_directories(output.parent_path());
                // Materialize safe source aliases, rather than shipping links
                // which still depend on a source-directory layout.
                fs::copy_file(
                    file, output, fs::copy_options::overwrite_existing
                );
            }
            return true;
        } catch (const std::exception& error) {
            *error_message = error.what();
            return false;
        }
    }

} // namespace

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
        && install_assets && project_has_assets_dir(project_root)) {
        if (!copy_runtime_files(
                project_root, runtime_asset_categories(),
                project_root / "assets", install_dir / "assets", error_message
            ))
            return false;
    }

    payload_sources->emplace(destination, built_artifact_path);
    return true;
}

bool collect_tooling_templates(
    const fs::path& project_root, const manifest& value,
    const artifact_ref& primary, std::vector<fs::path>* files,
    std::string* error_message
) {
    files->clear();
    if (value.id != "ecosystem")
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

    const auto source = project_root / "assets/templates";
    if (!fs::is_directory(source) || fs::is_symlink(source)) {
        *error_message
            = "tooling package requires its source template bundle at "
            + source.string();
        return false;
    }
    bool has_files = false;
    const std::regex cache_pattern(runtime_asset_cache_pattern());
    auto iterator = fs::recursive_directory_iterator(source);
    for (; iterator != fs::recursive_directory_iterator(); ++iterator) {
        const auto& entry = *iterator;
        if (entry.is_directory()
            && std::regex_search(
                entry.path().filename().generic_string(), cache_pattern
            )) {
            iterator.disable_recursion_pending();
            continue;
        }
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
    *files = runtime_asset_files(project_root, { "templates" });
    return true;
}

bool stage_tooling_templates(
    const fs::path& project_root, const manifest& value,
    const artifact_ref& primary, const fs::path& stage_root,
    std::string* error_message
) {
    std::vector<fs::path> files;
    if (!collect_tooling_templates(
            project_root, value, primary, &files, error_message
        ))
        return false;
    if (files.empty())
        return true;
    const auto source = project_root / "assets/templates";
    // Ship the built project's data, independently of execution-time template
    // overrides and install_assets. Hidden tracked/ directories are included.
    const auto destination = stage_root / "usr/share/ecosystem/templates";
    if (!ensure_clean_directory(destination, error_message))
        return false;
    return copy_runtime_files(
        project_root, { "templates" }, source, destination, error_message
    );
}

command_error stage_distribution(
    const fs::path& project_root, const manifest& value,
    const resolved_artifact& primary, const fs::path& built_primary,
    const fs::path& work_dir, staged_distribution* staged,
    std::string* error_message
) {
    *staged = {};
    const auto payload = work_dir / "payload";
    std::map<fs::path, fs::path> sources;
    if (!ensure_clean_directory(payload, error_message))
        return command_error::task_failed;
    for (const auto& ref : distribution_artifacts(value, primary.ref)) {
        const auto item = resolve_artifact(value, ref);
        if (!item) {
            *error_message
                = "unresolved install artifact: " + format_artifact_ref(ref);
            return command_error::invalid_request;
        }
        const auto path
            = format_artifact_ref(ref) == format_artifact_ref(primary.ref)
            ? std::optional<fs::path>(built_primary)
            : artifact_output_path(
                  local_build_dir(project_root, "release"),
                  *item->artifact_value,
                  artifact_output_name(
                      value, *item->component_value, *item->artifact_value
                  )
              );
        if (!path) {
            *error_message
                = "missing install artifact: " + format_artifact_ref(ref);
            return command_error::task_failed;
        }
        if (!stage_payload(
                project_root, *item, *path, payload, value.install_assets,
                &sources, error_message
            ))
            return command_error::task_failed;
        staged->artifact_files.emplace(
            payload / package_install_subdirectory(*item->artifact_value)
                / path->filename(),
            ref
        );
    }
    if (!stage_tooling_templates(
            project_root, value, primary.ref, payload, error_message
        )
        || !stage_runtime(
            project_root, payload, work_dir, &sources,
            &staged->system_libraries, error_message
        ))
        return command_error::task_failed;
    return command_error::ok;
}

} // namespace ecosystem::release_detail
