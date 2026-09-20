#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

void test_android_application_id_is_manifest_owned() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_manifest();
    manifest_value.version = "2.3.4";
    manifest_value.android_application_id = "org.ninjaro.sample";
    manifest_value.android_package_source_dir = "android";
    manifest_value.components.front().stack["android"]
        = json::array({ "qt_android" });
    manifest_value.components.front().artifacts.front().kind = "qt_app";

    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());
    require_contains(
        generated_cmake, "QT_ANDROID_PACKAGE_NAME org.ninjaro.sample",
        "Android package identity must come from manifest metadata"
    );
    require_contains(
        generated_cmake,
        "if (COMMAND qt_policy)\n"
        "        qt_policy(SET QTP0002 NEW)",
        "Android package properties must opt into safe Qt path JSON handling "
        "when the Qt policy API is available"
    );
    require_contains(
        generated_cmake, "QT_ANDROID_VERSION_NAME \"${PROJECT_VERSION}\"",
        "Android version name must come from manifest-owned project version"
    );
    require_contains(
        generated_cmake, "QT_ANDROID_MIN_SDK_VERSION 28",
        "Android Qt targets must declare the Qt-supported minimum SDK"
    );
    require_contains(
        generated_cmake,
        "QT_ANDROID_PACKAGE_SOURCE_DIR "
        "\"${CMAKE_CURRENT_SOURCE_DIR}/android\"",
        "tracked facade must resolve Android package sources from the "
        "project root"
    );
    require_contains(
        generated_cmake, "project(sample VERSION 2.3.4",
        "project version must come from manifest metadata"
    );
    require_contains(
        generated_cmake, "ECOSYSTEM_PROJECT_VERSION=\"${PROJECT_VERSION}\"",
        "targets must receive the manifest-owned project version"
    );

    const std::string developer_cmake
        = ecosystem::generate_developer_cmakelists(manifest_value, root.path());
    require_contains(
        developer_cmake,
        "QT_ANDROID_PACKAGE_SOURCE_DIR "
        "\"${ECOSYSTEM_PROJECT_ROOT}/android\"",
        "developer surface must resolve Android package sources from the "
        "project root"
    );

    const ecosystem::json serialized = ecosystem::to_json(manifest_value);
    require_true(
        serialized.at("android_package_source_dir") == "android",
        "Android package source directory must survive serialization"
    );

    std::string save_error;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &save_error
        ),
        "Android package metadata manifest must serialize"
    );
    const ecosystem::manifest_report loaded
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        loaded.errors.empty() && loaded.value.has_value()
            && loaded.value->android_package_source_dir == "android",
        "Android package source directory must survive a manifest round trip"
    );

    manifest_value.android_package_source_dir = "../outside";
    const ecosystem::string_list errors
        = ecosystem::validate_manifest(manifest_value);
    require_true(
        std::find_if(
            errors.begin(), errors.end(),
            [](const std::string& error) {
                return error.find("android_package_source_dir")
                    != std::string::npos;
            }
        ) != errors.end(),
        "Android package source directory must not escape the project root"
    );

    manifest_value.android_package_source_dir = ".";
    const ecosystem::string_list root_errors
        = ecosystem::validate_manifest(manifest_value);
    require_true(
        std::find_if(
            root_errors.begin(), root_errors.end(),
            [](const std::string& error) {
                return error.find("android_package_source_dir")
                    != std::string::npos;
            }
        ) != root_errors.end(),
        "Android package source directory must not copy the project root"
    );

    manifest_value.android_package_source_dir = "android;unsafe";
    const ecosystem::string_list unsafe_errors
        = ecosystem::validate_manifest(manifest_value);
    require_true(
        std::find_if(
            unsafe_errors.begin(), unsafe_errors.end(),
            [](const std::string& error) {
                return error.find("android_package_source_dir")
                    != std::string::npos;
            }
        ) != unsafe_errors.end(),
        "Android package source directory must be safe for CMake interpolation"
    );
}

