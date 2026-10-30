#include "test_cases.hpp"
#include "test_support.hpp"

namespace ecosystem_test_support {

namespace {
    void map_provider(const fs::path& consumer, const fs::path& provider) {
        const auto manifest
            = json::parse(read_text(consumer / "manifest.json"));
        const auto repository = manifest["artifacts"][0]["packages"]
                                        ["external_project"]["repository"]
                                            .get<std::string>();
        write_text(
            consumer / "ecosystem.local.json",
            json(
                { { "sources", { { repository, provider.string() } } } }
            ).dump(2)
        );
    }
}

void test_optional_features_build_and_export_selected_closure() {

    for (const bool external : { false, true }) {
        temp_dir simple;
        write_source_dependency_fixture(simple.path());
        const auto project = simple.path() / "consumer";
        const auto provider = simple.path() / "provider";
        auto authored = json::parse(read_text(project / "manifest.json"));
        auto& app = authored["artifacts"][1];
        app.erase("dependencies");
        app["optional_dependencies"] = json::array(
            { { { "artifact", external ? "imported:math" : "extra:library" },
                { "feature", "monitor" } } }
        );
        if (!external) {
            authored["artifacts"].erase(0);
            authored["artifacts"].push_back(
                { { "id", "extra:library" },
                  { "kind", "static_lib" },
                  { "owns", json::array({ "extra" }) } }
            );
            write_text(
                project / "include/extra.hpp", "#pragma once\nint extra();\n"
            );
            write_text(
                project / "src/extra.cpp",
                "#include <extra.hpp>\nint extra() { return 42; }\n"
            );
        }
        write_text(project / "manifest.json", authored.dump(2));
        const std::string call = external ? "answer({})" : "extra()";
        const std::string header = external ? "answer.hpp" : "extra.hpp";
        write_text(
            project / "src/main.cpp",
            std::string("#if CLIENT_FEATURE_MONITOR\n#include <")
                .append(header)
                .append(
                    ">\n#endif\nint main() {\n#if "
                    "CLIENT_FEATURE_MONITOR\nreturn "
                )
                .append(call)
                .append(" == 42 ? 0 : 1;\n#else\nreturn 0;\n#endif\n}\n")
        );
        fs::rename(provider, simple.path() / "saved-provider");
        auto executed = run_marx_cli(project, "run debug");
        require_true(
            executed.exit_code == 0,
            "separate default-off feature fixture: " + executed.output
        );
        require_not_contains(
            read_text(ecosystem::local_developer_cmakelists_path(project)),
            external ? "numbers" : "extra__library",
            "disabled local/direct-provider target is absent"
        );
        fs::rename(simple.path() / "saved-provider", provider);
        if (external)
            map_provider(project, provider);
        auto config = external
            ? json::parse(read_text(project / "ecosystem.local.json"))
            : json::object();
        config["features"] = json::array({ "monitor" });
        write_text(project / "ecosystem.local.json", config.dump(2));
        for (const auto* profile : { "debug", "release" }) {
            executed = run_marx_cli(project, std::string("run ") + profile);
            require_true(
                executed.exit_code == 0,
                "separate enabled feature fixture " + std::string(profile)
                    + ": " + executed.output
            );
        }
    }
    temp_dir root;
    const auto run_command = [&](const ecosystem::string_list& command) {
        return ecosystem::capture_command_result(command, root.path());
    };
    write_source_dependency_fixture(root.path());
    const auto project = root.path() / "consumer";
    const auto provider = root.path() / "provider";
    auto authored = json::parse(read_text(project / "manifest.json"));
    authored["facade"] = "client:api";
    authored["artifacts"].erase(1);
    authored["artifacts"].push_back(
        { { "id", "bridge:adapter" },
          { "kind", "static_lib" },
          { "owns", json::array({ "bridge" }) },
          { "dependencies", json::array({ "imported:math" }) } }
    );
    authored["artifacts"].push_back(
        { { "id", "client:api" },
          { "kind", "static_lib" },
          { "owns", json::array({ "client" }) },
          { "tests", { { "selftest", true } } },
          { "optional_dependencies",
            json::array(
                { { { "artifact", "bridge:adapter" },
                    { "feature", "monitor" } } }
            ) } }
    );
    write_text(project / "manifest.json", authored.dump(2));
    write_text(
        project / "include/client.hpp", "#pragma once\nint current_answer();\n"
    );
    write_text(
        project / "include/bridge.hpp", "#pragma once\nint observed_answer();\n"
    );
    write_text(
        project / "src/bridge.cpp",
        "#include <bridge.hpp>\n#include <answer.hpp>\nint observed_answer() { "
        "return answer({}); }\n"
    );
    write_text(
        project / "src/client.cpp",
        "#include <client.hpp>\n#if CLIENT_FEATURE_MONITOR\n#include "
        "<bridge.hpp>\n#endif\nint current_answer() {\n#if "
        "CLIENT_FEATURE_MONITOR\nreturn observed_answer();\n#else\nreturn "
        "7;\n#endif\n}\n"
    );
    write_text(
        project / "tests/client_tests.cpp",
        "#include <client.hpp>\nint main() {\n#if "
        "CLIENT_FEATURE_MONITOR\nreturn current_answer() == 42 ? 0 : "
        "1;\n#else\nreturn current_answer() == 7 ? 0 : 1;\n#endif\n}\n"
    );
    // There is no local provider at all during default-off acceptance.
    fs::rename(provider, root.path() / "saved-provider");
    const auto loaded = ecosystem::load_manifest(project / "manifest.json");
    require_true(
        loaded.value && loaded.errors.empty(), join_lines(loaded.errors)
    );
    const auto root_cmake
        = ecosystem::generate_cmakelists(*loaded.value, project);
    write_text(project / "CMakeLists.txt", root_cmake);
    require_not_contains(
        root_cmake, "numbers",
        "visitor has no optional provider or acquisition path"
    );
    const auto visitor = root.path() / "visitor";
    const auto install = root.path() / "default-sdk";
    for (const auto& command : std::vector<ecosystem::string_list> {
             { "cmake", "-S", project.string(), "-B", visitor.string(),
               "-DCMAKE_BUILD_TYPE=Debug" },
             { "cmake", "--build", visitor.string(), "-j2" },
             { "cmake", "--install", visitor.string(), "--prefix",
               install.string() } }) {
        const auto result = run_command(command);
        require_true(result.exit_code == 0, result.output);
    }
    const auto config
        = read_text(install / "lib/cmake/client/clientConfig.cmake");
    require_not_contains(
        config, "numbers", "disabled SDK has no optional find_dependency"
    );
    require_not_contains(
        read_text(install / "lib/cmake/client/clientTargets.cmake"), "bridge",
        "disabled SDK exports no adapter"
    );
    const auto downstream = root.path() / "downstream";
    write_text(
        downstream / "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.20)\nproject(downstream LANGUAGES "
        "CXX)\nfind_package(client CONFIG REQUIRED)\nadd_executable(use "
        "main.cpp)\ntarget_link_libraries(use PRIVATE client::client__api)\n"
    );
    write_text(
        downstream / "main.cpp",
        "#include <client.hpp>\nint main() {\n#if "
        "CLIENT_FEATURE_MONITOR\nreturn current_answer() == 42 ? 0 : "
        "1;\n#else\nreturn current_answer() == 7 ? 0 : 1;\n#endif\n}\n"
    );
    const auto use_sdk = [&](const fs::path& prefix, const std::string& label,
                             const fs::path& provider_config) {
        const auto build = root.path() / label;
        ecosystem::string_list configure {
            "cmake",
            "-S",
            downstream.string(),
            "-B",
            build.string(),
            "-Dclient_DIR=" + (prefix / "lib/cmake/client").string()
        };
        if (!provider_config.empty())
            configure.push_back("-Dnumbers_DIR=" + provider_config.string());
        for (const auto& command : std::vector<ecosystem::string_list> {
                 configure,
                 { "cmake", "--build", build.string(), "-j2" },
                 { (build / "use").string() } }) {
            const auto executed = run_command(command);
            require_true(
                executed.exit_code == 0,
                "installed SDK consumer " + label + ": " + executed.output
            );
        }
    };
    use_sdk(install, "use-default", {});
    auto result = run_engels_cli(project, "check tests");
    require_true(
        result.exit_code == 0,
        "default-off actual automatic tests: " + result.output
    );
    const auto debug_source
        = read_text(ecosystem::local_developer_cmakelists_path(project));
    require_not_contains(
        debug_source, "numbers",
        "unscoped developer graph also excludes optional-only providers"
    );
    // Unknown names fail without replacing the last valid graph.
    write_text(project / "ecosystem.local.json", R"({"features":["typo"]})");
    result = run_marx_cli(project, "build debug");
    require_true(result.exit_code == 5, result.output);
    require_contains(
        result.output, "unknown feature selection: typo",
        "no silent feature fallback"
    );
    require_true(
        read_text(ecosystem::local_developer_cmakelists_path(project))
            == debug_source,
        "invalid selection preserves valid generated CMake"
    );
    const auto repository
        = authored["artifacts"][0]["packages"]["external_project"]["repository"]
              .get<std::string>();
    write_text(
        project / "ecosystem.local.json",
        json(
            { { "features", { "monitor" } },
              { "sources", { { repository, provider.string() } } } }
        ).dump(2)
    );
    result = run_marx_cli(project, "build debug");
    require_true(
        result.exit_code == 5,
        "enabled missing provider must fail: " + result.output
    );
    require_contains(
        result.output, "sources[",
        "enabled failure identifies the missing provider"
    );
    fs::rename(root.path() / "saved-provider", provider);
    result = run_engels_cli(project, "check tests");
    require_true(
        result.exit_code == 0,
        "actual enabled automatic tests: " + result.output
    );
    const auto enabled_source
        = read_text(ecosystem::local_developer_cmakelists_path(project));
    require_contains(
        enabled_source, "CLIENT_FEATURE_MONITOR=1",
        "automatic tests and production receive selected feature"
    );
    const auto enabled_install = root.path() / "enabled-sdk";
    const auto installed = run_command(
        { "cmake", "--install",
          ecosystem::local_build_dir(project, "debug").string(), "--prefix",
          enabled_install.string() }
    );
    require_true(installed.exit_code == 0, installed.output);
    require_contains(
        read_text(enabled_install / "lib/cmake/client/clientConfig.cmake"),
        "find_dependency(numbers CONFIG)",
        "enabled export retains real transitive provider dependency"
    );
    require_contains(
        read_text(enabled_install / "lib/cmake/client/clientTargets.cmake"),
        "CLIENT_FEATURE_MONITOR=1",
        "installed usage requirements describe the actual selected feature set"
    );
    fs::path provider_config;
    for (const auto& entry :
         fs::recursive_directory_iterator(project / ".ecosystem/dependencies"))
        if (entry.path().filename() == "numbersConfig.cmake"
            && entry.path().parent_path().filename() == "numbers")
            provider_config = entry.path().parent_path();
    require_true(
        !provider_config.empty(),
        "real provider installed a usable package configuration"
    );
    use_sdk(enabled_install, "use-enabled", provider_config);
    result = run_marx_cli(project, "build release");
    require_true(
        result.exit_code == 0, "enabled Release graph: " + result.output
    );
    write_text(project / "ecosystem.local.json", R"({"features":[]})");
    fs::rename(provider, root.path() / "saved-provider");
    result = run_engels_cli(project, "check tests");
    require_true(
        result.exit_code == 0,
        "disabling again removes provider/feature and restores current tests: "
            + result.output
    );
    require_true(
        read_text(ecosystem::local_developer_cmakelists_path(project))
            == debug_source,
        "feature cutover exactly restores default graph"
    );
    require_true(
        read_text(project / "CMakeLists.txt") == root_cmake
            && json::parse(read_text(project / "manifest.json")) == authored,
        "local selection never rewrites authored manifest or visitor surface"
    );
}

void test_source_dependency_local_override_builds_and_installs_before_consumer() {
    {
        temp_dir selected;
        write_source_dependency_fixture(selected.path());
        const auto project = selected.path() / "consumer";
        const auto provider = selected.path() / "provider";
        map_provider(project, provider);
        auto authored = json::parse(read_text(provider / "manifest.json"));
        authored["install_artifacts"] = json::array({ "extra:library" });
        authored["artifacts"].push_back(
            { { "id", "missing:imported" },
              { "kind", "static_lib" },
              { "owns", json::array() },
              { "packages",
                { { "external_project",
                    { { "repository",
                        (selected.path() / "must-not-acquire").string() },
                      { "revision", "main" },
                      { "package", "unrelated" },
                      { "artifact", "core:library" } } } } } }
        );
        authored["artifacts"].push_back(
            { { "id", "extra:library" },
              { "kind", "static_lib" },
              { "owns", json::array({ "extra" }) },
              { "dependencies", json::array({ "missing:imported" }) } }
        );
        write_text(
            provider / "include/extra.hpp", "#pragma once\nint extra();\n"
        );
        write_text(provider / "src/extra.cpp", "int extra() { return 17; }\n");
        write_text(provider / "manifest.json", authored.dump(2));
        const auto result = run_marx_cli(project, "run debug app:main");
        require_true(
            result.exit_code == 0,
            "provider project-wide install intent must not enlarge a consumer "
            "export: "
                + result.output
        );
        for (const auto& entry : fs::recursive_directory_iterator(
                 project / ".ecosystem/dependencies"
             )) {
            if (entry.path().filename() == "numbersConfig.cmake")
                require_not_contains(
                    read_text(entry.path()), "unrelated",
                    "selected provider SDK has only its genuine dependencies"
                );
            require_true(
                entry.path().filename() != "libnumbers_extra_library.a",
                "unrequested companion must not enter the installed SDK"
            );
        }
        auto consumer = json::parse(read_text(project / "manifest.json"));
        consumer["artifacts"][0]["packages"]["external_project"]["artifact"]
            = "extra:library";
        write_text(project / "manifest.json", consumer.dump(2));
        const auto required = run_marx_cli(project, "build debug app:main");
        require_true(
            required.exit_code != 0,
            "a selected companion's missing dependency must still fail"
        );
        require_contains(
            required.output, "must-not-acquire",
            "failure identifies the genuinely required source"
        );
    }
    {
        temp_dir boundary;
        write_source_dependency_fixture(boundary.path());
        const auto project = boundary.path() / "consumer";
        map_provider(project, boundary.path() / "provider");
        const auto authored = project / "authored";
        write_text(authored / "keep.txt", "authored provider notes\n");
        fs::create_directories(project / ".ecosystem");
        fs::create_directory_symlink(
            authored, project / ".ecosystem/dependencies"
        );
        const auto rejected = run_marx_cli(project, "build debug");
        require_true(
            std::distance(
                fs::directory_iterator(authored), fs::directory_iterator()
            ) == 1,
            "provider preparation must not materialize SDK state through a "
            "symlink into authored files"
        );
        require_true(rejected.exit_code == 5, "redirected SDK state must fail");
        require_contains(
            rejected.output, "actor output path must not use a symlink",
            "SDK failure must identify the output boundary"
        );
        require_true(
            read_text(authored / "keep.txt") == "authored provider notes\n",
            "rejected SDK redirection preserves authored contents"
        );
    }
    {
        temp_dir boundary;
        write_source_dependency_fixture(boundary.path());
        const auto project = boundary.path() / "consumer";
        map_provider(project, boundary.path() / "provider");
        const auto initial = run_marx_cli(project, "build debug");
        require_true(initial.exit_code == 0, initial.output);
        fs::path generated;
        for (const auto& entry : fs::recursive_directory_iterator(
                 project / ".ecosystem/dependencies"
             ))
            if (entry.path().filename() == "CMakeLists.txt"
                && entry.path().parent_path().filename() == "project")
                generated = entry.path();
        require_true(
            !generated.empty(), "actual provider SDK graph must exist"
        );
        const auto authored = project / "authored.cmake";
        write_text(authored, "authored SDK rules\n");
        fs::remove(generated);
        fs::create_hard_link(authored, generated);
        const auto rejected = run_marx_cli(project, "build debug");
        require_true(
            rejected.exit_code == 5, "linked SDK graph must fail preflight"
        );
        require_contains(
            rejected.output, "actor output path must not use a hard link",
            "SDK graph aliases require the same output guard"
        );
        require_true(
            read_text(authored) == "authored SDK rules\n",
            "SDK generation must preserve the aliased authored inode"
        );
    }
    {
        temp_dir selected;
        write_source_dependency_fixture(selected.path());
        const auto project = selected.path() / "consumer";
        map_provider(project, selected.path() / "provider");
        auto authored = json::parse(read_text(project / "manifest.json"));
        const auto missing
            = (selected.path() / "must-not-acquire-consumer").string();
        authored["artifacts"].push_back(
            { { "id", "spare:imported" },
              { "kind", "static_lib" },
              { "owns", json::array() },
              { "packages",
                { { "external_project",
                    { { "repository", missing },
                      { "revision", "main" },
                      { "package", "aaa_unused" },
                      { "artifact", "unused:library" } } } } } }
        );
        write_text(project / "manifest.json", authored.dump(2));
        for (const auto* profile : { "debug", "release" }) {
            const auto choice = std::string(profile) + " app:main";
            const auto built = run_marx_cli(project, "build " + choice);
            require_true(
                built.exit_code == 0,
                "selected consumer must not acquire unrelated providers: "
                    + built.output
            );
            require_true(
                run_marx_cli(project, "run " + choice).exit_code == 0,
                "selected consumer must execute using its real mandatory "
                "provider"
            );
            require_not_contains(
                read_text(project / ".ecosystem/source/CMakeLists.txt"),
                "aaa_unused",
                "unselected provider must be absent from the generated graph"
            );
            const auto requested = run_marx_cli(
                project, std::string("build ") + profile + " spare:imported"
            );
            require_true(
                requested.exit_code == 5,
                "explicitly requested missing provider must still fail"
            );
            require_contains(
                requested.output, missing,
                "acquisition failure must identify the selected source"
            );
        }
        const auto complete = run_marx_cli(project, "build debug");
        require_true(
            complete.exit_code == 5,
            "unscoped preparation retains all declared provider requirements"
        );
        require_contains(
            complete.output, missing,
            "unscoped failure must preserve the real acquisition diagnostic"
        );
    }
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto provider = root.path() / "provider";
    const auto consumer = root.path() / "consumer";
    auto provider_manifest = json::parse(read_text(provider / "manifest.json"));
    provider_manifest["artifacts"].push_back(
        { { "id", "diagnostics:tool" },
          { "kind", "qt_app" },
          { "owns", json::array({ "diagnostics" }) },
          { "entry", "src/diagnostics.cpp" },
          { "packages", { { "qt", json::array({ "Core" }) } } } }
    );
    provider_manifest["artifacts"].push_back(
        { { "id", "unrelated:imported" },
          { "kind", "static_lib" },
          { "owns", json::array() },
          { "packages",
            { { "external_project",
                { { "repository", (root.path() / "must-not-acquire").string() },
                  { "revision", "main" },
                  { "package", "unused" },
                  { "artifact", "unused:library" } } } } } }
    );
    write_text(provider / "manifest.json", provider_manifest.dump(2));
    write_text(
        provider / "src/diagnostics.cpp",
        "#error unrelated-developer-tool-must-not-build\n"
    );
    map_provider(consumer, provider);
    auto result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0,
        "provider must install before consumer configuration: " + result.output
    );
    bool inspected_sdk = false;
    for (const auto& entry : fs::recursive_directory_iterator(
             consumer / ".ecosystem/dependencies"
         )) {
        if (entry.path().filename() != "CMakeLists.txt"
            || entry.path().parent_path().filename() != "project")
            continue;
        inspected_sdk = true;
        const auto generated = read_text(entry.path());
        require_not_contains(
            generated, "diagnostics__tool",
            "provider SDK must exclude unrelated developer targets"
        );
        require_not_contains(
            generated, "find_package(Qt6",
            "provider SDK must not discover unrelated Qt packages"
        );
        require_not_contains(
            generated, "ECOSYSTEM_BUILD_TESTS",
            "provider SDK must not expose an unused test graph switch"
        );
    }
    require_true(
        inspected_sdk,
        "actor-owned SDK source must be inspectable in consumer state"
    );
    require_true(
        run_marx_cli(consumer, "build debug imported:math").exit_code == 0,
        "an explicitly requested imported artifact must complete provider "
        "preparation"
    );
    require_true(
        run_marx_cli(consumer, "run debug").exit_code == 0,
        "installed include directories, C++20 features and library linkage "
        "must reach the C++17 consumer"
    );
    require_true(
        !fs::exists(provider / ".ecosystem")
            && !fs::exists(provider / "CMakeLists.txt"),
        "a local override must remain source-owned; preparation state belongs "
        "to the consumer"
    );
    const auto cmake = read_text(consumer / ".ecosystem/source/CMakeLists.txt");
    require_contains(
        cmake, "find_package(numbers CONFIG REQUIRED)",
        "consumer must find the installed package"
    );
    require_not_contains(
        cmake, provider.string(),
        "consumer generation must not bind provider private sources"
    );
    const auto release = run_marx_cli(consumer, "build release");
    require_true(
        release.exit_code == 0
            && run_marx_cli(consumer, "run release").exit_code == 0,
        "the same isolated provider SDK must build and execute in Release: "
            + release.output
    );
    write_text(
        provider / "src/answer.cpp",
        "#include <answer.hpp>\nint answer(std::span<const int>) { return 43; "
        "}\n"
    );
    write_text(
        consumer / "src/main.cpp",
        "#include <answer.hpp>\nint main() { return answer({}) == 43 ? 0 : 1; "
        "}\n"
    );
    result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0
            && run_marx_cli(consumer, "run debug").exit_code == 0,
        "mutable override changes must rebuild and reinstall before consumer "
        "relinking: "
            + result.output
    );
    write_text(provider / "src/answer.cpp", "#error provider-build-failed\n");
    result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 5,
        "provider failure must stop the consumer operation"
    );
    require_contains(
        result.output, "provider-build-failed",
        "provider failures must retain compiler diagnostics"
    );
}

