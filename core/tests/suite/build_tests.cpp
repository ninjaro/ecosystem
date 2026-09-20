#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

void test_installed_libraries_are_relocatable() {
    for (const auto* kind : { "static_lib", "shared_lib" }) {
        temp_dir root;
        const auto provider = root.path() / "provider";
        json authored = {
            { "id", "portable" },
            { "description", "Relocatable library package" },
            { "version", "1.2.3" },
            { "facade", "core:lib" },
            { "artifacts",
              json::array(
                  { { { "id", "core:headers" },
                      { "kind", "interface_lib" },
                      { "owns", { "api" } },
                      { "packages", { { "cxxopts", { "cxxopts" } } } } },
                    { { "id", "core:base" },
                      { "kind", "static_lib" },
                      { "owns", { "base" } } },
                    { { "id", "core:lib" },
                      { "kind", kind },
                      { "owns", { "wrapper" } },
                      { "dependencies", { "core:base", "core:headers" } } } }
              ) }
        };
        authored["artifacts"][1]["tests"] = { { "gtest", true } };
        const auto gtest = root.path() / "packages/GTest";
        write_text(
            gtest / "GTestConfig.cmake", R"(if(NOT TARGET GTest::gtest_main)
  add_library(GTest::gtest_main INTERFACE IMPORTED)
endif()
set(GTest_FOUND TRUE)
)"
        );
        if (std::string(kind) == "shared_lib") {
            authored["facade"] = "app:app";
            authored["install_artifacts"] = { "core:lib" };
            authored["artifacts"].push_back(
                { { "id", "app:app" },
                  { "kind", "exe" },
                  { "owns", json::array() },
                  { "entry", "src/main.cpp" } }
            );
            write_text(provider / "src/main.cpp", "int main() { return 0; }\n");
        }
        write_text(provider / "manifest.json", authored.dump(2));
        write_text(
            provider / "include/api.hpp",
            "#pragma once\n#include <span>\n#include <cxxopts.hpp>\ninline int "
            "size(std::span<int> values) { return "
            "static_cast<int>(values.size()); }\n"
        );
        write_text(
            provider / "include/base.hpp", "#pragma once\nint base();\n"
        );
        write_text(provider / "src/base.cpp", "int base() { return 41; }\n");
        write_text(
            provider / "include/wrapper.hpp",
            "#pragma once\n#include \"api.hpp\"\nint answer();\n"
        );
        write_text(
            provider / "src/wrapper.cpp",
            "#include \"wrapper.hpp\"\n#include \"base.hpp\"\nint answer() { "
            "return base() + 1; }\n"
        );
        write_text(
            provider / "include/unowned.hpp",
            "#error not part of the public package\n"
        );
        const auto loaded
            = ecosystem::load_manifest(provider / "manifest.json");
        require_true(loaded.errors.empty(), join_lines(loaded.errors));
        const auto synced = ecosystem::sync_project(provider, *loaded.value);
        require_true(synced.errors.empty(), join_lines(synced.errors));
        const auto build = root.path() / "provider-build";
        const auto prefix = root.path() / "installed";
        auto run = [&](const std::vector<std::string>& args) {
            require_true(
                ecosystem::run_command(args, root.path()) == 0,
                "independent install/consumer command must succeed"
            );
        };
        run({ "cmake", "-S", provider.string(), "-B", build.string(),
              "-DGTest_DIR=" + gtest.string(),
              "-DCMAKE_INSTALL_INCLUDEDIR=include/portable",
              "-DCMAKE_INSTALL_LIBDIR=lib64" });
        run({ "cmake", "--build", build.string(), "--parallel", "2" });
        run({ "cmake", "--install", build.string(), "--prefix",
              prefix.string() });
        require_true(
            !fs::exists(prefix / "include/portable/unowned.hpp"),
            "export must retain explicit header ownership"
        );
        const auto relocated = root.path() / "relocated";
        fs::rename(prefix, relocated);
        fs::rename(provider, root.path() / "hidden-source");
        fs::rename(build, root.path() / "hidden-build");
        for (const auto& file :
             fs::directory_iterator(relocated / "lib64/cmake/portable")) {
            const auto text = read_text(file.path());
            for (const auto& forbidden : { provider, build, prefix })
                require_not_contains(
                    text, forbidden.string(),
                    "installed metadata must not retain source/build/install "
                    "locations"
                );
        }
        const auto consumer = root.path() / "consumer";
        write_text(
            consumer / "CMakeLists.txt", R"(cmake_minimum_required(VERSION 3.20)
project(consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(portable 1.2 CONFIG REQUIRED)
find_package(portable 1.2 CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE portable::core__lib)
)"
        );
        write_text(
            consumer / "main.cpp",
            "#include <wrapper.hpp>\nint main() { int data[2]{}; "
            "cxxopts::Options options(\"consumer\"); return answer() == 42 && "
            "size(data) == 2 ? 0 : 1; }\n"
        );
        run({ "cmake", "-S", consumer.string(), "-B",
              (root.path() / "consumer-build").string(),
              "-DGTest_DIR=" + gtest.string(),
              "-Dportable_DIR="
                  + (relocated / "lib64/cmake/portable").string() });
        run({ "cmake", "--build", (root.path() / "consumer-build").string(),
              "--parallel", "2" });
        run({ (root.path() / "consumer-build/consumer").string() });
    }

    temp_dir root;
    auto external = sample_external_project_manifest();
    external.components.back().artifacts.front().kind = "static_lib";
    external.components.back().file_units.clear();
    external.components.back().modules = { "wrapper" };
    const auto exported = ecosystem::sync_project(root.path(), external);
    require_true(
        exported.errors.empty(),
        "libraries linked to installed providers must export"
    );
    const auto cmake = read_text(root.path() / "CMakeLists.txt");
    require_contains(
        cmake, "find_dependency(packing CONFIG)",
        "consumer package metadata must rediscover the installed provider"
    );
    require_contains(
        cmake, "install(EXPORT ",
        "normal consumer package export must remain enabled"
    );
    require_not_contains(
        cmake, "IMPORTED_LOCATION",
        "installed dependencies must not expose a private build path"
    );
}