void test_android_environment_selection_is_explicit_and_reported() {
    temp_dir root;
    const auto sdk = root.path() / "sdk with spaces";
    const auto qt = root.path() / "Qt";
    const auto log = root.path() / "qt-configure.log";
    const auto write_ndk = [&](const std::string& version) {
        const auto ndk = sdk / "ndk" / version;
        write_text(ndk / "build/cmake/android.toolchain.cmake", "# fixture\n");
        write_text(
            ndk / "source.properties", "Pkg.Revision = " + version + "\n"
        );
        return ndk;
    };
    const auto write_kit = [&](const std::string& version,
                               const std::string& arch) {
        const auto kit = qt / version / arch;
        write_executable_script(kit / "bin/qt-cmake", R"(#!/usr/bin/env bash
printf '%s\n' "$@" >> "$FAKE_QT_CONFIGURE_LOG"
exit 0
)");
        write_text(kit / "lib/cmake/Qt6/qt.toolchain.cmake", "# fixture\n");
        write_text(
            qt / version / "gcc_64/lib/cmake/Qt6/Qt6Config.cmake",
            "# host fixture\n"
        );
        return kit;
    };
    const auto ndk = write_ndk("30.0.123");
    const auto kit = write_kit("9.1.0", "android_arm64_v8a");
    write_executable_script(sdk / "platform-tools/adb", "#!/bin/sh\nexit 0\n");
    write_executable_script(
        sdk / "build-tools/42.0.0/aapt", "#!/bin/sh\nexit 0\n"
    );

    scoped_env sdk_env("ANDROID_SDK_ROOT", sdk.string());
    scoped_env sdk_home_env("ANDROID_HOME", "");
    scoped_env ndk_env("ANDROID_NDK_ROOT", "");
    scoped_env ndk_version_env("ANDROID_NDK_VERSION", "");
    scoped_env qt_env("QT_DIR", qt.string());
    scoped_env qt_version_env("QT_VER", "");
    scoped_env qt_host_env("QT_HOST_PATH", "");
    scoped_env qt_arch_env("ANDROID_QT_ARCH", "");
    scoped_env abi_env("ANDROID_ABI", "");
    scoped_env qt_cmake_env("ANDROID_CMAKE_BIN", "");
    scoped_env platform_env("ANDROID_PLATFORM", "");
    scoped_env build_tools_env("ANDROID_BUILD_TOOLS_VERSION", "");
    scoped_env aapt_env("AAPT_BIN", "");
    scoped_env adb_env("ADB_BIN", "");
    scoped_env log_env("FAKE_QT_CONFIGURE_LOG", log.string());

    auto selected = ecosystem::detect_android_environment();
    require_true(
        selected.errors.empty(),
        "unique installed kits must resolve: "
            + ecosystem::android_environment_report(selected).dump()
    );
    require_true(
        selected.abi == "arm64-v8a" && selected.qt_version == "9.1.0"
            && selected.ndk_version == "30.0.123",
        "selection must use installed versions and the Qt kit ABI"
    );
    require_true(
        selected.platform.empty(), "Qt must own the minimum API default"
    );
    require_true(
        selected.deployment_errors.empty()
            && selected.aapt_bin == (sdk / "build-tools/42.0.0/aapt").string(),
        "the unique installed aapt must resolve"
    );

    const auto check_error = [&](const std::string& expected) {
        const auto environment = ecosystem::detect_android_environment();
        require_contains(
            json(environment.errors).dump(), expected,
            "environment error must explain the selector"
        );
    };
    {
        scoped_env conflicting_home("ANDROID_HOME", root.path().string());
        check_error("different SDKs");
    }
    {
        scoped_env missing_root(
            "ANDROID_SDK_ROOT", (root.path() / "absent-sdk").string()
        );
        check_error("SDK directory is unavailable");
    }
    const auto second_kit = write_kit("9.1.0", "android_x86_64");
    check_error("ambiguous Qt Android kit");
    {
        scoped_env explicit_arch("ANDROID_QT_ARCH", "android_arm64_v8a");
        require_true(
            ecosystem::detect_android_environment().abi == "arm64-v8a",
            "Qt architecture must infer its ABI"
        );
        scoped_env conflicting_abi("ANDROID_ABI", "x86_64");
        check_error("conflicts with ANDROID_ABI");
    }
    scoped_env explicit_abi("ANDROID_ABI", "arm64-v8a");
    {
        scoped_env explicit_binary(
            "ANDROID_CMAKE_BIN", (second_kit / "bin/qt-cmake").string()
        );
        check_error("kit conflicts");
    }
    {
        scoped_env explicit_binary(
            "ANDROID_CMAKE_BIN", (root.path() / "missing-qt-cmake").string()
        );
        check_error("not executable");
    }
    {
        scoped_env explicit_binary("ANDROID_CMAKE_BIN", kit.string());
        check_error("not executable");
    }
    write_kit("9.2.0", "android_arm64_v8a");
    check_error("ambiguous Qt Android kit");
    scoped_env explicit_version("QT_VER", "9.1.0");
    const auto second_ndk = write_ndk("31.0.456");
    check_error("ambiguous Android NDK");
    scoped_env explicit_ndk("ANDROID_NDK_VERSION", "30.0.123");
    {
        scoped_env conflicting_ndk("ANDROID_NDK_ROOT", second_ndk.string());
        check_error("does not match");
    }
    {
        scoped_env missing_ndk("ANDROID_NDK_ROOT", root.path().string());
        check_error("lacks build/cmake/android.toolchain.cmake");
    }
    {
        scoped_env missing_host(
            "QT_HOST_PATH", (root.path() / "missing-host").string()
        );
        check_error("Qt host directory is unavailable");
    }
    {
        const auto host_binary = qt / "9.1.0/gcc_64/bin/qt-cmake";
        write_executable_script(host_binary, "#!/bin/sh\nexit 0\n");
        scoped_env desktop_binary("ANDROID_CMAKE_BIN", host_binary.string());
        check_error("desktop Qt kit");
    }
    {
        scoped_env missing_adb(
            "ADB_BIN", (root.path() / "missing-adb").string()
        );
        scoped_env missing_aapt(
            "AAPT_BIN", (root.path() / "missing-aapt").string()
        );
        selected = ecosystem::detect_android_environment();
        require_true(
            selected.adb_bin == (root.path() / "missing-adb").string()
                && selected.aapt_bin == (root.path() / "missing-aapt").string(),
            "explicit tool overrides must not fall back to other tools"
        );
        require_true(
            selected.errors.empty() && selected.deployment_errors.size() == 2U,
            "deployment prerequisites must not block build-only configuration"
        );
    }
    write_executable_script(
        sdk / "build-tools/43.0.0/aapt", "#!/bin/sh\nexit 0\n"
    );
    selected = ecosystem::detect_android_environment();
    require_contains(
        json(selected.deployment_errors).dump(), "ambiguous Android aapt",
        "multiple build tools require a choice"
    );
    scoped_env explicit_tools("ANDROID_BUILD_TOOLS_VERSION", "42.0.0");

    const auto project = root.path() / "project";
    auto manifest_value = sample_build_manifest("sample");
    manifest_value.components.front().stack
        = { { "qt", json::array({ "Core" }) },
            { "android", json::array({ "qt_android" }) } };
    manifest_value.components.front().artifacts.front().kind = "qt_app";
    std::string error;
    require_true(
        ecosystem::save_manifest(
            project / "manifest.json", manifest_value, &error
        ),
        "Android fixture must save: " + error
    );
    write_text(project / "src/main.cpp", "int main() { return 0; }\n");
    require_true(
        ecosystem::configure_build_tree(
            project, manifest_value, "android", false, false, false, &error,
            true
        ) == ecosystem::command_error::ok,
        "selected environment must configure: " + error
    );
    const auto arguments = read_text(log);
    require_contains(
        arguments, "-DANDROID_ABI=arm64-v8a",
        "configure must use the selected Qt ABI"
    );
    require_contains(
        arguments, "-DANDROID_NDK_ROOT=" + ndk.string(),
        "configure must use Qt's NDK root argument"
    );
    require_not_contains(
        arguments, "-DANDROID_PLATFORM=",
        "configuration must retain Qt's minimum API default"
    );
    const auto report_path = project / ".ecosystem/android/environment.json";
    const auto report = json::parse(read_text(report_path));
    require_true(
        report.at("abi") == "arm64-v8a"
            && report.at("ndk_root") == ndk.string(),
        "persistent evidence must name the selected toolchain"
    );
    {
        scoped_env platform("ANDROID_PLATFORM", "android-31");
        require_true(
            ecosystem::configure_build_tree(
                project, manifest_value, "android", false, false, false, &error,
                true
            ) == ecosystem::command_error::ok,
            "explicit platform must configure"
        );
        require_contains(
            read_text(log), "-DANDROID_PLATFORM=android-31",
            "explicit API override must reach Qt"
        );
    }
    const auto cached = ecosystem::local_build_cache_path(project, "android");
    write_text(cached, "ANDROID_ABI:STRING=x86_64\n");
    const auto previous_log = read_text(log);
    require_true(
        ecosystem::configure_build_tree(
            project, manifest_value, "android", false, false, false, &error,
            true
        ) == ecosystem::command_error::invalid_request,
        "ABI changes must not reuse an incompatible cache"
    );
    require_contains(
        error, "fresh Android build directory",
        "cache rejection must explain recovery"
    );
    require_true(
        read_text(log) == previous_log
            && read_text(cached) == "ANDROID_ABI:STRING=x86_64\n",
        "cache mismatch must preserve the old build and skip configure"
    );
    fs::remove(cached);
    require_true(
        run_marx_cli(project, "sync").exit_code == 0, "doctor fixture must sync"
    );
    const auto doctor = run_cli(project, "doctor --profile android sample:app");
    require_true(
        doctor.exit_code == 0,
        "Android scoped doctor must use the selected cross toolchain: "
            + doctor.output
    );
    require_contains(
        doctor.output,
        "android environment:", "doctor must report the environment"
    );
    require_contains(
        doctor.output, "arm64-v8a", "doctor must report the selected ABI"
    );
    require_true(
        read_text(log).size() > previous_log.size(),
        "artifact-scoped doctor must invoke Qt's Android configure"
    );
    require_contains(
        read_text(log), "probes/sample__app/source",
        "Android doctor must retain an independently scoped probe"
    );
    {
        scoped_env missing_binary(
            "ANDROID_CMAKE_BIN", (root.path() / "absent").string()
        );
        const auto before = read_text(log);
        require_true(
            ecosystem::configure_build_tree(
                project, manifest_value, "android", false, false, false, &error,
                true
            ) == ecosystem::command_error::missing_local_tooling,
            "invalid selection must stop before configuring"
        );
        require_true(
            read_text(log) == before,
            "invalid selection must not invoke another Qt"
        );
        require_true(
            !json::parse(read_text(report_path)).at("errors").empty(),
            "failed selection must retain diagnostic evidence"
        );
    }
}