void test_source_dependency_repository_selection_is_stable_and_explicit() {
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto provider = root.path() / "provider";
    const auto consumer = root.path() / "consumer";
    const auto git = [&](const ecosystem::string_list& args,
                         const fs::path& cwd) {
        auto command = ecosystem::string_list { "git" };
        command.insert(command.end(), args.begin(), args.end());
        const auto result = ecosystem::capture_command_result(command, cwd);
        require_true(
            result.exit_code == 0,
            "isolated Git operation must succeed: " + result.output
        );
        auto text = result.output;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        return text;
    };
    const auto commit = [&](const int answer) {
        write_text(
            provider / "src/answer.cpp",
            "#include <answer.hpp>\nint answer(std::span<const int>) { return "
                + std::to_string(answer) + "; }\n"
        );
        git({ "add", "." }, provider);
        git({ "-c", "user.name=Fixture", "-c",
              "user.email=fixture@example.invalid", "commit", "-qm",
              "provider change" },
            provider);
        return git({ "rev-parse", "HEAD" }, provider);
    };
    git({ "init", "-q", "-b", "main" }, provider);
    commit(42);
    const auto selected_commit = commit(43);
    auto manifest = json::parse(read_text(consumer / "manifest.json"));
    auto& dependency = manifest["artifacts"][0]["packages"]["external_project"];
    dependency["repository"] = provider.string();
    dependency["revision"] = "main";
    write_text(consumer / "manifest.json", manifest.dump(2));
    const auto expect = [&](const int value) {
        write_text(
            consumer / "src/main.cpp",
            "#include <answer.hpp>\nint main() { return answer({}) == "
                + std::to_string(value) + " ? 0 : 1; }\n"
        );
    };
    expect(43);
    auto result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0
            && run_marx_cli(consumer, "run debug").exit_code == 0,
        "repository selection must build a separately installed provider: "
            + result.output
    );
    fs::path selection;
    for (const auto& entry : fs::recursive_directory_iterator(
             consumer / ".ecosystem/dependencies"
         )) {
        if (entry.path().filename() == "selection.json")
            selection = entry.path();
    }
    require_true(
        !selection.empty(),
        "source resolution must record its exact local commit"
    );
    require_true(
        json::parse(read_text(selection)).at("commit") == selected_commit,
        "a named revision must resolve to the actual checked out commit"
    );
    const auto next_commit = commit(44);
    fs::rename(provider, root.path() / "offline-provider");
    result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0
            && run_marx_cli(consumer, "run debug").exit_code == 0,
        "ordinary rebuilds must use the pinned checkout without contacting the "
        "changed remote: "
            + result.output
    );
    const auto managed_source = selection.parent_path() / "source";
    write_text(
        managed_source / "src/answer.cpp", "#error changed managed checkout\n"
    );
    result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 5,
        "dirty managed sources must fail before building the consumer"
    );
    require_contains(
        result.output, "managed source checkout changed",
        "dirty source failure must explain the override path"
    );
    git({ "restore", "src/answer.cpp" }, managed_source);
    fs::rename(root.path() / "offline-provider", provider);
    dependency["revision"] = next_commit;
    write_text(consumer / "manifest.json", manifest.dump(2));
    expect(44);
    result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0
            && run_marx_cli(consumer, "run debug").exit_code == 0,
        "an explicit revision change must select and install the requested "
        "commit: "
            + result.output
    );
    require_true(
        json::parse(read_text(selection)).at("commit") == selected_commit,
        "new intent must not mutate the previous cached selection"
    );
    {
        map_provider(consumer, provider);
        write_text(
            provider / "src/answer.cpp",
            "#include <answer.hpp>\nint answer(std::span<const int>) { return "
            "45; }\n"
        );
        expect(45);
        result = run_marx_cli(consumer, "build debug");
        require_true(
            result.exit_code == 0
                && run_marx_cli(consumer, "run debug").exit_code == 0,
            "a mutable override must use the same installed package contract: "
                + result.output
        );
    }
    fs::remove(consumer / "ecosystem.local.json");
    expect(44);
    result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0
            && run_marx_cli(consumer, "run debug").exit_code == 0,
        "removing an override must restore the selected immutable repository "
        "state"
    );
}

