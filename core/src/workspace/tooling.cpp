#include "workspace/tooling.hpp"
#include "tooling/internal.hpp"

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace ecosystem {
using namespace tooling_detail;

namespace {

    std::string
    display_path(const fs::path& project_root, const fs::path& value) {
        const fs::path relative = value.lexically_relative(project_root);
        if (!relative.empty()) {
            return relative.generic_string();
        }
        return value.generic_string();
    }

} // namespace

int exit_code(const command_error error_class) {
    return static_cast<int>(error_class);
}

fs::path local_state_dir(const fs::path& project_root) {
    return project_root / ".ecosystem";
}

fs::path local_developer_source_dir(const fs::path& project_root) {
    return local_state_dir(project_root) / "source";
}

fs::path local_developer_cmakelists_path(const fs::path& project_root) {
    return local_developer_source_dir(project_root) / "CMakeLists.txt";
}

build_tree_layout describe_build_tree_layout(
    const std::string& profile, const std::string& scope
) {
    build_tree_layout layout;
    layout.scope = scope;
    layout.platform = "desktop";
    layout.configuration = "debug";
    layout.variant = "default";

    if (profile == "release") {
        layout.configuration = "release";
        return layout;
    }
    if (profile == "kde") {
        layout.variant = "kde";
        return layout;
    }
    if (profile == "coverage") {
        layout.variant = "coverage";
        return layout;
    }
    if (profile == "leaks") {
        layout.variant = "leaks";
        return layout;
    }
    if (profile == "android") {
        layout.platform = "android";
        return layout;
    }
    return layout;
}

fs::path local_build_root(const fs::path& project_root) {
    return local_state_dir(project_root) / "build";
}

fs::path
local_build_scope_dir(const fs::path& project_root, const std::string& scope) {
    return local_build_root(project_root) / scope;
}

fs::path
local_build_dir(const fs::path& project_root, const std::string& profile) {
    const build_tree_layout layout = describe_build_tree_layout(profile);
    return local_build_scope_dir(project_root, layout.scope) / layout.platform
        / layout.configuration / layout.variant;
}

fs::path local_build_cache_path(
    const fs::path& project_root, const std::string& profile
) {
    return local_build_dir(project_root, profile) / "CMakeCache.txt";
}

fs::path local_report_dir(const fs::path& project_root) {
    return local_state_dir(project_root) / "reports";
}

fs::path local_benchmark_dir(const fs::path& project_root) {
    return local_report_dir(project_root) / "benchmark";
}

fs::path local_sphinx_dir(const fs::path& project_root) {
    return local_state_dir(project_root) / "sphinx";
}

build_cache_status inspect_build_cache(
    const fs::path& project_root, const std::string& profile,
    const std::vector<fs::path>& freshness_inputs
) {
    return inspect_cache_file(
        project_root, local_build_cache_path(project_root, profile),
        freshness_inputs
    );
}

build_cache_status inspect_cache_file(
    const fs::path& project_root, const fs::path& cache_path,
    const std::vector<fs::path>& freshness_inputs
) {
    build_cache_status status;
    status.cache_path = cache_path;

    std::error_code error;
    status.exists = fs::exists(status.cache_path, error);
    if (error || !status.exists) {
        status.exists = false;
        return status;
    }

    fs::path freshness_reference = status.cache_path;
    const fs::path stamp_path
        = configure_stamp(status.cache_path.parent_path());
    if (fs::exists(stamp_path, error) && !error) {
        freshness_reference = stamp_path;
    } else {
        error.clear();
    }

    const fs::file_time_type cache_time
        = fs::last_write_time(freshness_reference, error);
    if (error) {
        status.exists = false;
        return status;
    }

    for (const fs::path& freshness_input : freshness_inputs) {
        if (!fs::exists(freshness_input, error) || error) {
            error.clear();
            continue;
        }

        const fs::file_time_type input_time
            = fs::last_write_time(freshness_input, error);
        if (error) {
            error.clear();
            continue;
        }

        if (input_time > cache_time) {
            status.stale = true;
            status.stale_reasons.push_back(
                display_path(project_root, freshness_input) + " is newer than "
                + display_path(project_root, status.cache_path)
            );
        }
    }

    return status;
}

bool write_text_file(
    const fs::path& path, const std::string& contents,
    std::string* error_message
) {
    std::error_code error;
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path(), error);
        if (error) {
            *error_message = "unable to create " + path.parent_path().string()
                + ": " + error.message();
            return false;
        }
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        *error_message = "unable to open " + path.string();
        return false;
    }
    file << contents;
    file.close();
    if (!file) {
        *error_message = "unable to write " + path.string();
        return false;
    }
    return true;
}

std::string read_text_file(const fs::path& path, std::string* error_message) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        *error_message = "unable to open " + path.string();
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void ensure_local_artifacts(
    const fs::path& project_root, const bool with_clang_format,
    const bool with_clang_tidy
) {
    const fs::path state_root = local_state_dir(project_root);
    fs::create_directories(state_root / "source");
    fs::create_directories(state_root / "build");
    fs::create_directories(state_root / "reports");

    static_cast<void>(with_clang_format);
    static_cast<void>(with_clang_tidy);
    // Optional root style files may be generated by `marx sync`.
    // Checks use canonical configuration when absent, without recreating them.
    // Build/check/doctor/report only materialize ignored local artifacts.
}

} // namespace ecosystem
