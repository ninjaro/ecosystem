#include "internal.hpp"
#include "workspace/tooling.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ecosystem {
using namespace tooling_detail;

namespace {

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

    std::string home_path(const fs::path& relative) {
        const std::string home = env_value("HOME");
        return home.empty() ? std::string()
                            : (fs::path(home) / relative).string();
    }

    std::string normalize_path(const fs::path& path) {
        if (path.empty())
            return {};
        std::error_code error;
        const auto normalized = fs::weakly_canonical(fs::absolute(path), error);
        return error ? path.string() : normalized.string();
    }

    std::vector<fs::path> subdirectories(const fs::path& root) {
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

    std::string select_path(
        const std::vector<fs::path>& candidates, const std::string& name,
        const std::string& setting, string_list* errors
    ) {
        std::set<std::string> unique;
        for (const auto& candidate : candidates)
            unique.insert(normalize_path(candidate));
        if (unique.size() == 1U)
            return *unique.begin();
        std::string message = unique.empty() ? "missing " : "ambiguous ";
        message += name + "; set " + setting;
        for (const auto& candidate : unique)
            message += "\n  " + candidate;
        errors->push_back(message);
        return {};
    }

    std::string ndk_revision(const fs::path& root) {
        if (root.empty())
            return {};
        std::ifstream input(root / "source.properties");
        for (std::string line; std::getline(input, line);) {
            const auto separator = line.find('=');
            if (separator != std::string::npos
                && trim(line.substr(0, separator)) == "Pkg.Revision")
                return trim(line.substr(separator + 1));
        }
        return {};
    }

    std::string adb_path(const std::string& sdk_root) {
        const std::string adb_override = env_value("ADB_BIN");
        if (!adb_override.empty()) {
            return normalize_path(adb_override);
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

    std::string emulator_path(const std::string& sdk_root) {
        const std::string emulator_override = env_value("ANDROID_EMULATOR_BIN");
        if (!emulator_override.empty()) {
            return normalize_path(emulator_override);
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

    std::string aapt_path(
        const std::string& sdk_root, const std::string& build_tools_version,
        string_list* errors
    ) {
        const std::string aapt_override = env_value("AAPT_BIN");
        if (!aapt_override.empty()) {
            return normalize_path(aapt_override);
        }

        if (!sdk_root.empty()) {
            if (!build_tools_version.empty()) {
                const fs::path preferred = fs::path(sdk_root) / "build-tools"
                    / build_tools_version / "aapt";
                return preferred.string();
            }

            std::vector<fs::path> candidates;
            for (const auto& version :
                 subdirectories(fs::path(sdk_root) / "build-tools"))
                if (executable_exists(version / "aapt"))
                    candidates.push_back(version / "aapt");
            if (!candidates.empty())
                return select_path(
                    candidates, "Android aapt",
                    "AAPT_BIN or ANDROID_BUILD_TOOLS_VERSION", errors
                );
        }

        return find_command_path("aapt");
    }

    bool cache_matches(
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
              fs::is_regular_file(qt_toolchain) ? normalize_path(qt_toolchain)
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
                    actual = normalize_path(actual);
                if (actual == expected)
                    continue;
                set_error(
                    error_message,
                    std::string("Android build cache conflicts with selected ")
                        .append(name)
                        .append(": ")
                        .append(actual)
                        .append(" (selected ")
                        .append(expected)
                        .append("); use a fresh Android build directory: ")
                        .append(build_dir.string())
                );
                return false;
            }
        return true;
    }

} // namespace

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
        set_error(error_message, message);
        return command_error::missing_local_tooling;
    }

    std::error_code fs_error;
    if (!cache_matches(build_dir, environment, error_message))
        return command_error::invalid_request;
    if (!reset_source(source_dir, build_dir, error_message)) {
        return command_error::task_failed;
    }
    fs::create_directories(build_dir, fs_error);
    if (fs_error) {
        set_error(error_message, fs_error.message());
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

android_environment detect_android_environment() {
    android_environment environment;
    const auto sdk_home = normalize_path(env_value("ANDROID_HOME"));
    const auto sdk_root = normalize_path(env_value("ANDROID_SDK_ROOT"));
    if (!sdk_home.empty() && !sdk_root.empty() && sdk_home != sdk_root)
        environment.errors.emplace_back(
            "ANDROID_HOME and ANDROID_SDK_ROOT name different SDKs"
        );
    environment.sdk_root = sdk_home.empty() ? sdk_root : sdk_home;
    if (environment.sdk_root.empty()) {
        std::vector<fs::path> candidates;
        for (const auto& candidate : { home_path(fs::path("Android") / "Sdk"),
                                       std::string("/opt/android-sdk") })
            if (!candidate.empty()
                && directory_exists(fs::path(candidate) / "platform-tools"))
                candidates.emplace_back(candidate);
        for (const auto& tool :
             { find_command_path("adb"), find_command_path("emulator") }) {
            const fs::path path = normalize_path(tool);
            if (path.parent_path().filename() == "platform-tools"
                || path.parent_path().filename() == "emulator")
                candidates.push_back(path.parent_path().parent_path());
        }
        environment.sdk_root = select_path(
            candidates, "Android SDK", "ANDROID_HOME (or ANDROID_SDK_ROOT)",
            &environment.errors
        );
    }
    if (!directory_exists(environment.sdk_root))
        environment.errors.push_back(
            "Android SDK directory is unavailable: " + environment.sdk_root
        );

    const auto requested_ndk = env_value("ANDROID_NDK_VERSION");
    environment.ndk_root = normalize_path(env_value("ANDROID_NDK_ROOT"));
    if (environment.ndk_root.empty() && !environment.sdk_root.empty()) {
        const fs::path ndks = fs::path(environment.sdk_root) / "ndk";
        if (!requested_ndk.empty()) {
            environment.ndk_root = normalize_path(ndks / requested_ndk);
        } else {
            std::vector<fs::path> candidates;
            for (const auto& candidate : subdirectories(ndks))
                if (fs::is_regular_file(
                        candidate / "build/cmake/android.toolchain.cmake"
                    ))
                    candidates.push_back(candidate);
            environment.ndk_root = select_path(
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
    environment.ndk_version = ndk_revision(environment.ndk_root);
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
    environment.platform = env_value("ANDROID_PLATFORM");
    environment.build_tools_version = env_value("ANDROID_BUILD_TOOLS_VERSION");
    environment.qt_dir = env_value("QT_DIR");
    if (environment.qt_dir.empty())
        environment.qt_dir = home_path("Qt");
    environment.qt_dir = normalize_path(environment.qt_dir);
    environment.qt_version = env_value("QT_VER");
    environment.qt_arch = env_value("ANDROID_QT_ARCH");
    environment.abi = env_value("ANDROID_ABI");
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
            architectures.begin(), architectures.end(),
            [&](const auto& item) {
                return item.first == environment.qt_arch
                    && item.second == environment.abi;
            }
        ))
        environment.errors.push_back(
            "ANDROID_QT_ARCH is unsupported or conflicts with ANDROID_ABI: "
            + environment.qt_arch
        );
    environment.qt_cmake_bin = normalize_path(env_value("ANDROID_CMAKE_BIN"));
    if (environment.qt_cmake_bin.empty()) {
        std::vector<fs::path> versions = environment.qt_version.empty()
            ? subdirectories(environment.qt_dir)
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
        environment.qt_cmake_bin = select_path(
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
                || (!environment.qt_arch.empty() && environment.qt_arch != arch
                ))
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
        environment.errors.emplace_back(
            "cannot infer Android ABI; set ANDROID_ABI for the selected "
            "qt-cmake"
        );
    if (!executable_exists(environment.qt_cmake_bin))
        environment.errors.push_back(
            "Qt Android qt-cmake is not executable: " + environment.qt_cmake_bin
        );
    environment.qt_host_path = normalize_path(env_value("QT_HOST_PATH"));
    if (environment.qt_host_path.empty() && !environment.qt_cmake_bin.empty()) {
        std::vector<fs::path> candidates;
        for (const auto* host : { "gcc_64", "clang_64", "macos" }) {
            const auto candidate = kit.parent_path() / host;
            if (fs::is_regular_file(
                    candidate / "lib/cmake/Qt6/Qt6Config.cmake"
                ))
                candidates.push_back(candidate);
        }
        environment.qt_host_path = select_path(
            candidates, "Qt host kit", "QT_HOST_PATH", &environment.errors
        );
    }
    if (!directory_exists(environment.qt_host_path))
        environment.errors.push_back(
            "Qt host directory is unavailable; set QT_HOST_PATH: "
            + environment.qt_host_path
        );
    environment.emulator_bin = emulator_path(environment.sdk_root);
    environment.avd_name = env_value("ANDROID_AVD_NAME");
    if (environment.avd_name.empty()) {
        environment.avd_name = "api35_x86_64";
    }

    environment.adb_bin = adb_path(environment.sdk_root);
    environment.aapt_bin = aapt_path(
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

} // namespace ecosystem
