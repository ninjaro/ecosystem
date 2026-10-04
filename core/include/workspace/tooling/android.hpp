#pragma once

#include "workspace/tooling/process.hpp"

namespace ecosystem {

struct android_environment {
    std::string sdk_root;
    std::string ndk_root;
    std::string ndk_version;
    std::string platform;
    std::string build_tools_version;
    std::string qt_dir;
    std::string qt_version;
    std::string qt_host_path;
    std::string qt_arch;
    std::string qt_cmake_bin;
    std::string abi;
    std::string emulator_bin;
    std::string avd_name;
    std::string adb_bin;
    std::string aapt_bin;
    string_list errors;
    string_list deployment_errors;
};

command_error configure_android_source_tree(
    const std::filesystem::path& project_root,
    const std::filesystem::path& source_dir,
    const std::filesystem::path& build_dir, const string_list& cmake_options,
    std::string* error_message, bool capture_output = false
);
android_environment detect_android_environment();
json android_environment_report(const android_environment& environment);

} // namespace ecosystem
