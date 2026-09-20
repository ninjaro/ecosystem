#include "workspace/doctor.hpp"

#include "packages/package_surface.hpp"
#include "workspace/project.hpp"
#include "workspace/template_text.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace ecosystem {
namespace doctor_support {

    std::vector<fs::path>
    project_cache_freshness_inputs(const fs::path& project_root) {
        return {
            project_root / "manifest.json",
            local_developer_cmakelists_path(project_root),
        };
    }

    fs::path local_doctor_probe_root(
        const fs::path& project_root,
        const std::optional<artifact_ref>& requested_artifact
    ) {
        return local_doctor_dir(project_root)
            / doctor_probe_scope_key(requested_artifact);
    }

    fs::path local_doctor_probe_source_dir(
        const fs::path& project_root,
        const std::optional<artifact_ref>& requested_artifact
    ) {
        return local_doctor_probe_root(project_root, requested_artifact)
            / "source";
    }

    fs::path local_doctor_probe_build_dir(
        const fs::path& project_root,
        const std::optional<artifact_ref>& requested_artifact,
        const std::string& profile
    ) {
        const build_tree_layout layout = describe_build_tree_layout(profile);
        return local_doctor_probe_root(project_root, requested_artifact)
            / layout.platform / layout.configuration / layout.variant;
    }

    std::vector<fs::path> doctor_probe_freshness_inputs(
        const fs::path& project_root, const fs::path& source_dir
    ) {
        return {
            project_root / "manifest.json",
            source_dir / "CMakeLists.txt",
        };
    }

    std::string generate_doctor_probe_cmakelists(
        const manifest& manifest_value, const dependency_summary& dependencies,
        std::string* error_message
    ) try {
        package_surface_options options;
        options.emit_profile_option_lines = true;
        return render_text_template(
            "cmake/doctor_probe.tpl",
            {
                { "cpp_standard",
                  std::to_string(manifest_value.cpp_standard) },
                { "package_surface",
                  render_package_surface(dependencies, options) },
            },
            error_message
        );
    } catch (const template_render_error& error) {

        *error_message = error.what();
        return {};
    }

    bool ensure_probe_surface(
        const fs::path& source_dir, const std::string& cmake_contents,
        std::string* error_message
    ) {
        const fs::path cmake_path = source_dir / "CMakeLists.txt";
        std::string read_error;
        const std::string current_contents
            = read_text_file(cmake_path, &read_error);
        if (read_error.empty() && current_contents == cmake_contents) {
            return true;
        }
        return write_text_file(cmake_path, cmake_contents, error_message);
    }

    doctor_cache_refresh_result refresh_scoped_doctor_probe_cache(
        const fs::path& project_root, const manifest& manifest_value,
        const dependency_summary& dependencies,
        const std::optional<artifact_ref>& requested_artifact,
        const std::string& cache_profile
    ) {
        doctor_cache_refresh_result result;
        const fs::path source_dir
            = local_doctor_probe_source_dir(project_root, requested_artifact);
        const fs::path build_dir = local_doctor_probe_build_dir(
            project_root, requested_artifact, cache_profile
        );
        const std::string cmake_contents
            = generate_doctor_probe_cmakelists(
                manifest_value, dependencies, &result.error_message
            );
        if (!result.error_message.empty()) {
            result.status = command_error::task_failed;
            result.cache_status = inspect_cache_file(
                project_root, build_dir / "CMakeCache.txt",
                doctor_probe_freshness_inputs(project_root, source_dir)
            );
            return result;
        }
        if (!ensure_probe_surface(
                source_dir, cmake_contents, &result.error_message
            )) {
            result.status = command_error::task_failed;
            result.cache_status = inspect_cache_file(
                project_root, build_dir / "CMakeCache.txt",
                doctor_probe_freshness_inputs(project_root, source_dir)
            );
            return result;
        }

        result.cache_status = inspect_cache_file(
            project_root, build_dir / "CMakeCache.txt",
            doctor_probe_freshness_inputs(project_root, source_dir)
        );
        if (result.cache_status.exists && !result.cache_status.stale
            && cache_profile != "android") {
            return result;
        }

        std::vector<std::string> configure_options;
        if (dependencies.kde.enabled || dependencies.kdegames.enabled) {
            configure_options.push_back(
                std::string("-DECOSYSTEM_PROFILE_KDE=")
                + (cache_profile == "kde" ? "ON" : "OFF")
            );
        }
        result.status = cache_profile == "android"
            ? configure_android_source_tree(
                  project_root, source_dir, build_dir, configure_options,
                  &result.error_message
              )
            : configure_cmake_source_tree(
                  source_dir, build_dir, configure_options,
                  cache_profile == "release" ? "Release" : "Debug",
                  &result.error_message
              );
        result.cache_status = inspect_cache_file(
            project_root, build_dir / "CMakeCache.txt",
            doctor_probe_freshness_inputs(project_root, source_dir)
        );
        result.refreshed = result.status == command_error::ok
            && result.cache_status.exists && !result.cache_status.stale;
        return result;
    }

} // namespace doctor_support

using namespace doctor_support;

std::string doctor_cache_profile(const std::optional<std::string>& profile) {
    if (!profile.has_value()) {
        return "debug";
    }
    if (*profile == "debug" || *profile == "release" || *profile == "android"
        || *profile == "kde" || *profile == "coverage") {
        return *profile;
    }
    return "debug";
}

fs::path local_doctor_dir(const fs::path& project_root) {
    return local_build_scope_dir(project_root, "probes");
}

std::string
doctor_probe_scope_key(const std::optional<artifact_ref>& requested_artifact) {
    if (!requested_artifact.has_value()) {
        return "project";
    }
    return requested_artifact->component_id + "__"
        + requested_artifact->artifact_id;
}

doctor_cache_refresh_result refresh_doctor_cache(
    const fs::path& project_root, const manifest& manifest_value,
    const dependency_summary& dependencies,
    const std::optional<artifact_ref>& requested_artifact,
    const std::optional<std::string>& profile
) {
    doctor_cache_refresh_result result;
    result.status = ensure_local_developer_surface(
        project_root, manifest_value, &result.error_message
    );
    if (result.status != command_error::ok) {
        result.cache_status = inspect_build_cache(
            project_root, doctor_cache_profile(profile),
            project_cache_freshness_inputs(project_root)
        );
        return result;
    }

    const std::string cache_profile = doctor_cache_profile(profile);
    result.cache_status = inspect_build_cache(
        project_root, cache_profile,
        project_cache_freshness_inputs(project_root)
    );
    if (!dependency_summary_has_entries(dependencies)) {
        return result;
    }
    if (result.cache_status.exists && !result.cache_status.stale
        && cache_profile != "android") {
        return result;
    }
    if (requested_artifact.has_value()) {
        return refresh_scoped_doctor_probe_cache(
            project_root, manifest_value, dependencies, requested_artifact,
            cache_profile
        );
    }

    result.status = configure_build_tree(
        project_root, manifest_value, cache_profile,
        has_tests_enabled(manifest_value), cache_profile == "coverage",
        has_benchmarks_enabled(manifest_value), &result.error_message
    );
    result.cache_status = inspect_build_cache(
        project_root, cache_profile,
        project_cache_freshness_inputs(project_root)
    );
    result.refreshed = result.status == command_error::ok
        && result.cache_status.exists && !result.cache_status.stale;
    return result;
}

} // namespace ecosystem