void test_install_artifacts_build_and_install_independent_executables() {
    temp_dir root;
    write_sample_dual_run_project(root.path());
    auto value = sample_dual_run_manifest();
    value.install_artifacts = { "app:app", "tool:cli" };
    std::string error;
    require_true(
        ecosystem::save_manifest(root.path() / "manifest.json", value, &error),
        error
    );
    const auto loaded = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        loaded.errors.empty()
            && loaded.value->install_artifacts == value.install_artifacts,
        "install intent must round-trip"
    );
    require_true(
        ecosystem::sync_project(root.path(), value).errors.empty(),
        "multi-artifact facade must sync"
    );
    const fs::path build = root.path() / "build";
    const fs::path prefix = root.path() / "install";
    require_true(
        ecosystem::run_command(
            { "cmake", "-S", root.path().string(), "-B", build.string() },
            root.path()
        ) == 0,
        "independent executable targets must configure without executable link "
        "dependencies"
    );
    require_true(
        ecosystem::run_command(
            { "cmake", "--build", build.string(), "--parallel", "2" },
            root.path()
        ) == 0,
        "both install artifacts must build"
    );
    require_true(
        ecosystem::run_command(
            { "cmake", "--install", build.string(), "--prefix",
              prefix.string() },
            root.path()
        ) == 0,
        "both install artifacts must install"
    );
    std::set<std::string> installed;
    for (const auto& entry : fs::directory_iterator(prefix / "bin")) {
        installed.insert(entry.path().filename().string());
    }
    require_true(
        installed == std::set<std::string> { "app", "cli" },
        "install must contain exactly the declared executable outputs"
    );
    value.install_artifacts = { "unknown:missing", "unknown:missing" };
    const auto errors = join_lines(ecosystem::validate_manifest(value));
    require_contains(
        errors, "does not resolve",
        "unresolved install intent must fail validation"
    );
    require_contains(
        errors, "duplicate", "duplicate install intent must fail validation"
    );
}