void test_source_dependency_consumer_exports_are_relocatable() {
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto provider = root.path() / "provider";
    const auto consumer = root.path() / "consumer";
    auto manifest = json::parse(read_text(consumer / "manifest.json"));
    manifest["facade"] = "core:wrapper";
    manifest["artifacts"][1]
        = { { "id", "core:wrapper" },
            { "kind", "static_lib" },
            { "owns", json::array({ "wrapper" }) },
            { "dependencies", json::array({ "imported:math" }) } };
    write_text(consumer / "manifest.json", manifest.dump(2));
    write_text(
        consumer / "include/wrapper.hpp",
        "#pragma once\n#include <answer.hpp>\nint wrapped_answer();\n"
    );
    write_text(
        consumer / "src/wrapper.cpp",
        "#include <wrapper.hpp>\nint wrapped_answer() { return answer({}); }\n"
    );
    map_provider(consumer, provider);
    const auto result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0,
        "consumer library must build against the installed provider: "
            + result.output
    );
    const auto prefix = root.path() / "installed-client";
    auto run = [&](const ecosystem::string_list& args) {
        const auto command
            = ecosystem::capture_command_result(args, root.path());
        require_true(
            command.exit_code == 0,
            "relocated consumer operation failed: " + command.output
        );
    };
    run({ "cmake", "--install",
          ecosystem::local_build_dir(consumer, "debug").string(), "--prefix",
          prefix.string() });
    fs::path provider_prefix;
    for (const auto& entry : fs::recursive_directory_iterator(
             consumer / ".ecosystem/dependencies"
         )) {
        if (entry.path().filename() == "numbersConfig.cmake"
            && entry.path().string().find("/install/") != std::string::npos) {
            provider_prefix = entry.path()
                                  .parent_path()
                                  .parent_path()
                                  .parent_path()
                                  .parent_path();
        }
    }
    require_true(
        !provider_prefix.empty(), "provider install metadata must exist"
    );
    const auto relocated_provider = root.path() / "relocated-provider";
    fs::copy(provider_prefix, relocated_provider, fs::copy_options::recursive);
    fs::rename(provider, root.path() / "hidden-provider");
    fs::rename(consumer, root.path() / "hidden-consumer");
    const auto plain = root.path() / "plain";
    write_text(plain / "CMakeLists.txt", R"(cmake_minimum_required(VERSION 3.20)
project(plain LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(client CONFIG REQUIRED)
add_executable(plain main.cpp)
target_link_libraries(plain PRIVATE client::core__wrapper)
)");
    write_text(
        plain / "main.cpp",
        "#include <wrapper.hpp>\nint main() { return wrapped_answer() == 42 ? "
        "0 : 1; }\n"
    );
    const auto build = root.path() / "plain-build";
    run({ "cmake", "-S", plain.string(), "-B", build.string(),
          "-DCMAKE_PREFIX_PATH=" + prefix.string() + ";"
              + relocated_provider.string() });
    run({ "cmake", "--build", build.string() });
    run({ (build / "plain").string() });
    for (const auto& entry : fs::recursive_directory_iterator(prefix)) {
        if (entry.path().extension() != ".cmake")
            continue;
        const auto text = read_text(entry.path());
        for (const auto& private_path : { provider, consumer, provider_prefix })
            require_not_contains(
                text, private_path.string(),
                "consumer exports must not retain any private provider "
                "source/build/install location"
            );
    }
}

