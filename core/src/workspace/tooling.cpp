#include "workspace/tooling.hpp"

#include "workspace/project.hpp"
#include "workspace/source_dependencies.hpp"
#include "workspace/sync.hpp"
#include "workspace/template_text.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ecosystem {
namespace tooling_support {

    std::string trim_copy(const std::string& value) {
        const std::size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }
        const std::size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1U);
    }

    std::string first_line(const std::string& value) {
        const std::size_t separator = value.find('\n');
        return trim_copy(value.substr(0U, separator));
    }

    std::string shell_quote(const std::string& value) {
        std::string escaped = "'";
        for (const char character : value) {
            if (character == '\'') {
                escaped += "'\"'\"'";
            } else {
                escaped.push_back(character);
            }
        }
        escaped.push_back('\'');
        return escaped;
    }

    std::string build_shell_command(
        const std::vector<std::string>& args, const fs::path& working_directory,
        const std::vector<std::pair<std::string, std::string>>& environment
    ) {
        std::ostringstream stream;
        if (!working_directory.empty()) {
            stream << "cd " << shell_quote(working_directory.string())
                   << " && ";
        }
        for (const auto& [name, value] : environment) {
            stream << name << "=" << shell_quote(value) << " ";
        }
        for (std::size_t index = 0; index < args.size(); ++index) {
            if (index > 0U) {
                stream << " ";
            }
            stream << shell_quote(args[index]);
        }
        return stream.str();
    }

    std::string
    display_path(const fs::path& project_root, const fs::path& value) {
        const fs::path relative = value.lexically_relative(project_root);
        if (!relative.empty()) {
            return relative.generic_string();
        }
        return value.generic_string();
    }

    void assign_error(std::string* error_message, const std::string& message) {
        if (error_message != nullptr) {
            *error_message = message;
        }
    }

    bool render_tooling_template(
        const fs::path& relative_path, const template_bindings& bindings,
        std::string* contents, std::string* error_message
    ) {
        *contents
            = render_text_template(relative_path, bindings, error_message);
        return error_message == nullptr || error_message->empty();
    }

    bool render_tooling_template_candidates(
        const std::vector<fs::path>& relative_paths,
        const template_bindings& bindings, std::string* contents,
        std::string* error_message
    ) {
        *contents = render_text_template_candidates(
            relative_paths, bindings, error_message
        );
        return error_message == nullptr || error_message->empty();
    }

    bool write_template_artifact(
        const fs::path& path, const fs::path& template_path,
        const template_bindings& bindings, std::string* error_message
    ) {
        std::string contents;
        if (!render_tooling_template(
                template_path, bindings, &contents, error_message
            )) {
            return false;
        }
        return write_text_file(path, contents, error_message);
    }

    bool write_template_artifact_candidates(
        const fs::path& path, const std::vector<fs::path>& relative_paths,
        const template_bindings& bindings, std::string* error_message
    ) {
        std::string contents;
        if (!render_tooling_template_candidates(
                relative_paths, bindings, &contents, error_message
            )) {
            return false;
        }
        return write_text_file(path, contents, error_message);
    }

    std::string env_or_empty(const char* name) {
        const char* value = std::getenv(name);
        return value == nullptr ? std::string() : std::string(value);
    }

    bool directory_exists(const fs::path& path) {
        if (path.empty()) {
            return false;
        }
        std::error_code error;
        return fs::exists(path, error) && !error
            && fs::is_directory(path, error);
    }

    bool executable_exists(const fs::path& path) {
        return !path.empty() && ::access(path.c_str(), X_OK) == 0;
    }

    bool directory_tree_has_extension(
        const fs::path& root, const std::string& extension
    ) {
        std::error_code error;
        if (!fs::exists(root, error) || error) {
            return false;
        }

        fs::recursive_directory_iterator iterator(root, error);
        const fs::recursive_directory_iterator end;
        while (!error && iterator != end) {
            if (iterator->is_regular_file(error)
                && iterator->path().extension().generic_string() == extension) {
                return true;
            }

            error.clear();
            iterator.increment(error);
        }

        return false;
    }

    std::string default_home_path(const fs::path& relative) {
        const std::string home = env_or_empty("HOME");
        return home.empty() ? std::string()
                            : (fs::path(home) / relative).string();
    }

    std::optional<std::string> detect_android_sdk_root_impl() {
        for (const std::string& candidate : {
                 env_or_empty("ANDROID_SDK_ROOT"),
                 env_or_empty("ANDROID_HOME"),
                 default_home_path(fs::path("Android") / "Sdk"),
                 std::string("/opt/android-sdk"),
             }) {
            if (directory_exists(candidate)) {
                return candidate;
            }
        }

        for (const std::string& tool :
             { find_command_path("adb"), find_command_path("emulator") }) {
            if (!tool.empty()) {
                const fs::path root
                    = fs::path(tool).parent_path().parent_path();
                if (directory_exists(root)) {
                    return root.string();
                }
            }
        }

        return std::nullopt;
    }

    std::string detect_android_adb_impl(const std::string& sdk_root) {
        const std::string adb_override = env_or_empty("ADB_BIN");
        if (executable_exists(adb_override)) {
            return adb_override;
        }

        if (!sdk_root.empty()) {
            const fs::path sdk_adb
                = fs::path(sdk_root) / "platform-tools" / "adb";
            if (executable_exists(sdk_adb)) {
                return sdk_adb.string();
            }
        }

        return find_command_path("adb");
    }

    std::string detect_android_emulator_impl(const std::string& sdk_root) {
        const std::string emulator_override
            = env_or_empty("ANDROID_EMULATOR_BIN");
        if (executable_exists(emulator_override)) {
            return emulator_override;
        }

        if (!sdk_root.empty()) {
            const fs::path sdk_emulator
                = fs::path(sdk_root) / "emulator" / "emulator";
            if (executable_exists(sdk_emulator)) {
                return sdk_emulator.string();
            }
        }

        return find_command_path("emulator");
    }

    std::string detect_android_aapt_impl(
        const std::string& sdk_root, const std::string& build_tools_version
    ) {
        const std::string aapt_override = env_or_empty("AAPT_BIN");
        if (executable_exists(aapt_override)) {
            return aapt_override;
        }

        if (!sdk_root.empty()) {
            if (!build_tools_version.empty()) {
                const fs::path preferred = fs::path(sdk_root) / "build-tools"
                    / build_tools_version / "aapt";
                if (executable_exists(preferred)) {
                    return preferred.string();
                }
            }

            const fs::path build_tools_dir = fs::path(sdk_root) / "build-tools";
            std::error_code error;
            if (fs::exists(build_tools_dir, error) && !error) {
                fs::recursive_directory_iterator iterator(
                    build_tools_dir, error
                );
                const fs::recursive_directory_iterator end;
                while (!error && iterator != end) {
                    if (iterator->is_regular_file(error)
                        && iterator->path().filename().generic_string()
                            == "aapt"
                        && executable_exists(iterator->path())) {
                        return iterator->path().string();
                    }
                    error.clear();
                    iterator.increment(error);
                }
            }
        }

        return find_command_path("aapt");
    }

    fs::path configure_stamp_path(const fs::path& build_dir) {
        return build_dir / ".ecosystem_configured.stamp";
    }

    std::optional<std::string>
    cached_cmake_home_directory(const fs::path& build_dir) {
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

    bool reset_build_dir_for_source_change(
        const fs::path& source_dir, const fs::path& build_dir,
        std::string* error_message
    ) {
        const std::optional<std::string> cached_home
            = cached_cmake_home_directory(build_dir);
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
            assign_error(
                error_message,
                "unable to reset " + build_dir.string() + ": " + error.message()
            );
            return false;
        }
        return true;
    }

    std::string cmake_build_type_for_profile(const std::string& profile) {
        return profile == "release" ? "Release" : "Debug";
    }

} // namespace tooling_support