void test_visitor_facade_tracks_selected_artifact() {
    temp_dir root;
    const auto project = root.path() / "visitor project";
    write_sample_dual_run_project(project);
    write_text(
        project / "src/cli_main.cpp",
        "#include <iostream>\nint main(int argc, char** argv) {\n"
        "std::cout << \"worker\";\n"
        "for (int i = 1; i < argc; ++i) std::cout << ' ' << argv[i];\n"
        "std::cout << '\\n'; return argc > 1 ? 7 : 0; }\n"
    );
    auto value = sample_dual_run_manifest();
    value.install_artifacts = { "app:app", "tool:cli" };
    value.components[0].artifacts[0].name = "editor";
    value.components[1].artifacts[0].name = "worker";
    const auto build = project / "build";
    auto configure = [&]() {
        std::string error;
        require_true(
            ecosystem::save_manifest(project / "manifest.json", value, &error),
            error
        );
        require_true(
            ecosystem::sync_project(project, value).errors.empty(),
            "visitor surface must sync"
        );
        require_true(
            ecosystem::run_command(
                { "cmake", "-S", ".", "-B", "build" }, project
            ) == 0,
            "visitor configure must need no actor or internal artifact name"
        );
    };
    auto compile = [&]() {
        require_true(
            ecosystem::run_command({ "cmake", "--build", "build" }, project)
                == 0,
            "ordinary visitor build must materialize the facade"
        );
    };
    configure();
    compile();
    const auto cmake = read_text(project / "CMakeLists.txt");
    require_contains(
        cmake, "add_executable(app__app",
        "facade must preserve the canonical target"
    );
    require_contains(
        cmake, "OUTPUT_NAME editor",
        "facade must preserve the canonical output name"
    );
    require_not_contains(
        cmake, "add_executable(mvp", "facade must not duplicate the application"
    );
    require_true(
        fs::is_symlink(build / "mvp")
            && fs::equivalent(build / "mvp", build / "editor"),
        "mvp must point at the selected real executable"
    );
    require_true(
        !fs::read_symlink(build / "mvp").is_absolute(),
        "facade links must survive build-directory relocation"
    );
    const auto first = run_cli_with_binary(build / "mvp", project, "");
    require_true(
        first.exit_code == 0, "the three-command visitor smoke must succeed"
    );
    require_contains(
        first.output, "app-run", "only the selected executable must run"
    );

    const auto editor_time = fs::last_write_time(build / "editor");
    const auto worker_time = fs::last_write_time(build / "worker");
    value.facade_entry_artifact = "tool:cli";
    configure();
    compile();
    require_true(
        fs::equivalent(build / "mvp", build / "worker"),
        "selection changes must refresh the link without relinking"
    );
    fs::rename(build / "mvp", build / "removed-facade");
    compile();
    require_true(
        fs::is_symlink(build / "mvp"),
        "ordinary rebuild must restore a removed facade"
    );
    require_true(
        fs::last_write_time(build / "editor") == editor_time
            && fs::last_write_time(build / "worker") == worker_time,
        "facade maintenance must not rebuild unchanged executables"
    );
    const auto arguments
        = run_cli_with_binary(build / "mvp", project, "'two words'");
    require_true(
        arguments.exit_code == 7,
        "facade must preserve the real executable exit status"
    );
    require_contains(
        arguments.output, "worker two words",
        "facade must preserve application arguments"
    );
    const auto moved = project / "relocated build";
    fs::rename(build, moved);
    require_true(
        run_cli_with_binary(moved / "mvp", project, "").exit_code == 0,
        "relative facade must run after moving the build directory"
    );
    fs::rename(moved, build);

    // Switching from an alias to a real output named mvp must not leave a link
    // through which the linker can overwrite the previously selected output.
    value.components[1].artifacts[0].name = "mvp";
    configure();
    compile();
    require_true(
        !fs::is_symlink(build / "mvp"),
        "a canonical mvp output must not link to itself or overwrite its "
        "predecessor"
    );
    const auto native_time = fs::last_write_time(build / "mvp");
    configure();
    compile();
    require_true(
        fs::last_write_time(build / "mvp") == native_time,
        "reconfigure must preserve a canonical mvp executable"
    );
    require_true(
        fs::last_write_time(build / "worker") == worker_time,
        "changing facade output must preserve the previous executable"
    );

    value.components[1].artifacts[0].name = "worker";
    configure();
    compile();
    const auto api = sample_interface_library_manifest();
    value.components.push_back(api.components.front());
    value.facade_entry_artifact = api.facade_entry_artifact;
    write_text(project / "include/sample/api.hpp", "#pragma once\n");
    configure();
    compile();
    require_true(
        !fs::exists(build / "mvp") && !fs::is_symlink(build / "mvp"),
        "a library facade must retire the prior visitor link without inventing "
        "an app"
    );
}