void test_source_dependency_diamond_uses_one_sdk_identity() {
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto provider = root.path() / "provider";
    const auto consumer = root.path() / "consumer";
    auto authored = json::parse(read_text(consumer / "manifest.json"));
    const auto imported = authored["artifacts"][0];
    json sources
        = { { "https://example.invalid/numbers.git", provider.string() } };
    authored["artifacts"] = json::array();
    for (const auto* branch : { "left", "right" }) {
        const auto project = root.path() / branch;
        const auto repository
            = std::string("https://example.invalid/") + branch + ".git";
        sources[repository] = project.string();
        const json wrapper = {
            { "id", branch },
            { "description", "Diamond branch" },
            { "cpp_standard", 20 },
            { "facade", "core:wrapper" },
            { "artifacts",
              json::array(
                  { imported,
                    { { "id", "core:wrapper" },
                      { "kind", "static_lib" },
                      { "owns", json::array({ branch }) },
                      { "dependencies", json::array({ "imported:math" }) } } }
              ) }
        };
        write_text(project / "manifest.json", wrapper.dump(2));
        write_text(
            project / "include" / (std::string(branch) + ".hpp"),
            "#pragma once\nint " + std::string(branch) + "_answer();\n"
        );
        write_text(
            project / "src" / (std::string(branch) + ".cpp"),
            "#include <answer.hpp>\n#include <" + std::string(branch)
                + ".hpp>\nint " + branch + "_answer() { return answer({}); }\n"
        );
        auto dependency = imported;
        dependency["id"] = std::string(branch) + ":imported";
        dependency["packages"]["external_project"]
            = { { "repository", repository },
                { "revision", "selected-revision" },
                { "package", branch },
                { "artifact", "core:wrapper" } };
        authored["artifacts"].push_back(dependency);
    }
    authored["artifacts"].push_back(
        { { "id", "app:main" },
          { "kind", "exe" },
          { "owns", json::array() },
          { "entry", "src/main.cpp" },
          { "dependencies",
            json::array({ "left:imported", "right:imported" }) } }
    );
    write_text(consumer / "manifest.json", authored.dump(2));
    write_text(
        consumer / "ecosystem.local.json",
        json({ { "sources", sources } }).dump(2)
    );
    write_text(
        consumer / "src/main.cpp",
        "#include <left.hpp>\n#include <right.hpp>\nint main() { return "
        "left_answer() == 42 && right_answer() == 42 ? 0 : 1; }\n"
    );
    const auto log = root.path() / "cmake.log";
    const auto tools = root.path() / "tools";
    write_executable_script(
        tools / "cmake",
        "#!/bin/sh\ncase \"$1\" in\n-S|--build|--install)\n"
        "printf '%s|%s\\n' \"$1\" \"$2\" >> "
        "\"$ECOSYSTEM_TEST_CMAKE_LOG\" ;;\nesac\n"
        "exec \"$ECOSYSTEM_TEST_REAL_CMAKE\" \"$@\"\n"
    );
    scoped_env cmake(
        "ECOSYSTEM_TEST_REAL_CMAKE", ecosystem::find_command_path("cmake")
    );
    scoped_env command_log("ECOSYSTEM_TEST_CMAKE_LOG", log.string());
    scoped_env path("PATH", tools.string() + ":" + current_path_env());
    const auto check_preparation = [&] {
        const auto calls = read_text(log);
        for (const auto* step : { "-S|", "--build|", "--install|" }) {
            std::istringstream lines(calls);
            std::string line;
            std::size_t count = 0;
            while (std::getline(lines, line))
                if (line.starts_with(step)
                    && line.find("/dependencies/numbers/") != std::string::npos)
                    ++count;
            require_true(
                count == 1,
                std::string("diamond must prepare the common SDK once for ")
                    + step + ": " + calls
            );
        }
    };
    const auto result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0,
        "actual diamond consumer must build: " + result.output
    );
    check_preparation();
    std::vector<fs::path> installed;
    for (const auto& entry : fs::recursive_directory_iterator(
             consumer / ".ecosystem/dependencies"
         )) {
        if (entry.path().filename() == "numbersConfig.cmake"
            && entry.path().string().find("/install/") != std::string::npos)
            installed.push_back(entry.path());
    }
    require_true(
        installed.size() == 1,
        "one package selection in a diamond must have exactly one installed "
        "SDK identity, found "
            + std::to_string(installed.size())
    );
    require_true(
        run_marx_cli(consumer, "run debug").exit_code == 0,
        "both branches must resolve the common provider's actual interface"
    );
    for (const auto* branch : { "left", "right" })
        require_true(
            !fs::exists(root.path() / branch / ".ecosystem"),
            "all transitive preparation state belongs to the root consumer"
        );
    // Completion is operation-local, not persistent evidence of a valid SDK.
    write_text(
        provider / "src/answer.cpp",
        "#include <answer.hpp>\nint answer(std::span<const int>) { return 43; "
        "}\n"
    );
    write_text(
        consumer / "src/main.cpp",
        "#include <left.hpp>\n#include <right.hpp>\nint main() { return "
        "left_answer() == 43 && right_answer() == 43 ? 0 : 1; }\n"
    );
    for (const auto* profile : { "debug", "release" }) {
        write_text(log, "");
        const auto build
            = run_marx_cli(consumer, std::string("build ") + profile);
        require_true(
            build.exit_code == 0,
            "a later operation must prepare the changed real SDK: "
                + build.output
        );
        check_preparation();
        require_true(
            run_marx_cli(consumer, std::string("run ") + profile).exit_code
                == 0,
            "both branches must observe the new provider value"
        );
    }
    // Different transitive exports still share the same provider SDK. The
    // second branch widens its export set rather than selecting all companions
    // up front or acquiring an independently built copy of the common library.
    auto expanded = json::parse(read_text(provider / "manifest.json"));
    expanded["install_artifacts"] = json::array({ "extra:library" });
    expanded["artifacts"].push_back(
        { { "id", "extra:library" },
          { "kind", "static_lib" },
          { "owns", json::array({ "extra" }) } }
    );
    write_text(provider / "include/extra.hpp", "#pragma once\nint extra();\n");
    write_text(provider / "src/extra.cpp", "int extra() { return 44; }\n");
    write_text(provider / "manifest.json", expanded.dump(2));
    auto right = json::parse(read_text(root.path() / "right/manifest.json"));
    auto& request = right["artifacts"][0]["packages"]["external_project"];
    request["artifact"] = "extra:library";
    write_text(root.path() / "right/manifest.json", right.dump(2));
    write_text(
        root.path() / "right/src/right.cpp",
        "#include <extra.hpp>\nint right_answer() { return extra(); }\n"
    );
    write_text(
        consumer / "src/main.cpp",
        "#include <left.hpp>\n#include <right.hpp>\nint main() { return "
        "left_answer() == 43 && right_answer() == 44 ? 0 : 1; }\n"
    );
    const auto widened = run_marx_cli(consumer, "run debug");
    require_true(
        widened.exit_code == 0,
        "later transitive export must widen the same SDK: " + widened.output
    );
    const auto exports
        = read_text(installed.front().parent_path() / "numbersTargets.cmake");
    require_contains(
        exports, "numbers::math__library", "first branch export retained"
    );
    require_contains(
        exports, "numbers::extra__library", "later branch export installed"
    );
    request["artifact"] = "missing:export";
    write_text(root.path() / "right/manifest.json", right.dump(2));
    const auto invalid = run_marx_cli(consumer, "build debug");
    require_true(
        invalid.exit_code == 5,
        "a later edge must validate its export even when the SDK is prepared"
    );
    require_contains(
        invalid.output,
        "provider artifact missing or kind mismatch: missing:export",
        "completed preparation must not conceal an invalid second branch"
    );
    request["artifact"] = "math:library";
    request["revision"] = "conflicting-revision";
    write_text(root.path() / "right/manifest.json", right.dump(2));
    const auto conflict = run_marx_cli(consumer, "build debug");
    require_true(
        conflict.exit_code == 5, "conflicting source intent must fail"
    );
    require_contains(
        conflict.output, "conflicting source selections for package numbers",
        "completed preparation must not mask source conflicts"
    );
    request["revision"] = "selected-revision";
    write_text(root.path() / "right/manifest.json", right.dump(2));
    auto cyclic = json::parse(read_text(provider / "manifest.json"));
    cyclic["artifacts"].push_back(authored["artifacts"][0]);
    cyclic["artifacts"][0]["dependencies"] = json::array({ "left:imported" });
    write_text(provider / "manifest.json", cyclic.dump(2));
    write_text(log, "");
    const auto cycle = run_marx_cli(consumer, "build debug");
    require_true(
        cycle.exit_code == 5, "an active provider is not a completed SDK"
    );
    require_contains(
        cycle.output, "source dependency cycle at left",
        "cycle diagnosis is preserved"
    );
    require_true(
        read_text(log).empty(), "cyclic SDKs must not be built or installed"
    );
}

