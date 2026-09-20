#include "workspace/tooling.hpp"

#include "workspace/project.hpp"
#include "workspace/source_dependencies.hpp"
#include "workspace/sync.hpp"
#include "workspace/template_text.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
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
        std::error_code error;
        return !path.empty() && fs::is_regular_file(path, error) && !error
            && ::access(path.c_str(), X_OK) == 0;
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

    std::string normalized_android_path(const fs::path& path) {
        if (path.empty())
            return {};
        std::error_code error;
        const auto normalized = fs::weakly_canonical(fs::absolute(path), error);
        return error ? path.string() : normalized.string();
    }

    std::vector<fs::path> android_subdirectories(const fs::path& root) {
        std::vector<fs::path> paths;
        std::error_code error;
        for (fs::directory_iterator it(root, error), end; !error && it != end;
             it.increment(error)) {
            if (it->is_directory(error))
                paths.push_back(it->path());
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    }

    std::string select_android_path(
        const std::vector<fs::path>& candidates, const std::string& name,
        const std::string& setting, string_list* errors
    ) {
        std::set<std::string> unique;
        for (const auto& candidate : candidates)
            unique.insert(normalized_android_path(candidate));
        if (unique.size() == 1U)
            return *unique.begin();
        std::string message = unique.empty() ? "missing " : "ambiguous ";
        message += name + "; set " + setting;
        for (const auto& candidate : unique)
            message += "\n  " + candidate;
        errors->push_back(message);
        return {};
    }

    std::string android_ndk_revision(const fs::path& root) {
        if (root.empty())
            return {};
        std::ifstream input(root / "source.properties");
        for (std::string line; std::getline(input, line);) {
            const auto separator = line.find('=');
            if (separator != std::string::npos
                && trim_copy(line.substr(0, separator)) == "Pkg.Revision")
                return trim_copy(line.substr(separator + 1));
        }
        return {};
    }

    std::string detect_android_adb_impl(const std::string& sdk_root) {
        const std::string adb_override = env_or_empty("ADB_BIN");
        if (!adb_override.empty()) {
            return normalized_android_path(adb_override);
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
        if (!emulator_override.empty()) {
            return normalized_android_path(emulator_override);
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
        const std::string& sdk_root, const std::string& build_tools_version,
        string_list* errors
    ) {
        const std::string aapt_override = env_or_empty("AAPT_BIN");
        if (!aapt_override.empty()) {
            return normalized_android_path(aapt_override);
        }

        if (!sdk_root.empty()) {
            if (!build_tools_version.empty()) {
                const fs::path preferred = fs::path(sdk_root) / "build-tools"
                    / build_tools_version / "aapt";
                return preferred.string();
            }

            std::vector<fs::path> candidates;
            for (const auto& version :
                 android_subdirectories(fs::path(sdk_root) / "build-tools"))
                if (executable_exists(version / "aapt"))
                    candidates.push_back(version / "aapt");
            if (!candidates.empty())
                return select_android_path(
                    candidates, "Android aapt",
                    "AAPT_BIN or ANDROID_BUILD_TOOLS_VERSION", errors
                );
        }

        return find_command_path("aapt");
    }

    fs::path configure_stamp_path(const fs::path& build_dir) {
        return build_dir / ".ecosystem_configured.stamp";
    }

    bool android_cache_matches(
        const fs::path& build_dir, const android_environment& environment,
        std::string* error_message
    ) {
        const auto qt_toolchain
            = fs::path(environment.qt_cmake_bin).parent_path().parent_path()
            / "lib/cmake/Qt6/qt.toolchain.cmake";
        const std::vector<std::pair<std::string, std::string>> settings {
            { "ANDROID_ABI", environment.abi },
            { "CMAKE_ANDROID_ARCH_ABI", environment.abi },
            { "ANDROID_NDK", environment.ndk_root },
            { "ANDROID_NDK_ROOT", environment.ndk_root },
            { "CMAKE_ANDROID_NDK", environment.ndk_root },
            { "QT_HOST_PATH", environment.qt_host_path },
            { "CMAKE_TOOLCHAIN_FILE",
              fs::is_regular_file(qt_toolchain)
                  ? normalized_android_path(qt_toolchain)
                  : std::string() }
        };
        std::ifstream cache(build_dir / "CMakeCache.txt");
        for (std::string line; std::getline(cache, line);)
            for (const auto& [name, expected] : settings) {
                if (expected.empty() || !line.starts_with(name + ":"))
                    continue;
                const auto separator = line.find('=');
                if (separator == std::string::npos)
                    continue;
                auto actual = line.substr(separator + 1);
                if (name != "ANDROID_ABI" && name != "CMAKE_ANDROID_ARCH_ABI")
                    actual = normalized_android_path(actual);
                if (actual == expected)
                    continue;
                assign_error(
                    error_message,
                    "Android build cache conflicts with selected " + name + ": "
                        + actual + " (selected " + expected
                        + "); use a fresh Android build directory: "
                        + build_dir.string()
                );
                return false;
            }
        return true;
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
    return scoped_command_log::capture(
        args, working_directory, environment, false
    );
}

thread_local scoped_command_log* scoped_command_log::active_ = nullptr;

scoped_command_log::scoped_command_log(const fs::path& path)
    : previous_(active_)
    , path_(path)
    , stream_(path, std::ios::binary) {
    active_ = this;
    append("");
}

scoped_command_log::~scoped_command_log() { active_ = previous_; }

const std::string& scoped_command_log::error() const { return error_; }

std::string scoped_command_log::active_error() {
    return active_ == nullptr ? std::string() : active_->error();
}

void scoped_command_log::append(const std::string& text) {
    if (!error_.empty())
        return;
    stream_ << text;
    stream_.flush();
    if (!stream_)
        error_ = "unable to write command log: " + path_.string();
}

captured_command scoped_command_log::capture(
    const std::vector<std::string>& args, const fs::path& working_directory,
    const std::vector<std::pair<std::string, std::string>>& environment,
    const bool echo_output
) {
    captured_command result;
    if (active_ != nullptr) {
        const json command { { "argv", args },
                             { "cwd",
                               working_directory.empty()
                                   ? fs::current_path().string()
                                   : working_directory.string() } };
        active_->append("\n>>> " + command.dump() + "\n");
        if (!active_->error().empty()) {
            result.output = active_->error();
            return result;
        }
    }
    const std::string command = "("
        + build_shell_command(args, working_directory, environment) + ") 2>&1";
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        result.output = "unable to start command\n";
        if (active_ != nullptr)
            active_->append(result.output + "<<< {\"exit_code\":-1}\n");
        return result;
    }

    bool read_failed = false;
    char buffer[4096];
    for (;;) {
        const auto count = ::read(::fileno(pipe), buffer, sizeof(buffer));
        if (count == 0)
            break;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            read_failed = true;
            break;
        }
        const std::string chunk(buffer, static_cast<std::size_t>(count));
        if (echo_output) {
            std::cout << chunk;
            std::cout.flush();
        } else {
            result.output += chunk;
        }
        if (active_ != nullptr)
            active_->append(chunk);
    }
    const int status = ::pclose(pipe);
    result.exit_code = status == -1 || read_failed ? -1
        : WIFEXITED(status)                        ? WEXITSTATUS(status)
        : WIFSIGNALED(status)                      ? 128 + WTERMSIG(status)
                                                   : status;
    if (active_ != nullptr) {
        active_->append(
            "\n<<< " + json { { "exit_code", result.exit_code } }.dump() + "\n"
        );
        if (!active_->error().empty()) {
            result.exit_code = -1;
            result.output += "\n" + active_->error();
        }
    }
    return result;
}

int run_command(
    const std::vector<std::string>& args, const fs::path& working_directory,
    const std::vector<std::pair<std::string, std::string>>& environment
) {
    if (scoped_command_log::active_ != nullptr)
        return scoped_command_log::capture(
                   args, working_directory, environment, true
        )
            .exit_code;
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

command_error configure_android_source_tree(
    const fs::path& project_root, const fs::path& source_dir,
    const fs::path& build_dir, const string_list& cmake_options,
    std::string* error_message, const bool capture_output
) {
    const android_environment environment = detect_android_environment();
    const auto report_path
        = local_state_dir(project_root) / "android/environment.json";
    if (!write_text_file(
            report_path, android_environment_report(environment).dump(2) + "\n",
            error_message
        ))
        return command_error::task_failed;
    if (!environment.errors.empty()) {
        std::string message = "Android environment selection failed ("
            + report_path.string() + ")";
        for (const auto& issue : environment.errors)
            message += "\n" + issue;
        assign_error(error_message, message);
        return command_error::missing_local_tooling;
    }

    std::error_code fs_error;
    if (!android_cache_matches(build_dir, environment, error_message))
        return command_error::invalid_request;
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
        environment.qt_cmake_bin,
        "-S",
        source_dir.string(),
        "-B",
        build_dir.string(),
        "-DCMAKE_BUILD_TYPE=Debug",
        std::string("-DQT_HOST_PATH=") + environment.qt_host_path,
        std::string("-DANDROID_ABI=") + environment.abi,
        std::string("-DANDROID_SDK_ROOT=") + environment.sdk_root,
        std::string("-DANDROID_NDK_ROOT=") + environment.ndk_root,
        "-DECOSYSTEM_PROFILE_KDE=OFF",
        "-DECOSYSTEM_PROFILE_ANDROID=ON",
    };
    if (!environment.platform.empty())
        command.push_back("-DANDROID_PLATFORM=" + environment.platform);
    command.insert(command.end(), cmake_options.begin(), cmake_options.end());

    const auto result = capture_output
        ? capture_command_result(command, source_dir)
        : captured_command { run_command(command, source_dir), {} };
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
    const auto sdk_home = normalized_android_path(env_or_empty("ANDROID_HOME"));
    const auto sdk_root
        = normalized_android_path(env_or_empty("ANDROID_SDK_ROOT"));
    if (!sdk_home.empty() && !sdk_root.empty() && sdk_home != sdk_root)
        environment.errors.push_back(
            "ANDROID_HOME and ANDROID_SDK_ROOT name different SDKs"
        );
    environment.sdk_root = sdk_home.empty() ? sdk_root : sdk_home;
    if (environment.sdk_root.empty()) {
        std::vector<fs::path> candidates;
        for (const auto& candidate :
             { default_home_path(fs::path("Android") / "Sdk"),
               std::string("/opt/android-sdk") })
            if (!candidate.empty()
                && directory_exists(fs::path(candidate) / "platform-tools"))
                candidates.emplace_back(candidate);
        for (const auto& tool :
             { find_command_path("adb"), find_command_path("emulator") }) {
            const fs::path path = normalized_android_path(tool);
            if (path.parent_path().filename() == "platform-tools"
                || path.parent_path().filename() == "emulator")
                candidates.push_back(path.parent_path().parent_path());
        }
        environment.sdk_root = select_android_path(
            candidates, "Android SDK", "ANDROID_HOME (or ANDROID_SDK_ROOT)",
            &environment.errors
        );
    }
    if (!directory_exists(environment.sdk_root))
        environment.errors.push_back(
            "Android SDK directory is unavailable: " + environment.sdk_root
        );

    const auto requested_ndk = env_or_empty("ANDROID_NDK_VERSION");
    environment.ndk_root
        = normalized_android_path(env_or_empty("ANDROID_NDK_ROOT"));
    if (environment.ndk_root.empty() && !environment.sdk_root.empty()) {
        const fs::path ndks = fs::path(environment.sdk_root) / "ndk";
        if (!requested_ndk.empty()) {
            environment.ndk_root
                = normalized_android_path(ndks / requested_ndk);
        } else {
            std::vector<fs::path> candidates;
            for (const auto& candidate : android_subdirectories(ndks))
                if (fs::is_regular_file(
                        candidate / "build/cmake/android.toolchain.cmake"
                    ))
                    candidates.push_back(candidate);
            environment.ndk_root = select_android_path(
                candidates, "Android NDK",
                "ANDROID_NDK_ROOT or ANDROID_NDK_VERSION", &environment.errors
            );
        }
    }
    if (environment.ndk_root.empty()
        || !fs::is_regular_file(
            fs::path(environment.ndk_root)
            / "build/cmake/android.toolchain.cmake"
        ))
        environment.errors.push_back(
            "Android NDK lacks build/cmake/android.toolchain.cmake: "
            + environment.ndk_root
        );
    environment.ndk_version = android_ndk_revision(environment.ndk_root);
    if (environment.ndk_version.empty())
        environment.errors.push_back(
            "Android NDK lacks Pkg.Revision in source.properties: "
            + environment.ndk_root
        );
    else if (!requested_ndk.empty() && requested_ndk != environment.ndk_version)
        environment.errors.push_back(
            "ANDROID_NDK_VERSION does not match the selected NDK revision "
            + environment.ndk_version
        );

    // The selected Qt toolchain owns its minimum API default. An explicit
    // override is forwarded unchanged for upstream validation.
    environment.platform = env_or_empty("ANDROID_PLATFORM");
    environment.build_tools_version
        = env_or_empty("ANDROID_BUILD_TOOLS_VERSION");
    environment.qt_dir = env_or_empty("QT_DIR");
    if (environment.qt_dir.empty())
        environment.qt_dir = default_home_path("Qt");
    environment.qt_dir = normalized_android_path(environment.qt_dir);
    environment.qt_version = env_or_empty("QT_VER");
    environment.qt_arch = env_or_empty("ANDROID_QT_ARCH");
    environment.abi = env_or_empty("ANDROID_ABI");
    const std::vector<std::pair<std::string, std::string>> architectures {
        { "android_arm64_v8a", "arm64-v8a" },
        { "android_armv7", "armeabi-v7a" },
        { "android_x86", "x86" },
        { "android_x86_64", "x86_64" }
    };
    for (const auto& [arch, abi] : architectures) {
        if (environment.qt_arch == arch && environment.abi.empty())
            environment.abi = abi;
        if (environment.abi == abi && environment.qt_arch.empty())
            environment.qt_arch = arch;
    }
    if (!environment.abi.empty()
        && std::none_of(
            architectures.begin(), architectures.end(),
            [&](const auto& item) { return item.second == environment.abi; }
        ))
        environment.errors.push_back(
            "unsupported ANDROID_ABI: " + environment.abi
        );
    if (!environment.qt_arch.empty()
        && std::none_of(
            architectures.begin(), architectures.end(), [&](const auto& item) {
                return item.first == environment.qt_arch
                    && item.second == environment.abi;
            }
        ))
        environment.errors.push_back(
            "ANDROID_QT_ARCH is unsupported or conflicts with ANDROID_ABI: "
            + environment.qt_arch
        );
    environment.qt_cmake_bin
        = normalized_android_path(env_or_empty("ANDROID_CMAKE_BIN"));
    if (environment.qt_cmake_bin.empty()) {
        std::vector<fs::path> versions = environment.qt_version.empty()
            ? android_subdirectories(environment.qt_dir)
            : std::vector<fs::path> { fs::path(environment.qt_dir)
                                      / environment.qt_version };
        std::vector<fs::path> candidates;
        for (const auto& version : versions)
            for (const auto& [arch, abi] : architectures) {
                const auto binary = version / arch / "bin/qt-cmake";
                if ((environment.qt_arch.empty() || environment.qt_arch == arch)
                    && (environment.abi.empty() || environment.abi == abi)
                    && executable_exists(binary))
                    candidates.push_back(binary);
            }
        environment.qt_cmake_bin = select_android_path(
            candidates, "Qt Android kit",
            "ANDROID_CMAKE_BIN or QT_DIR/QT_VER with ANDROID_ABI (or "
            "ANDROID_QT_ARCH)",
            &environment.errors
        );
    }
    const fs::path kit
        = fs::path(environment.qt_cmake_bin).parent_path().parent_path();
    if (kit.filename() == "gcc_64" || kit.filename() == "clang_64"
        || kit.filename() == "macos")
        environment.errors.push_back(
            "ANDROID_CMAKE_BIN selects a desktop Qt kit: " + kit.string()
        );
    for (const auto& [arch, abi] : architectures)
        if (kit.filename() == arch) {
            if ((!environment.abi.empty() && environment.abi != abi)
                || (!environment.qt_arch.empty()
                    && environment.qt_arch != arch))
                environment.errors.push_back(
                    "selected Qt Android kit conflicts with "
                    "ANDROID_ABI/ANDROID_QT_ARCH: "
                    + kit.string()
                );
            environment.qt_arch = arch;
            environment.abi = abi;
            environment.qt_version = kit.parent_path().filename().string();
        }
    if (environment.abi.empty())
        environment.errors.push_back(
            "cannot infer Android ABI; set ANDROID_ABI for the selected "
            "qt-cmake"
        );
    if (!executable_exists(environment.qt_cmake_bin))
        environment.errors.push_back(
            "Qt Android qt-cmake is not executable: " + environment.qt_cmake_bin
        );
    environment.qt_host_path
        = normalized_android_path(env_or_empty("QT_HOST_PATH"));
    if (environment.qt_host_path.empty() && !environment.qt_cmake_bin.empty()) {
        std::vector<fs::path> candidates;
        for (const auto* host : { "gcc_64", "clang_64", "macos" }) {
            const auto candidate = kit.parent_path() / host;
            if (fs::is_regular_file(
                    candidate / "lib/cmake/Qt6/Qt6Config.cmake"
                ))
                candidates.push_back(candidate);
        }
        environment.qt_host_path = select_android_path(
            candidates, "Qt host kit", "QT_HOST_PATH", &environment.errors
        );
    }
    if (!directory_exists(environment.qt_host_path))
        environment.errors.push_back(
            "Qt host directory is unavailable; set QT_HOST_PATH: "
            + environment.qt_host_path
        );
    environment.emulator_bin
        = detect_android_emulator_impl(environment.sdk_root);
    environment.avd_name = env_or_empty("ANDROID_AVD_NAME");
    if (environment.avd_name.empty()) {
        environment.avd_name = "api35_x86_64";
    }

    environment.adb_bin = detect_android_adb_impl(environment.sdk_root);
    environment.aapt_bin = detect_android_aapt_impl(
        environment.sdk_root, environment.build_tools_version,
        &environment.deployment_errors
    );
    if (!executable_exists(environment.aapt_bin))
        environment.deployment_errors.push_back(
            "aapt is unavailable; set AAPT_BIN or ANDROID_BUILD_TOOLS_VERSION: "
            + environment.aapt_bin
        );
    if (!executable_exists(environment.adb_bin))
        environment.deployment_errors.push_back(
            "adb is unavailable; set ADB_BIN or install SDK platform-tools: "
            + environment.adb_bin
        );

    return environment;
}

json android_environment_report(const android_environment& environment) {
    return { { "sdk_root", environment.sdk_root },
             { "ndk_root", environment.ndk_root },
             { "ndk_version", environment.ndk_version },
             { "qt_cmake_bin", environment.qt_cmake_bin },
             { "qt_host_path", environment.qt_host_path },
             { "qt_version", environment.qt_version },
             { "qt_arch", environment.qt_arch },
             { "abi", environment.abi },
             { "platform",
               environment.platform.empty() ? "Qt toolchain default"
                                            : environment.platform },
             { "aapt_bin", environment.aapt_bin },
             { "adb_bin", environment.adb_bin },
             { "emulator_bin", environment.emulator_bin },
             { "avd_name", environment.avd_name },
             { "errors", environment.errors },
             { "deployment_errors", environment.deployment_errors } };
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