void test_visitor_facade_handles_runtime_output_layouts() {
    for (const bool multi : { false, true }) {
        if (multi && !ecosystem::probe_tool("ninja").available)
            continue;
        temp_dir root;
        write_sample_build_project(root.path(), "sample");
        auto value = sample_build_manifest("sample");
        value.components.front().artifacts.front().name = "mvp";
        require_true(
            ecosystem::sync_project(root.path(), value).errors.empty(),
            "layout fixture must sync"
        );
        const auto build = root.path() / "build";
        std::vector<std::string> args {
            "cmake",
            "-S",
            ".",
            "-B",
            "build",
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY="
                + (build / "runtime outputs").string()
        };
        if (multi)
            args.insert(args.end(), { "-G", "Ninja Multi-Config" });
        require_true(
            ecosystem::run_command(args, root.path()) == 0,
            "custom runtime layout must configure"
        );
        for (const std::string& config : multi
                 ? std::vector<std::string> { "Debug", "Release" }
                 : std::vector<std::string> { "Debug" }) {
            require_true(
                ecosystem::run_command(
                    { "cmake", "--build", "build", "--config", config },
                    root.path()
                ) == 0,
                "configured facade must build"
            );
            const auto actual
                = build / "runtime outputs" / (multi ? config : "") / "mvp";
            require_true(
                fs::is_symlink(build / "mvp")
                    && fs::equivalent(build / "mvp", actual),
                "facade must follow the target file in the selected "
                "configuration"
            );
            require_true(
                run_cli_with_binary(build / "mvp", root.path(), "").exit_code
                    == 0,
                "custom-layout facade must run"
            );
        }
        require_true(
            ecosystem::run_command(
                { "cmake", "--build", "build", "--target", "clean" },
                root.path()
            ) == 0,
            "clean must succeed"
        );
        require_true(
            !fs::is_symlink(build / "mvp"),
            "clean must remove generated facade files"
        );
    }
}

void test_visitor_facade_rejects_conflicting_output_names() {
    for (const std::string name : { "mvp", "mvp.exe" }) {
        auto value = sample_dual_run_manifest();
        value.components[1].artifacts[0].name = name;
        const auto errors = join_lines(ecosystem::validate_manifest(value));
        require_contains(
            errors, "visitor facade output collision",
            "other artifacts must not overwrite the selected facade"
        );
        require_contains(
            errors, "tool:cli",
            "facade collision must name the conflicting artifact"
        );
        require_contains(
            errors, "app:app",
            "facade collision must identify the selected artifact"
        );
        value.facade_entry_artifact = "tool:cli";
        require_true(
            ecosystem::validate_manifest(value).empty(),
            "the selected artifact may already have the canonical mvp filename"
        );
    }
}

void test_library_facade_installs_library_target_and_headers() {
    temp_dir root;
    const ecosystem::manifest manifest_value = sample_library_manifest();
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake, "include(GNUInstallDirs)",
        "generated facade must use standard install-directory variables"
    );
    require_contains(
        generated_cmake, "install(TARGETS core__lib",
        "library-first facade must install the facade library target"
    );
    require_contains(
        generated_cmake, "ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}",
        "library-first facade must install archives under the standard lib dir"
    );
    require_contains(
        generated_cmake,
        "install(DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/include/\" "
        "DESTINATION "
        "${CMAKE_INSTALL_INCLUDEDIR})",
        "library-first facade must install its public headers"
    );
}