void test_source_dependency_shared_and_interface_usage_requirements() {
    for (const auto* kind : { "shared_lib", "interface_lib" }) {
        temp_dir root;
        write_source_dependency_fixture(root.path());
        const auto provider = root.path() / "provider";
        const auto consumer = root.path() / "consumer";
        auto provider_manifest
            = json::parse(read_text(provider / "manifest.json"));
        provider_manifest["install_artifacts"]
            = json::array({ "companion:qt" });
        provider_manifest["artifacts"].push_back(
            { { "id", "companion:qt" },
              { "kind", "static_lib" },
              { "owns", json::array({ "adapter" }) },
              { "dependencies", json::array({ "math:library" }) },
              { "packages", { { "qt", json::array({ "Core" }) } } } }
        );
        write_text(
            provider / "include/adapter.hpp",
            "#pragma once\nint adapter_answer();\n"
        );
        write_text(
            provider / "src/adapter.cpp",
            "#include <QObject>\n#include <answer.hpp>\n#include "
            "<adapter.hpp>\nint adapter_answer() { QObject object; return "
            "answer({}) + (object.parent() ? 1 : 0); }\n"
        );
        provider_manifest["artifacts"][0]["kind"] = kind;
        auto consumer_manifest
            = json::parse(read_text(consumer / "manifest.json"));
        consumer_manifest["artifacts"][0]["kind"] = kind;
        auto adapter_import = consumer_manifest["artifacts"][0];
        adapter_import["id"] = "adapter:imported";
        adapter_import["kind"] = "static_lib";
        adapter_import["packages"]["external_project"]["artifact"]
            = "companion:qt";
        consumer_manifest["artifacts"].push_back(adapter_import);
        consumer_manifest["artifacts"][1]["dependencies"].push_back(
            "adapter:imported"
        );
        write_text(
            consumer / "src/main.cpp",
            "#include <answer.hpp>\n#include <adapter.hpp>\nint main() { "
            "return "
            "answer({}) == 42 && adapter_answer() == 42 ? 0 : 1; }\n"
        );
        const auto log = root.path() / "cmake.log";
        const auto tools = root.path() / "tools";
        write_executable_script(
            tools / "cmake",
            "#!/bin/sh\ncase \"$1\" in\n"
            "-S) echo configure >> \"$ECOSYSTEM_TEST_CMAKE_LOG\" ;;\n"
            "--build) echo build >> \"$ECOSYSTEM_TEST_CMAKE_LOG\" ;;\n"
            "--install) echo install >> \"$ECOSYSTEM_TEST_CMAKE_LOG\" ;;\n"
            "esac\nexec \"$ECOSYSTEM_TEST_REAL_CMAKE\" \"$@\"\n"
        );
        scoped_env cmake(
            "ECOSYSTEM_TEST_REAL_CMAKE", ecosystem::find_command_path("cmake")
        );
        scoped_env command_log("ECOSYSTEM_TEST_CMAKE_LOG", log.string());
        scoped_env path("PATH", tools.string() + ":" + current_path_env());
        write_text(provider / "manifest.json", provider_manifest.dump(2));
        write_text(consumer / "manifest.json", consumer_manifest.dump(2));
        if (std::string(kind) == "interface_lib") {
            fs::remove(provider / "src/answer.cpp");
            write_text(
                provider / "include/answer.hpp",
                "#pragma once\n#include <span>\ninline int "
                "answer(std::span<const int>) { return 42; }\n"
            );
        }
        map_provider(consumer, provider);
        const auto result = run_marx_cli(consumer, "build debug");
        require_true(
            result.exit_code == 0,
            "declared SDK must build successfully: " + result.output
        );
        const auto calls = read_text(log);
        require_true(
            calls.find("install\n") != std::string::npos
                && calls.find("install\n", calls.find("install\n") + 1)
                    == std::string::npos,
            "two imports of one package must prepare and install its SDK "
            "exactly once: "
                + calls
        );
        bool companion_installed = false;
        for (const auto& entry : fs::recursive_directory_iterator(
                 consumer / ".ecosystem/dependencies"
             )) {
            if (entry.path().filename() == "numbersTargets.cmake") {
                require_contains(
                    read_text(entry.path()), "numbers::companion__qt",
                    "declared SDK companions must retain their installed "
                    "exports"
                );
                companion_installed = true;
            }
        }
        require_true(
            companion_installed,
            "full declared SDK must install its Qt companion"
        );
        require_true(
            result.exit_code == 0
                && run_marx_cli(consumer, "run debug").exit_code == 0,
            std::string("installed usage requirements must work for ") + kind
                + ": " + result.output
        );
        consumer_manifest["artifacts"][2]["packages"]["external_project"]
                         ["artifact"] = "missing:export";
        write_text(consumer / "manifest.json", consumer_manifest.dump(2));
        write_text(log, "");
        const auto invalid = run_marx_cli(consumer, "build debug");
        require_true(
            invalid.exit_code == 5,
            "invalid second import must fail preparation"
        );
        require_contains(
            invalid.output,
            "provider artifact missing or kind mismatch: missing:export",
            "every import must be validated before preparation"
        );
        require_true(
            read_text(log).empty(),
            "invalid import must fail before any CMake command"
        );
        consumer_manifest["artifacts"][2]["packages"]["external_project"]
                         ["artifact"] = "companion:qt";
        consumer_manifest["artifacts"][2]["packages"]["external_project"]
                         ["revision"] = "conflicting-revision";
        write_text(consumer / "manifest.json", consumer_manifest.dump(2));
        const auto conflict = run_marx_cli(consumer, "build debug");
        require_true(
            conflict.exit_code == 5, "conflicting package revisions must fail"
        );
        require_contains(
            conflict.output,
            "conflicting source selections for package numbers",
            "conflicting aliases must report the package"
        );
        require_true(
            read_text(log).empty(),
            "conflicting import must fail before any CMake command"
        );
    }
}

