#include "internal.hpp"
#include "workspace/tooling.hpp"

#include "workspace/source_dependencies.hpp"
#include "workspace/sync.hpp"
#include "workspace/template_text.hpp"

#include <fstream>
#include <optional>

namespace fs = std::filesystem;

namespace ecosystem {
using namespace tooling_detail;

namespace {

    std::optional<std::string> cached_source(const fs::path& build_dir) {
        const fs::path cache_path = build_dir / "CMakeCache.txt";
        std::ifstream file(cache_path, std::ios::binary);
        if (!file.is_open()) {
            return std::nullopt;
        }

        std::string line;
        const std::string prefix = "CMAKE_HOME_DIRECTORY:INTERNAL=";
        while (std::getline(file, line)) {
            if (line.rfind(prefix, 0U) == 0U) {
                return line.substr(prefix.size());
            }
        }

        return std::nullopt;
    }

    std::string build_type(const std::string& profile) {
        return profile == "release" ? "Release" : "Debug";
    }

} // namespace

bool tooling_detail::reset_source(
    const fs::path& source_dir, const fs::path& build_dir,
    std::string* error_message
) {
    const std::optional<std::string> cached_home = cached_source(build_dir);
    if (!cached_home.has_value()) {
        return true;
    }

    const std::string normalized_source
        = source_dir.lexically_normal().string();
    const std::string normalized_cached_home
        = fs::path(*cached_home).lexically_normal().string();
    if (normalized_cached_home == normalized_source) {
        return true;
    }

    std::error_code error;
    fs::remove_all(build_dir, error);
    if (error) {
        set_error(
            error_message,
            "unable to reset " + build_dir.string() + ": " + error.message()
        );
        return false;
    }
    return true;
}

command_error configure_cmake_source_tree(
    const fs::path& source_dir, const fs::path& build_dir,
    const std::vector<std::string>& cmake_options,
    const std::string& build_type, std::string* error_message,
    const bool capture_output
) {
    const tool_status cmake_tool = probe_tool("cmake");
    if (!cmake_tool.available) {
        set_error(error_message, "cmake is not available");
        return command_error::missing_local_tooling;
    }
    const tool_status clang_tool = probe_tool("clang++");
    if (!clang_tool.available) {
        set_error(error_message, "clang++ is not available");
        return command_error::missing_local_tooling;
    }
    const tool_status clang_c_tool = probe_tool("clang");

    std::error_code fs_error;
    if (!reset_source(source_dir, build_dir, error_message)) {
        return command_error::task_failed;
    }
    fs::create_directories(build_dir, fs_error);
    if (fs_error) {
        set_error(error_message, fs_error.message());
        return command_error::task_failed;
    }

    std::vector<std::string> command {
        cmake_tool.path,     "-S",
        source_dir.string(), "-B",
        build_dir.string(),  std::string("-DCMAKE_BUILD_TYPE=") + build_type,
    };
    command.insert(command.end(), cmake_options.begin(), cmake_options.end());

    const std::vector<std::pair<std::string, std::string>> environment {
        { "CC", clang_c_tool.available ? clang_c_tool.path : clang_tool.path },
        { "CXX", clang_tool.path },
    };
    const auto result = capture_output
        ? capture_command_result(command, source_dir, environment)
        : captured_command { run_command(command, source_dir, environment),
                             {} };
    if (result.exit_code != 0) {
        set_error(error_message, "cmake configure failed\n" + result.output);
        return command_error::task_failed;
    }

    std::string ignored_error;
    if (!write_text_file(
            configure_stamp(build_dir), "configured\n", &ignored_error
        )) {
        set_error(error_message, ignored_error);
        return command_error::task_failed;
    }
    return command_error::ok;
}

command_error configure_build_tree(
    const fs::path& project_root, const manifest& manifest_value,
    const std::string& profile, const bool with_tests, const bool with_coverage,
    const bool with_benchmarks, std::string* error_message,
    const bool capture_output
) {
    ensure_local_artifacts(project_root, false, false);
    const command_error surface_status = ensure_local_developer_surface(
        project_root, manifest_value, error_message
    );
    if (surface_status != command_error::ok)
        return surface_status;
    string_list dependency_options;
    const auto dependency_status = prepare_source_dependencies(
        project_root, manifest_value, profile, &dependency_options,
        error_message
    );
    if (dependency_status != command_error::ok)
        return dependency_status;
    if (profile == "android") {
        return configure_android_source_tree(
            project_root, local_developer_source_dir(project_root),
            local_build_dir(project_root, profile),
            { std::string("-DECOSYSTEM_BUILD_TESTS=")
                  + (with_tests ? "ON" : "OFF"),
              std::string("-DECOSYSTEM_BUILD_BENCHMARKS=")
                  + (with_benchmarks ? "ON" : "OFF"),
              std::string("-DECOSYSTEM_ENABLE_COVERAGE=")
                  + (with_coverage ? "ON" : "OFF") },
            error_message, capture_output
        );
    }

    dependency_options.insert(
        dependency_options.end(),
        { "-DECOSYSTEM_PROJECT_ROOT:PATH=" + project_root.string(),
          std::string("-DECOSYSTEM_BUILD_TESTS=") + (with_tests ? "ON" : "OFF"),
          std::string("-DECOSYSTEM_BUILD_BENCHMARKS=")
              + (with_benchmarks ? "ON" : "OFF"),
          std::string("-DECOSYSTEM_ENABLE_COVERAGE=")
              + (with_coverage ? "ON" : "OFF"),
          std::string("-DECOSYSTEM_PROFILE_KDE=")
              + (profile == "kde" ? "ON" : "OFF"),
          "-DECOSYSTEM_PROFILE_ANDROID=OFF" }
    );
    return configure_cmake_source_tree(
        local_developer_source_dir(project_root),
        local_build_dir(project_root, profile), dependency_options,
        build_type(profile), error_message, capture_output
    );
}

command_error ensure_local_developer_surface(
    const fs::path& project_root, const manifest& manifest_value,
    std::string* error_message
) try {
    const fs::path cmake_path = local_developer_cmakelists_path(project_root);
    const std::string generated_cmake
        = generate_developer_cmakelists(manifest_value, project_root);
    ensure_local_artifacts(project_root, false, false);
    std::string read_error;
    const std::string current_contents
        = read_text_file(cmake_path, &read_error);
    if (read_error.empty() && current_contents == generated_cmake) {
        return command_error::ok;
    }

    if (!write_text_file(cmake_path, generated_cmake, error_message)) {
        return command_error::task_failed;
    }
    return command_error::ok;
} catch (const template_render_error& error) {

    set_error(error_message, error.what());
    return command_error::task_failed;
}

} // namespace ecosystem