using namespace tooling_support;

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
        = configure_stamp_path(status.cache_path.parent_path());
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

bool command_exists(const std::string& name) {
    return !find_command_path(name).empty();
}

std::string find_command_path(const std::string& name) {
    if (name.find('/') != std::string::npos) {
        return ::access(name.c_str(), X_OK) == 0 ? name : std::string();
    }

    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return {};
    }

    std::stringstream stream(path_env);
    std::string directory;
    while (std::getline(stream, directory, ':')) {
        const fs::path candidate = fs::path(directory) / name;
        if (::access(candidate.c_str(), X_OK) == 0) {
            return candidate.string();
        }
    }
    return {};
}

std::string capture_command(
    const std::vector<std::string>& args, const fs::path& working_directory
) {
    return capture_command_result(args, working_directory).output;
}

captured_command capture_command_result(
    const std::vector<std::string>& args, const fs::path& working_directory,
    const std::vector<std::pair<std::string, std::string>>& environment
) {
    captured_command result;
    const std::string command
        = build_shell_command(args, working_directory, environment) + " 2>&1";
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return result;
    }

    char buffer[256];
    while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), pipe)
           != nullptr) {
        result.output += buffer;
    }
    const int status = ::pclose(pipe);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
    return result;
}

int run_command(
    const std::vector<std::string>& args, const fs::path& working_directory,
    const std::vector<std::pair<std::string, std::string>>& environment
) {
    const std::string command
        = build_shell_command(args, working_directory, environment);
    const int status = std::system(command.c_str());
    if (status == -1) {
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return status;
}

command_error configure_cmake_source_tree(
    const fs::path& source_dir, const fs::path& build_dir,
    const std::vector<std::string>& cmake_options,
    const std::string& build_type, std::string* error_message,
    const bool capture_output
) {
    const tool_status cmake_tool = probe_tool("cmake");
    if (!cmake_tool.available) {
        assign_error(error_message, "cmake is not available");
        return command_error::missing_local_tooling;
    }
    const tool_status clang_tool = probe_tool("clang++");
    if (!clang_tool.available) {
        assign_error(error_message, "clang++ is not available");
        return command_error::missing_local_tooling;
    }
    const tool_status clang_c_tool = probe_tool("clang");

    std::error_code fs_error;
    if (!reset_build_dir_for_source_change(
            source_dir, build_dir, error_message
        )) {
        return command_error::task_failed;
    }
    fs::create_directories(build_dir, fs_error);
    if (fs_error) {
        assign_error(error_message, fs_error.message());
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
        assign_error(error_message, "cmake configure failed\n" + result.output);
        return command_error::task_failed;
    }

    std::string ignored_error;
    if (!write_text_file(
            configure_stamp_path(build_dir), "configured\n", &ignored_error
        )) {
        assign_error(error_message, ignored_error);
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
    if (surface_status != command_error::ok) {
        return surface_status;
    }

    string_list dependency_options;
    const auto dependency_status = prepare_source_dependencies(
        project_root, manifest_value, profile, &dependency_options,
        error_message
    );
    if (dependency_status != command_error::ok)
        return dependency_status;

    if (profile == "android") {
        const android_environment environment = detect_android_environment();
        if (!executable_exists(environment.qt_cmake_bin)) {
            assign_error(
                error_message,
                "android configure requires a Qt Android qt-cmake binary; "
                "set ANDROID_CMAKE_BIN or QT_DIR/QT_VER/ANDROID_QT_ARCH"
            );
            return command_error::missing_local_tooling;
        }
        if (!directory_exists(environment.sdk_root)) {
            assign_error(
                error_message,
                "android configure requires ANDROID_SDK_ROOT (or "
                "ANDROID_HOME) to point at an installed Android SDK"
            );
            return command_error::missing_local_tooling;
        }
        if (!directory_exists(environment.ndk_root)) {
            assign_error(
                error_message,
                "android configure requires ANDROID_NDK_ROOT (or "
                "ANDROID_NDK_VERSION under the Android SDK)"
            );
            return command_error::missing_local_tooling;
        }

        std::error_code fs_error;
        const fs::path build_dir = local_build_dir(project_root, profile);
        if (!reset_build_dir_for_source_change(
                local_developer_source_dir(project_root), build_dir,
                error_message
            )) {
            return command_error::task_failed;
        }
        fs::create_directories(build_dir, fs_error);
        if (fs_error) {
            assign_error(error_message, fs_error.message());
            return command_error::task_failed;
        }

        std::vector<std::string> command {
            environment.qt_cmake_bin,
            "-S",
            local_developer_source_dir(project_root).string(),
            "-B",
            build_dir.string(),
            std::string("-DCMAKE_BUILD_TYPE=")
                + cmake_build_type_for_profile(profile),
            std::string("-DQT_HOST_PATH=") + environment.qt_host_path,
            std::string("-DANDROID_ABI=")
                + (env_or_empty("ANDROID_ABI").empty()
                       ? std::string("x86_64")
                       : env_or_empty("ANDROID_ABI")),
            std::string("-DANDROID_PLATFORM=") + environment.platform,
            std::string("-DANDROID_SDK_ROOT=") + environment.sdk_root,
            std::string("-DANDROID_NDK=") + environment.ndk_root,
            std::string("-DECOSYSTEM_BUILD_TESTS=")
                + (with_tests ? "ON" : "OFF"),
            std::string("-DECOSYSTEM_BUILD_BENCHMARKS=")
                + (with_benchmarks ? "ON" : "OFF"),
            std::string("-DECOSYSTEM_ENABLE_COVERAGE=")
                + (with_coverage ? "ON" : "OFF"),
            "-DECOSYSTEM_PROFILE_KDE=OFF",
            "-DECOSYSTEM_PROFILE_ANDROID=ON",
        };

        const auto result = capture_output
            ? capture_command_result(
                  command, local_developer_source_dir(project_root)
              )
            : captured_command { run_command(
                                     command,
                                     local_developer_source_dir(project_root)
                                 ),
                                 {} };
        if (result.exit_code != 0) {
            assign_error(
                error_message, "cmake configure failed\n" + result.output
            );
            return command_error::task_failed;
        }

        std::string ignored_error;
        if (!write_text_file(
                configure_stamp_path(build_dir), "configured\n", &ignored_error
            )) {
            assign_error(error_message, ignored_error);
            return command_error::task_failed;
        }
        return command_error::ok;
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
        cmake_build_type_for_profile(profile), error_message, capture_output
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

    assign_error(error_message, error.what());
    return command_error::task_failed;
}

tool_status probe_tool(
    const std::string& name, const std::vector<std::string>& version_args
) {
    tool_status status;
    status.name = name;
    status.path = find_command_path(name);
    status.available = !status.path.empty();
    if (!status.available) {
        return status;
    }

    std::vector<std::string> args { status.path };
    args.insert(args.end(), version_args.begin(), version_args.end());
    status.version = first_line(capture_command(args));
    return status;
}

android_environment detect_android_environment() {
    android_environment environment;

    const std::optional<std::string> detected_sdk_root
        = detect_android_sdk_root_impl();
    environment.sdk_root = env_or_empty("ANDROID_SDK_ROOT");
    if (environment.sdk_root.empty()) {
        environment.sdk_root = env_or_empty("ANDROID_HOME");
    }
    if (environment.sdk_root.empty() && detected_sdk_root.has_value()) {
        environment.sdk_root = *detected_sdk_root;
    }
    if (environment.sdk_root.empty()) {
        environment.sdk_root = default_home_path(fs::path("Android") / "Sdk");
    }

    environment.ndk_version = env_or_empty("ANDROID_NDK_VERSION");
    if (environment.ndk_version.empty()) {
        environment.ndk_version = "27.2.12479018";
    }
    environment.ndk_root = env_or_empty("ANDROID_NDK_ROOT");
    if (environment.ndk_root.empty() && !environment.sdk_root.empty()) {
        environment.ndk_root
            = (fs::path(environment.sdk_root) / "ndk" / environment.ndk_version)
                  .string();
    }

    environment.platform = env_or_empty("ANDROID_PLATFORM");
    if (environment.platform.empty()) {
        environment.platform = "android-24";
    }

    environment.build_tools_version
        = env_or_empty("ANDROID_BUILD_TOOLS_VERSION");
    if (environment.build_tools_version.empty()) {
        environment.build_tools_version = "34.0.0";
    }

    environment.qt_dir = env_or_empty("QT_DIR");
    if (environment.qt_dir.empty()) {
        environment.qt_dir = default_home_path("Qt");
    }

    environment.qt_version = env_or_empty("QT_VER");
    if (environment.qt_version.empty()) {
        environment.qt_version = "6.8.2";
    }

    environment.qt_host_path = env_or_empty("QT_HOST_PATH");
    if (environment.qt_host_path.empty() && !environment.qt_dir.empty()) {
        environment.qt_host_path
            = (fs::path(environment.qt_dir) / environment.qt_version / "gcc_64")
                  .string();
    }

    environment.qt_arch = env_or_empty("ANDROID_QT_ARCH");
    if (environment.qt_arch.empty()) {
        environment.qt_arch = "android_x86_64";
    }

    environment.qt_cmake_bin = env_or_empty("ANDROID_CMAKE_BIN");
    if (environment.qt_cmake_bin.empty() && !environment.qt_dir.empty()) {
        environment.qt_cmake_bin
            = (fs::path(environment.qt_dir) / environment.qt_version
               / environment.qt_arch / "bin" / "qt-cmake")
                  .string();
    }

    environment.emulator_bin
        = detect_android_emulator_impl(environment.sdk_root);
    environment.avd_name = env_or_empty("ANDROID_AVD_NAME");
    if (environment.avd_name.empty()) {
        environment.avd_name = "api35_x86_64";
    }

    environment.adb_bin = detect_android_adb_impl(environment.sdk_root);
    environment.aapt_bin = detect_android_aapt_impl(
        environment.sdk_root, environment.build_tools_version
    );

    return environment;
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
    // Tracked style surfaces are owned by `marx sync`;
    // build/check/doctor/report only materialize ignored local artifacts.
}

bool write_local_sphinx_conf(
    const fs::path& project_root, const std::string& project_name,
    std::string* error_message
) {
    const fs::path docs_dir = project_root / "docs";
    const bool has_markdown_docs
        = directory_tree_has_extension(docs_dir, ".md");
    std::string contents;
    if (!render_tooling_template(
            "tooling/sphinx_conf.py.tpl",
            {
                { "project_name", project_name },
                { "extensions",
                  has_markdown_docs ? "    'myst_parser',\n" : std::string() },
                { "source_suffix_markdown",
                  has_markdown_docs ? "    '.md': 'markdown',\n"
                                    : std::string() },
            },
            &contents, error_message
        )) {
        return false;
    }

    return write_text_file(
        local_sphinx_dir(project_root) / "conf.py", contents, error_message
    );
}

json toolchains_report() {
    const std::vector<std::string> tools {
        "cmake",   "ctest",        "clang++",  "clang-format",
        "doxygen", "sphinx-build", "llvm-cov", "llvm-profdata",
    };

    json report = json::object();
    for (const std::string& tool : tools) {
        const tool_status status = probe_tool(tool);
        report[tool] = json::object(
            {
                { "available", status.available },
                { "path", status.path },
                { "version", status.version },
            }
        );
    }
    return report;
}

} // namespace ecosystem