void test_interface_library_facade_installs_headers_without_target() {
    temp_dir root;
    const ecosystem::manifest manifest_value
        = sample_interface_library_manifest();
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake,
        "install(DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/include/\" "
        "DESTINATION "
        "${CMAKE_INSTALL_INCLUDEDIR})",
        "header-only facade must still install its public headers"
    );
    require_contains(
        generated_cmake, "install(TARGETS api__api EXPORT ",
        "header-only facade must export its interface usage requirements"
    );
}

void test_runnable_facade_skips_linked_static_library_install_surface() {
    temp_dir root;
    const ecosystem::manifest manifest_value = sample_dependency_manifest();
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake, "install(TARGETS app__app",
        "runnable facade must install its runnable entry target"
    );
    require_not_contains(
        generated_cmake, "install(TARGETS core__lib",
        "runnable facade must not install linked static libraries"
    );
    require_not_contains(
        generated_cmake,
        "install(DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/include/\" "
        "DESTINATION "
        "${CMAKE_INSTALL_INCLUDEDIR})",
        "runnable facade must not install headers for skipped static libraries"
    );
}

void test_runnable_facade_installs_linked_shared_library_closure() {
    temp_dir root;
    const ecosystem::manifest manifest_value = sample_shared_runtime_manifest();
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake, "install(TARGETS app__app",
        "runnable facade must install its runnable entry target"
    );
    require_contains(
        generated_cmake, "install(TARGETS core__lib",
        "runnable facade must install linked shared-library closure targets"
    );
    require_not_contains(
        generated_cmake, "install(TARGETS spare__helper",
        "runnable facade must not install unrelated shared libraries"
    );
}

void test_project_assets_are_staged_and_installed() {
    temp_dir root;
    std::error_code error;
    fs::create_directories(root.path() / "assets", error);
    require_true(!error, "asset fixture directory must be created");

    ecosystem::manifest manifest_value = sample_dependency_manifest();
    manifest_value.id = "sample_assets";
    manifest_value.install_assets = true;
    manifest_value.components.at(1).stack
        = json::object({ { "qt", json::array({ "Core", "Widgets" }) } });
    manifest_value.components.at(1).artifacts.front().kind = "qt_app";
    const std::string generated_cmake
        = ecosystem::generate_developer_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake, "ecosystem_stage_assets(app__app)",
        "runnable facade must stage project assets beside the build output"
    );
    require_contains(
        generated_cmake, "ecosystem_embed_assets(app__app)",
        "Qt applications must embed opted-in assets for packaged runtimes"
    );
    require_not_contains(
        generated_cmake, "ecosystem_embed_assets(benchmarks__bench)",
        "non-Qt executables must not receive Qt-generated resource objects"
    );
    require_not_contains(
        generated_cmake, "ecosystem_embed_assets(tests__tests)",
        "non-Qt test executables must not receive Qt-generated resource objects"
    );
    require_contains(
        generated_cmake,
        "DESTINATION ${CMAKE_INSTALL_DATADIR}/sample_assets/assets",
        "facade install must preserve project assets under its data directory"
    );
    require_contains(
        generated_cmake, "qt_add_resources(${target_name}",
        "the generated Qt-only embedding helper must use qt_add_resources"
    );
    require_contains(
        generated_cmake, "PREFIX \"/sample_assets\"",
        "embedded assets must use a project-owned resource prefix"
    );
}