void test_cli_run_android_deploys_selected_artifact() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_build_manifest("sample");
    manifest_value.components.front().stack = json::object(
        {
            { "qt", json::array({ "Core" }) },
            { "android", json::array({ "qt_android" }) },
        }
    );
    manifest_value.components.front().artifacts.front().kind = "qt_app";

    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary android manifest"
    );
    write_text(root.path() / "src/main.cpp", "int main() { return 0; }\n");

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path sdk_root = fake_root / "sdk";
    const fs::path ndk_root = sdk_root / "ndk" / "27.2.12479018";
    const fs::path adb_log = fake_root / "adb.log";
    fs::create_directories(fake_bin);
    fs::create_directories(ndk_root);
    write_text(ndk_root / "build/cmake/android.toolchain.cmake", "# fixture\n");
    write_text(
        ndk_root / "source.properties", "Pkg.Revision = 27.2.12479018\n"
    );

    write_executable_script(
        fake_bin / "qt-cmake",
        "#!/usr/bin/env bash\n"
        "while [ $# -gt 0 ]; do\n"
        "  if [ \"$1\" = \"-B\" ]; then\n"
        "    build_dir=\"$2\"\n"
        "    shift 2\n"
        "    continue\n"
        "  fi\n"
        "  shift\n"
        "done\n"
        "mkdir -p \"$build_dir/outputs/apk/debug\"\n"
        "mkdir -p \"$build_dir/android-build\"\n"
        "printf 'old layout APK' > "
        "\"$build_dir/android-build/sample__app.apk\"\n"
        ": > \"$build_dir/outputs/apk/debug/stale-debug.apk\"\n"
        ": > \"$build_dir/outputs/apk/debug/sample__app-x86_64-debug.apk\"\n"
        ": > \"$build_dir/outputs/apk/debug/sample__app-arm64-debug.apk\"\n"
        "exit 0\n"
    );
    write_executable_script(
        fake_bin / "cmake",
        "#!/usr/bin/env bash\n"
        "build_dir=\"\"\n"
        "target=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  case \"$1\" in\n"
        "    --build)\n"
        "      build_dir=\"$2\"\n"
        "      shift 2\n"
        "      ;;\n"
        "    --target)\n"
        "      target=\"$2\"\n"
        "      shift 2\n"
        "      ;;\n"
        "    *)\n"
        "      shift\n"
        "      ;;\n"
        "  esac\n"
        "done\n"
        "mkdir -p \"$build_dir\"\n"
        "printf '%s\\n' \"$target\" >> \"$FAKE_ANDROID_BUILD_LOG\"\n"
        "if [ \"$target\" != \"sample__app_make_apk\" ]; then exit 8; fi\n"
        "mkdir -p \"$build_dir/android-build-sample__app\"\n"
        "if [ \"$FAKE_APK_MODE\" = failed ]; then exit 9; fi\n"
        "if [ \"$FAKE_APK_MODE\" = missing ]; then exit 0; fi\n"
        "if [ \"$FAKE_APK_MODE\" = empty ]; then\n"
        "  : > \"$build_dir/android-build-sample__app/sample__app.apk\"\n"
        "else\n"
        "  printf 'fixture APK' > "
        "\"$build_dir/android-build-sample__app/sample__app.apk\"\n"
        "fi\n"
        "exit 0\n"
    );
    write_executable_script(
        fake_bin / "adb",
        "#!/usr/bin/env bash\n"
        "if [ \"$1\" = \"devices\" ]; then\n"
        "  echo \"List of devices attached\"\n"
        "  printf '%s\\n' \"$FAKE_ADB_DEVICES\"\n"
        "  exit \"${FAKE_ADB_DEVICES_EXIT:-0}\"\n"
        "fi\n"
        "printf '%s\\n' \"$*\" >> \"$FAKE_ADB_LOG\"\n"
        "if [ \"$3\" = \"wait-for-device\" ]; then\n"
        "  exit 0\n"
        "fi\n"
        "if [ \"$3\" = \"shell\" ] && [ \"$4\" = \"getprop\" ] && [ \"$5\" = "
        "\"sys.boot_completed\" ]; then\n"
        "  echo 1\n"
        "  exit 0\n"
        "fi\n"
        "exit 0\n"
    );
    write_executable_script(
        fake_bin / "emulator",
        "#!/usr/bin/env bash\n"
        "printf 'unexpected emulator launch\\n' >> \"$FAKE_ADB_LOG\"\n"
    );
    write_executable_script(
        fake_bin / "aapt",
        "#!/usr/bin/env bash\n"
        "echo \"package: name='org.example.sample' versionCode='1'\"\n"
    );

    const std::string original_path = []() {
        const char* value = std::getenv("PATH");
        return value == nullptr ? std::string() : std::string(value);
    }();
    scoped_env path_env("PATH", fake_bin.string() + ":" + original_path);
    scoped_env sdk_env("ANDROID_SDK_ROOT", sdk_root.string());
    scoped_env sdk_home_env("ANDROID_HOME", sdk_root.string());
    scoped_env ndk_env("ANDROID_NDK_ROOT", ndk_root.string());
    scoped_env ndk_version_env("ANDROID_NDK_VERSION", "27.2.12479018");
    scoped_env host_env("QT_HOST_PATH", fake_root.string());
    scoped_env arch_env("ANDROID_QT_ARCH", "android_x86_64");
    scoped_env abi_env("ANDROID_ABI", "x86_64");
    scoped_env qt_cmake_env(
        "ANDROID_CMAKE_BIN", (fake_bin / "qt-cmake").string()
    );
    scoped_env adb_env("ADB_BIN", (fake_bin / "adb").string());
    scoped_env aapt_env("AAPT_BIN", (fake_bin / "aapt").string());
    scoped_env adb_log_env("FAKE_ADB_LOG", adb_log.string());
    scoped_env build_log_env(
        "FAKE_ANDROID_BUILD_LOG", (fake_root / "build.log").string()
    );
    scoped_env apk_mode_env("FAKE_APK_MODE", "normal");
    scoped_env boot_timeout_env("ANDROID_EMULATOR_BOOT_TIMEOUT", "5");
    scoped_env emulator_env(
        "ANDROID_EMULATOR_BIN", (fake_bin / "emulator").string()
    );
    scoped_env serial_env("ANDROID_SERIAL", "");
    scoped_env devices_env(
        "FAKE_ADB_DEVICES", "emulator-5554 device product:fake model:FakeDevice"
    );
    scoped_env query_exit_env("FAKE_ADB_DEVICES_EXIT", "0");

    write_text(adb_log, "");
    {
        scoped_env missing_adb("ADB_BIN", (fake_root / "missing-adb").string());
        scoped_env missing_aapt(
            "AAPT_BIN", (fake_root / "missing-aapt").string()
        );
        const auto build
            = run_marx_cli(root.path(), "build android sample:app");
        require_true(
            build.exit_code == 0,
            "build-only APK production must work without deployment tools: "
                + build.output
        );
        require_contains(
            build.output,
            "apk: "
            ".ecosystem/build/project/android/debug/default/"
            "android-build-sample__app/sample__app.apk",
            "build must report the selected APK"
        );
        require_true(
            read_text(adb_log).empty(), "build-only command must not deploy"
        );
    }
    const cli_result result
        = run_cli(root.path(), "run android --android-mode emulator");
    require_true(
        result.exit_code == 0,
        "ecos run android must deploy a Qt Android artifact"
    );
    require_contains(
        result.output, "ran sample:app",
        "android ecos run must report the deployed artifact"
    );
    require_contains(
        read_text(adb_log), "install -r",
        "android ecos run must install the APK through adb"
    );
    require_contains(
        read_text(adb_log), "android-build-sample__app/sample__app.apk",
        "android ecos run must rebuild and select the requested target APK "
        "instead of a stale package"
    );
    require_contains(
        read_text(adb_log), "shell monkey -p org.example.sample",
        "android ecos run must launch the installed package through adb"
    );
    for (const auto* mode : { "missing", "empty", "failed" }) {
        scoped_env apk_mode("FAKE_APK_MODE", mode);
        write_text(adb_log, "");
        const auto apk = ecosystem::local_build_dir(root.path(), "android")
            / "android-build-sample__app/sample__app.apk";
        fs::remove(apk);
        const auto rejected
            = run_cli(root.path(), "run android --android-mode emulator");
        require_true(
            rejected.exit_code != 0,
            "bad APK output must fail before deployment"
        );
        require_true(
            read_text(adb_log).empty(), "bad APK output must not reach adb"
        );
        if (std::string(mode) != "failed") {
            require_contains(
                rejected.output, "nonempty APK for sample:app",
                "missing/empty output must identify the selected artifact"
            );
            require_contains(
                rejected.output, apk.string(),
                "missing output must name its expected location"
            );
        }
    }

    const auto check_target =
        [&](const std::string& devices, const std::string& mode,
            const std::string& requested, const std::string& selected,
            const std::string& diagnostic,
            const std::string& query_exit = "0") {
            write_text(adb_log, "");
            scoped_env connected("FAKE_ADB_DEVICES", devices);
            scoped_env explicit_serial("ANDROID_SERIAL", requested);
            scoped_env query_status("FAKE_ADB_DEVICES_EXIT", query_exit);
            const auto attempt
                = run_cli(root.path(), "run android --android-mode " + mode);
            if (!selected.empty()) {
                require_true(
                    attempt.exit_code == 0,
                    "ready Android target must deploy: " + attempt.output
                );
                const auto log = read_text(adb_log);
                require_contains(
                    log, "-s " + selected + " install -r",
                    "deployment must use the resolved ready target"
                );
                require_contains(
                    log, "-s " + selected + " shell monkey",
                    "launch must use the same selected target"
                );
            } else {
                require_true(
                    attempt.exit_code != 0,
                    "invalid Android target selection must fail: " + devices
                );
                require_contains(
                    attempt.output, diagnostic,
                    "selection failure must explain the target/query problem"
                );
                require_true(
                    read_text(adb_log).empty(),
                    "selection failure must not wait, install, launch or start "
                    "another emulator: "
                        + read_text(adb_log)
                );
            }
        };
    check_target(
        "emulator-5554 offline\nemulator-5556 device", "emulator", "",
        "emulator-5556", ""
    );
    check_target(
        "phone-a unauthorized\nphone-b device", "device", "", "phone-b", ""
    );
    check_target(
        "emulator-5554 offline\nphone-b device", "auto", "", "phone-b", ""
    );
    check_target(
        "phone-a device\nemulator-5554 device", "auto", "", "emulator-5554", ""
    );
    check_target(
        "emulator-5554 device\nemulator-5556 device\nphone-b device", "auto",
        "", "", "ANDROID_SERIAL"
    );
    check_target(
        "phone-a device\nphone-b device", "device", "", "", "phone-a, phone-b"
    );
    check_target(
        "phone-a device\nphone-b device\nemulator-5554 device", "auto",
        "phone-b", "phone-b", ""
    );
    check_target(
        "emulator-5554 device\nemulator-5556 device", "emulator",
        "emulator-5556", "emulator-5556", ""
    );
    check_target(
        "phone-a device", "auto", "absent", "", "ANDROID_SERIAL=absent"
    );
    check_target(
        "phone-a unauthorized\nphone-b device", "auto", "phone-a", "",
        "unauthorized"
    );
    check_target(
        "phone-a device", "emulator", "phone-a", "", "--android-mode emulator"
    );
    check_target(
        "emulator-5554 offline", "emulator", "", "", "emulator-5554 (offline)"
    );
    check_target(
        "phone-a unauthorized", "device", "", "", "phone-a (unauthorized)"
    );
    check_target("phone-a recovery", "device", "", "", "phone-a (recovery)");
    check_target(
        "phone-a no permissions (user is not in the plugdev group)", "device",
        "", "", "no permissions"
    );
    check_target("", "device", "", "", "no physical Android device");
    check_target(
        "adb server unavailable", "auto", "", "", "adb devices failed", "7"
    );
    check_target(
        "* daemon not running; starting now\n* daemon started successfully\n"
        "phone-a\nphone-b\tdevice product:fake",
        "device", "", "phone-b", ""
    );

    const auto ready_adb = read_text(fake_bin / "adb");
    const auto timeout_binary = ecosystem::find_command_path("timeout");
    require_true(!timeout_binary.empty(), "Android tests require GNU timeout");
    for (const auto* tool : { "bash", "mkdir" })
        fs::create_symlink(ecosystem::find_command_path(tool), fake_bin / tool);
    {
        scoped_env minimal_path("PATH", fake_bin.string());
        write_text(adb_log, "");
        const auto without_timeout
            = run_cli(root.path(), "run android --android-mode emulator");
        require_true(
            without_timeout.exit_code != 0,
            "missing timeout must fail before device interaction"
        );
        require_contains(
            without_timeout.output, "requires GNU Coreutils timeout",
            "missing timeout must name the required tool"
        );
        require_true(
            read_text(adb_log).empty(), "missing timeout must not call adb"
        );
        const auto build_only
            = run_marx_cli(root.path(), "build android sample:app");
        require_true(
            build_only.exit_code == 0, "APK build must not require timeout"
        );
        fs::create_symlink(timeout_binary, fake_bin / "gtimeout");
        const auto with_gtimeout
            = run_cli(root.path(), "run android --android-mode emulator");
        require_true(
            with_gtimeout.exit_code == 0,
            "gtimeout must support the same readiness contract: "
                + with_gtimeout.output
        );
        fs::remove(fake_bin / "gtimeout");
    }
    write_executable_script(fake_bin / "adb", R"(#!/usr/bin/env bash
block_phase() {
    printf '%s: entered\n' "$1" >> "$FAKE_ADB_LOG"
    trap '' TERM
    sleep 3
    printf '%s: escaped timeout\n' "$1" >> "$FAKE_ADB_LOG"
}
if [ "$1" = devices ]; then
    if [ "$FAKE_BOOT_MODE" = query ]; then block_phase query; fi
    if [ "$FAKE_BOOT_MODE" = shared ]; then sleep 0.7; fi
    printf 'List of devices attached\nemulator-5554 device\n'
    exit 0
fi
printf '%s\n' "$*" >> "$FAKE_ADB_LOG"
if [ "$3" = wait-for-device ]; then
    if [ "$FAKE_BOOT_MODE" = wait ]; then block_phase wait; fi
    if [ "$FAKE_BOOT_MODE" = shared ]; then
        printf 'shared: entered wait\n' >> "$FAKE_ADB_LOG"
        sleep 0.7
        printf 'shared: escaped timeout\n' >> "$FAKE_ADB_LOG"
    fi
fi
if [ "$3" = shell ] && [ "$4" = getprop ]; then
    if [ "$FAKE_BOOT_MODE" = property ]; then block_phase property; fi
    if [ "$FAKE_BOOT_MODE" = pending ]; then echo 0; exit 0; fi
    echo 1
    if [ "$FAKE_BOOT_MODE" = property_error ]; then exit 7; fi
fi
exit 0
)");
    for (const auto* mode : { "query", "wait", "property", "pending", "shared",
                              "property_error" }) {
        scoped_env timeout("ANDROID_EMULATOR_BOOT_TIMEOUT", "1");
        scoped_env boot_mode("FAKE_BOOT_MODE", mode);
        write_text(adb_log, "");
        const auto start = std::chrono::steady_clock::now();
        const auto boot_result
            = run_cli(root.path(), "run android --android-mode emulator");
        const auto elapsed = std::chrono::steady_clock::now() - start;
        require_true(
            boot_result.exit_code != 0,
            std::string("unfinished/failed boot must reject deployment: ")
                + mode + "\n" + boot_result.output
        );
        require_true(
            elapsed < std::chrono::milliseconds(2500),
            std::string("ADB commands must respect the shared boot deadline: ")
                + mode
        );
        const auto log = read_text(adb_log);
        require_not_contains(log, "escaped timeout", "hung ADB must be killed");
        require_not_contains(
            log, "install -r", "unfinished boot must not install"
        );
        require_not_contains(
            log, "shell monkey", "unfinished boot must not launch"
        );
        require_not_contains(
            log, "unexpected emulator",
            "failed query must not start an emulator"
        );
        require_contains(
            boot_result.output,
            std::string(mode) == "query" ? "adb devices failed"
                                         : "did not finish booting",
            "boot failure must identify the failed phase"
        );
    }
    write_executable_script(fake_bin / "adb", ready_adb);

    // Preserve automatic startup: an emulator may first appear offline before
    // becoming a connected candidate. The boot check still follows selection.
    scoped_env started_env(
        "FAKE_EMULATOR_STATE", (fake_root / "emulator-state").string()
    );
    write_executable_script(
        fake_bin / "emulator",
        "#!/usr/bin/env bash\n"
        "printf 'starting\\n' > \"$FAKE_EMULATOR_STATE\"\n"
    );
    write_executable_script(fake_bin / "adb", R"(#!/usr/bin/env bash
if [ "$1" = devices ]; then
    echo 'List of devices attached'
    if [ -f "$FAKE_EMULATOR_STATE" ]; then
        read -r state < "$FAKE_EMULATOR_STATE"
        if [ "$state" = starting ]; then
            echo 'emulator-5560 offline'
            printf 'query: offline\n' >> "$FAKE_ADB_LOG"
            printf 'ready\n' > "$FAKE_EMULATOR_STATE"
        else
            echo 'emulator-5560 device'
            printf 'query: device\n' >> "$FAKE_ADB_LOG"
        fi
    fi
    exit 0
fi
printf '%s\n' "$*" >> "$FAKE_ADB_LOG"
if [ "$3" = shell ] && [ "$4" = getprop ]; then
    echo 1
fi
exit 0
)");
    check_target("", "auto", "", "emulator-5560", "");
    const auto startup = read_text(adb_log);
    require_contains(
        startup, "query: offline", "startup must observe offline state"
    );
    const std::string boot_query
        = "-s emulator-5560 shell getprop sys.boot_completed";
    require_contains(
        startup, boot_query, "connected emulator must still pass the boot check"
    );
    require_true(
        startup.find("query: device") < startup.find(boot_query),
        "startup must wait for a connected emulator before the boot check"
    );

    scoped_env query_state(
        "FAKE_QUERY_STATE", (fake_root / "query-state").string()
    );
    scoped_env startup_timeout("ANDROID_EMULATOR_BOOT_TIMEOUT", "1");
    write_text(adb_log, "");
    write_executable_script(fake_bin / "adb", R"(#!/usr/bin/env bash
if [ "$1" = devices ]; then
    echo 'List of devices attached'
    if [ ! -f "$FAKE_QUERY_STATE" ]; then
        : > "$FAKE_QUERY_STATE"
        exit 0
    fi
    printf 'startup query: entered\n' >> "$FAKE_ADB_LOG"
    trap '' TERM
    sleep 3
    printf 'startup query: escaped timeout\n' >> "$FAKE_ADB_LOG"
    exit 0
fi
printf '%s\n' "$*" >> "$FAKE_ADB_LOG"
)");
    const auto startup_begin = std::chrono::steady_clock::now();
    const auto stuck_startup
        = run_cli(root.path(), "run android --android-mode auto");
    require_true(
        stuck_startup.exit_code != 0
            && std::chrono::steady_clock::now() - startup_begin
                < std::chrono::milliseconds(2500),
        "emulator discovery commands must share the bounded readiness deadline"
    );
    require_contains(
        stuck_startup.output, "starting android emulator",
        "fixture must enter startup"
    );
    require_contains(
        stuck_startup.output, "adb devices failed",
        "stuck startup query must fail"
    );
    require_contains(
        read_text(adb_log), "startup query: entered",
        "fixture must stall during discovery"
    );
    require_not_contains(
        read_text(adb_log), "escaped timeout", "startup query must be killed"
    );
    require_not_contains(
        read_text(adb_log), "install -r", "failed startup must not install"
    );
}

} // namespace ecosystem_test_support