void test_source_dependency_rejects_invalid_provider_contracts_before_consumer_configure() {
    const std::vector<std::pair<
        std::string, std::function<void(const fs::path&, const fs::path&)>>>
        cases {
            { "invalid provider manifest",
              [](const auto& provider, const auto&) {
                  write_text(provider / "manifest.json", "{ invalid JSON\n");
              } },
            { "provider package identity mismatch",
              [](const auto& provider, const auto&) {
                  auto value
                      = json::parse(read_text(provider / "manifest.json"));
                  value["id"] = "different";
                  write_text(provider / "manifest.json", value.dump(2));
              } },
            { "provider artifact missing or kind mismatch",
              [](const auto&, const auto& consumer) {
                  auto value
                      = json::parse(read_text(consumer / "manifest.json"));
                  value["artifacts"][0]["packages"]["external_project"]
                       ["artifact"] = "math:absent";
                  write_text(consumer / "manifest.json", value.dump(2));
              } },
            { "provider artifact missing or kind mismatch",
              [](const auto&, const auto& consumer) {
                  auto value
                      = json::parse(read_text(consumer / "manifest.json"));
                  value["artifacts"][0]["kind"] = "shared_lib";
                  write_text(consumer / "manifest.json", value.dump(2));
              } },
            { "provider artifact is not exported",
              [](const auto& provider, const auto&) {
                  auto value
                      = json::parse(read_text(provider / "manifest.json"));
                  value["facade"] = "extra:interface";
                  value["artifacts"].push_back(
                      { { "id", "extra:interface" },
                        { "kind", "interface_lib" },
                        { "owns", json::array({ "extra" }) } }
                  );
                  write_text(provider / "include/extra.hpp", "#pragma once\n");
                  write_text(provider / "manifest.json", value.dump(2));
              } },
            { "actor output path must not use a symlink",
              [](const auto&, const auto& consumer) {
                  const auto outside = consumer.parent_path() / "outside";
                  fs::create_directories(outside);
                  fs::create_directories(consumer / ".ecosystem");
                  fs::create_directory_symlink(
                      outside, consumer / ".ecosystem/dependencies"
                  );
              } }
        };
    for (const auto& [message, mutate] : cases) {
        temp_dir root;
        write_source_dependency_fixture(root.path());
        const auto provider = root.path() / "provider";
        const auto consumer = root.path() / "consumer";
        mutate(provider, consumer);
        map_provider(consumer, provider);
        const auto result = run_marx_cli(consumer, "build debug");
        require_true(
            result.exit_code == 5,
            "invalid provider contract must fail preparation: " + result.output
        );
        require_contains(
            result.output, message,
            "preparation failure must explain the violated contract"
        );
        require_true(
            !fs::exists(ecosystem::local_build_cache_path(consumer, "debug")),
            "provider contract failures must happen before consumer configure"
        );
        if (fs::exists(root.path() / "outside"))
            require_true(
                fs::is_empty(root.path() / "outside"),
                "dependency state preflight must not write through an escaping "
                "symlink"
            );
    }
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto provider = root.path() / "provider";
    const auto consumer = root.path() / "consumer";
    {
        map_provider(consumer, "../provider");
        const auto result = run_marx_cli(consumer, "build debug");
        require_true(
            result.exit_code == 5, "ambiguous relative overrides must fail"
        );
        require_contains(
            result.output, "must be an absolute path",
            "relative override failure must be actionable"
        );
    }
    const auto real_cmake = ecosystem::find_command_path("cmake");
    const auto tools = root.path() / "tools";
    write_executable_script(
        tools / "cmake",
        "#!/bin/sh\nif [ \"$1\" = --install ]; then\n  echo "
        "provider-install-failed >&2\n  exit 9\nfi\nexec "
        "\"$ECOSYSTEM_TEST_REAL_CMAKE\" \"$@\"\n"
    );
    scoped_env cmake("ECOSYSTEM_TEST_REAL_CMAKE", real_cmake);
    scoped_env path("PATH", tools.string() + ":" + current_path_env());
    map_provider(consumer, provider);
    const auto result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 5,
        "provider install failure must fail the consumer operation"
    );
    require_contains(
        result.output, "provider-install-failed",
        "install failure must preserve the native output"
    );
    require_true(
        !fs::exists(ecosystem::local_build_cache_path(consumer, "debug")),
        "failed installation cannot configure a consumer against incomplete or "
        "stale output"
    );
}

void test_source_dependency_rejects_ambiguous_authored_metadata() {
    const auto valid = sample_external_project_manifest();
    for (const auto& [field, value] :
         std::vector<std::pair<std::string, json>> {
             { "package", "" },
             { "package", "unsafe)" },
             { "artifact", "missing-colon" },
             { "artifact", "bad/path:library" },
             { "artifact", "math:bad)" },
             { "repository", "--upload-pack=unexpected" },
             { "repository", "repo\nname" },
             { "revision", "--unexpected" },
             { "revision", "ref\nname" },
             { "component", "old" },
             { "local_source_dir", "/silently-ignored" },
             { "package", 5 } }) {
        auto broken = valid;
        broken.components.front().stack["external_project"][field] = value;
        require_true(
            !ecosystem::validate_manifest(broken).empty(),
            "invalid provider metadata must fail before generation: " + field
        );
    }
}

} // namespace ecosystem_test_support