void test_installed_actors_use_their_template_bundle() {
    temp_dir root;
    const auto install = root.path() / "install";
    require_true(
        ecosystem::run_command(
            { "cmake", "--install", test_cli_build_dir().string(), "--prefix",
              install.string() },
            root.path()
        ) == 0,
        "tooling install must include both actors and runtime templates"
    );
    const auto prefix = root.path() / "relocated";
    fs::rename(install, prefix);
    const auto bundle = prefix / "share/manifesto/templates";
    require_true(
        fs::is_regular_file(bundle / "tracked/.github/workflows/tests.yml.tpl"),
        "install must include hidden tracked template directories"
    );
    const auto surface = bundle / "cmake/surface_prefix.tpl";
    write_text(surface, "# installed bundle\n" + read_text(surface));
    write_text(
        bundle / "mutation/header.hpp.tpl",
        "// installed header\n#pragma once\n"
    );
    const auto project = root.path() / "project";
    write_sample_build_project(project, "sample");
    write_text(
        root.path() / "templates/cmake/surface_prefix.tpl",
        "{{wrong_workspace_version}}\n"
    );
    fs::create_directories(root.path() / "launchers");
    const auto marx = root.path() / "launchers/marx";
    fs::create_symlink(prefix / "bin/marx", marx);
    scoped_env no_override("MANIFESTO_TEMPLATE_ROOT", "");
    scoped_env no_legacy_override("ECOSYSTEM_TEMPLATE_ROOT", "");
    auto run = [&](const fs::path& binary, const std::string& args) {
        const auto result = run_cli_with_binary(binary, project, args);
        require_true(
            result.exit_code == 0,
            "installed command must succeed:\n" + result.output
        );
    };
    run(marx, "sync");
    require_contains(
        read_text(project / "CMakeLists.txt"), "# installed bundle",
        "installed actors must select their relocated bundle through a symlink "
        "launcher"
    );
    run(prefix / "bin/engels", "check repo");
    run(marx, "mutate add module sample:app helper");
    require_contains(
        read_text(project / "include/helper.hpp"), "installed header",
        "installed mutation must use the same bundle"
    );
    const auto overrides = root.path() / "overrides";
    write_text(
        overrides / "cmake/surface_prefix.tpl",
        "# explicit override\n" + read_text(surface)
    );
    {
        scoped_env override("MANIFESTO_TEMPLATE_ROOT", overrides.string());
        run(marx, "sync");
        require_contains(
            read_text(project / "CMakeLists.txt"), "# explicit override",
            "deliberate partial overrides must precede installed data"
        );
    }
    const auto previous = read_text(project / "CMakeLists.txt");
    fs::rename(surface, bundle / "cmake/surface_prefix.saved");
    auto failed = run_cli_with_binary(marx, project, "sync");
    require_true(
        failed.exit_code != 0,
        "missing bundle members must fail instead of borrowing source templates"
    );
    require_contains(
        failed.output, "cmake/surface_prefix.tpl",
        "missing template diagnostics must name the member"
    );
    require_contains(
        failed.output, bundle.string(),
        "missing template diagnostics must identify the selected bundle"
    );
    require_true(
        read_text(project / "CMakeLists.txt") == previous,
        "failed rendering must preserve tracked state"
    );
    fs::rename(bundle, prefix / "missing-bundle");
    failed = run_cli_with_binary(marx, project, "sync");
    require_true(
        failed.exit_code != 0,
        "a missing installation must not use an incidental checkout"
    );
    {
        scoped_env override(
            "MANIFESTO_TEMPLATE_ROOT",
            (fs::path(ECOS_TEST_SOURCE_DIR) / "templates").string()
        );
        run(marx, "sync");
    }
}

void test_build_tree_layout_nests_project_and_probe_profiles() {
    temp_dir root;

    const std::vector<std::pair<std::string, std::string>> project_cases {
        { "debug", ".ecosystem/build/project/desktop/debug/default" },
        { "release", ".ecosystem/build/project/desktop/release/default" },
        { "kde", ".ecosystem/build/project/desktop/debug/kde" },
        { "coverage", ".ecosystem/build/project/desktop/debug/coverage" },
        { "leaks", ".ecosystem/build/project/desktop/debug/leaks" },
        { "android", ".ecosystem/build/project/android/debug/default" },
    };

    for (const auto& [profile, expected_relative_path] : project_cases) {
        const fs::path expected_path = root.path() / expected_relative_path;
        require_true(
            ecosystem::local_build_dir(root.path(), profile) == expected_path,
            "project build layout must map profile `" + profile
                + "` to the nested reusable tree"
        );
        require_true(
            ecosystem::local_build_cache_path(root.path(), profile)
                == expected_path / "CMakeCache.txt",
            "build cache path must stay inside the nested build tree for `"
                + profile + "`"
        );
    }

    require_true(
        ecosystem::local_doctor_dir(root.path())
            == root.path() / ".ecosystem/build/probes",
        "doctor probe scope must live under the nested build root"
    );
}

} // namespace ecosystem_test_support
