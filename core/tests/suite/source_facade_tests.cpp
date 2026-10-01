#include "test_cases.hpp"
#include "test_support.hpp"

namespace ecosystem_test_support {

namespace {
    void write_tracked_cmake(const fs::path& project) {
        const auto loaded = ecosystem::load_manifest(project / "manifest.json");
        require_true(loaded.errors.empty(), join_lines(loaded.errors));
        write_text(
            project / "CMakeLists.txt",
            ecosystem::generate_cmakelists(*loaded.value, project)
        );
    }

    void facade_command(
        const fs::path& root, const ecosystem::string_list& arguments
    ) {
        const auto result = ecosystem::capture_command_result(arguments, root);
        require_true(
            result.exit_code == 0, "facade command failed: " + result.output
        );
    }

    std::string commit_facade(const fs::path& provider) {
        facade_command(provider, { "git", "add", "." });
        facade_command(
            provider,
            { "git", "-c", "user.name=Fixture", "-c",
              "user.email=fixture@example.invalid", "-c",
              "commit.gpgsign=false", "commit", "-qm", "provider facade" }
        );
        auto result = ecosystem::capture_command_result(
            { "git", "rev-parse", "HEAD" }, provider
        );
        require_true(result.exit_code == 0, result.output);
        while (
            !result.output.empty()
            && (result.output.back() == '\n' || result.output.back() == '\r'))
            result.output.pop_back();
        return result.output;
    }

    void
    use_facade_repository(const fs::path& provider, const fs::path& consumer) {
        // A visitor must consume only the committed facade, without a manifest
        // interpreter or installed actors in the provider checkout.
        fs::remove(provider / "manifest.json");
        facade_command(provider, { "git", "init", "-q", "-b", "main" });
        commit_facade(provider);
        auto manifest = json::parse(read_text(consumer / "manifest.json"));
        auto& dependency
            = manifest["artifacts"][0]["packages"]["external_project"];
        dependency["repository"] = provider.string();
        dependency["revision"] = "main";
        write_text(consumer / "manifest.json", manifest.dump(2));
    }
}

void test_source_facade_outer_preserves_installed_consumer() {
    for (const bool multi : { false, true }) {
        if (multi && !ecosystem::probe_tool("ninja").available)
            continue;
        temp_dir root;
        const auto project = root.path() / "space in path";
        write_source_dependency_fixture(project);
        const auto provider = project / "provider";
        const auto consumer = project / "consumer";
        const auto prefix = project / "installed provider";
        const auto build = project / "build";
        write_tracked_cmake(provider);
        use_facade_repository(provider, consumer);
        write_tracked_cmake(consumer);
        write_text(
            consumer / "src/main.cpp",
            "#include <answer.hpp>\n#ifndef OUTER_CACHE\n#error missing outer "
            "cache\n#endif\n"
            "int main() { return answer({}) == 42 && OUTER_CACHE == 7 ? 0 : 1; "
            "}\n"
        );
        auto configure_consumer = ecosystem::string_list {
            "cmake",
            "-S",
            consumer.string(),
            "-B",
            build.string(),
            "-DCMAKE_PREFIX_PATH=" + (project / "absent prefix").string() + ";"
                + prefix.string(),
            "-DCMAKE_CXX_FLAGS=-DOUTER_CACHE=7",
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY="
                + (build / "runtime outputs").string()
        };
        if (multi) {
            configure_consumer.insert(
                configure_consumer.end(), { "-G", "Ninja Multi-Config" }
            );
        }
        const auto configurations = multi
            ? ecosystem::string_list { "Debug", "Release", "Debug" }
            : ecosystem::string_list { "Debug" };
        fs::remove(consumer / "manifest.json");
        facade_command(project, configure_consumer);
        for (const auto& config : configurations) {
            facade_command(
                project,
                { "cmake", "--build", build.string(), "--config", config }
            );
            facade_command(project, { (build / "mvp").string() });
            require_true(
                fs::is_symlink(build / "mvp")
                    && !fs::read_symlink(build / "mvp").is_absolute(),
                "outer mvp follows a relocatable inner facade entry"
            );
            if (fs::exists(provider))
                fs::rename(provider, project / "hidden-provider");
        }
        write_text(
            consumer / "src/main.cpp",
            "#include <answer.hpp>\nint main() { return answer({}) == 42 ? 7 : "
            "1; }\n"
        );
        facade_command(
            project, { "cmake", "--build", build.string(), "--config", "Debug" }
        );
        require_true(
            ecosystem::capture_command_result(
                { (build / "mvp").string() }, project
            )
                    .exit_code
                == 7,
            "an ordinary outer rebuild must observe edited consumer sources"
        );
        fs::remove(build / "mvp");
        facade_command(
            project, { "cmake", "--build", build.string(), "--config", "Debug" }
        );
        require_true(
            fs::exists(build / "mvp"),
            "outer rebuild restores the visitor entry"
        );
        const auto installed = project / "installed client";
        facade_command(
            project,
            { "cmake", "--install", build.string(), "--config", "Debug",
              "--prefix", installed.string() }
        );
        require_true(
            ecosystem::capture_command_result(
                { (installed / "bin/main").string() }, project
            )
                    .exit_code
                == 7,
            "outer install delegates the selected executable and --prefix"
        );
        write_text(consumer / "src/main.cpp", "#error inner-compile-failure\n");
        const auto failed = ecosystem::capture_command_result(
            { "cmake", "--build", build.string() }, project
        );
        require_true(
            failed.exit_code != 0,
            "inner build errors must fail the outer build"
        );
        require_contains(
            failed.output, "inner-compile-failure",
            "native inner diagnostics must survive orchestration"
        );
        facade_command(
            project, { "cmake", "--build", build.string(), "--target", "clean" }
        );
        require_true(
            !fs::is_symlink(build / "mvp"),
            "outer clean retires generated visitor output"
        );
    }
}

void test_source_facade_outer_delegates_library_install() {
    for (const auto* wrapper_kind : { "static_lib", "shared_lib" }) {
        temp_dir root;
        write_source_dependency_fixture(root.path());
        const auto provider = root.path() / "provider";
        const auto consumer = root.path() / "consumer";
        const auto prefix = root.path() / "packages";
        const auto build = root.path() / "build";
        // A reference to externally visible data requires PIC when this archive
        // is linked into a shared wrapper. A constant-only function hides the
        // bug.
        write_text(
            provider / "src/answer.cpp",
            "#include <answer.hpp>\nint provider_answer = 42;\n"
            "int answer(std::span<const int>) { return provider_answer; }\n"
        );
        write_tracked_cmake(provider);
        use_facade_repository(provider, consumer);
        auto manifest = json::parse(read_text(consumer / "manifest.json"));
        manifest["facade"] = "core:wrapper";
        manifest["artifacts"][1]
            = { { "id", "core:wrapper" },
                { "kind", wrapper_kind },
                { "owns", json::array({ "wrapper" }) },
                { "dependencies", json::array({ "imported:math" }) } };
        write_text(consumer / "manifest.json", manifest.dump(2));
        write_text(
            consumer / "include/wrapper.hpp",
            "#pragma once\nint wrapped_answer();\n"
        );
        write_text(
            consumer / "src/wrapper.cpp",
            "#include <answer.hpp>\n#include <wrapper.hpp>\nint "
            "wrapped_answer() { "
            "return answer({}); }\n"
        );
        write_tracked_cmake(consumer);
        // The visitor consumes a library which itself has a source provider.
        const auto top = root.path() / "top";
        auto top_manifest = manifest;
        top_manifest["id"] = "top";
        top_manifest["facade"] = "core:top";
        top_manifest["artifacts"][1]["id"] = "core:top";
        top_manifest["artifacts"][1]["kind"] = "static_lib";
        top_manifest["artifacts"][1]["owns"] = json::array({ "top" });
        top_manifest["artifacts"][0]["kind"] = wrapper_kind;
        top_manifest["artifacts"][0]["packages"]["external_project"]["package"]
            = "client";
        top_manifest["artifacts"][0]["packages"]["external_project"]["artifact"]
            = "core:wrapper";
        write_text(top / "manifest.json", top_manifest.dump(2));
        write_text(
            top / "include/top.hpp", "#pragma once\nint top_answer();\n"
        );
        write_text(
            top / "src/top.cpp",
            "#include <wrapper.hpp>\nint top_answer() { return "
            "wrapped_answer(); "
            "}\n"
        );
        use_facade_repository(consumer, top);
        write_tracked_cmake(top);
        fs::remove(top / "manifest.json");
        facade_command(
            root.path(),
            { "cmake", "-S", top.string(), "-B", build.string(),
              "-Dnumbers_DIR=" + (prefix / "lib/cmake/numbers").string() }
        );
        facade_command(root.path(), { "cmake", "--build", build.string() });
        require_true(
            !fs::exists(build / "mvp"),
            "a library outer facade does not invent an executable"
        );
        facade_command(
            root.path(),
            { "cmake", "--install", build.string(), "--prefix",
              prefix.string() }
        );
        require_true(
            fs::exists(prefix / "lib/cmake/top/topConfig.cmake")
                && fs::exists(prefix / "lib/cmake/client/clientConfig.cmake")
                && fs::exists(prefix / "lib/cmake/numbers/numbersConfig.cmake"),
            "outer library install exports its recursive provider closure"
        );
        for (const auto& entry : fs::recursive_directory_iterator(prefix)) {
            if (entry.path().extension() != ".cmake")
                continue;
            const auto metadata = read_text(entry.path());
            require_not_contains(
                metadata, root.path().string(),
                "installed package metadata must not retain source, build or "
                "original install paths"
            );
            require_not_contains(
                metadata, "ExternalProject_Add",
                "installed packages must only discover their dependencies"
            );
        }
        fs::rename(provider, root.path() / "hidden-provider");
        fs::rename(consumer, root.path() / "hidden-consumer");
        fs::rename(top, root.path() / "hidden-top");
        fs::rename(build, root.path() / "hidden-build");
        const auto relocated = root.path() / "relocated";
        fs::rename(prefix, relocated);
        const auto plain = root.path() / "plain";
        write_text(
            plain / "CMakeLists.txt",
            "cmake_minimum_required(VERSION 3.20)\nproject(plain LANGUAGES "
            "CXX)\n"
            "find_package(top CONFIG REQUIRED)\nadd_executable(plain "
            "main.cpp)\ntarget_link_libraries(plain PRIVATE "
            "top::core__top)\n"
        );
        write_text(
            plain / "main.cpp",
            "#include <top.hpp>\nint main() { return top_answer() == 42 ? "
            "0 : 1; }\n"
        );
        facade_command(
            root.path(),
            { "cmake", "-S", plain.string(), "-B", (plain / "build").string(),
              "-DCMAKE_PREFIX_PATH=" + relocated.string() }
        );
        facade_command(
            root.path(), { "cmake", "--build", (plain / "build").string() }
        );
        facade_command(root.path(), { (plain / "build/plain").string() });
    }
}

void test_source_facade_pins_revisions_and_rejects_stale_packages() {
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto provider = root.path() / "provider";
    const auto consumer = root.path() / "consumer";
    const auto build = root.path() / "build";
    write_tracked_cmake(provider);
    use_facade_repository(provider, consumer);
    auto manifest = json::parse(read_text(consumer / "manifest.json"));
    auto& revision
        = manifest["artifacts"][0]["packages"]["external_project"]["revision"];
    const auto select = [&](const std::string& ref) {
        revision = ref;
        write_text(consumer / "manifest.json", manifest.dump(2));
        write_tracked_cmake(consumer);
        facade_command(
            root.path(),
            { "cmake", "-S", consumer.string(), "-B", build.string() }
        );
    };
    const auto expect = [&](const int value) {
        write_text(
            consumer / "src/main.cpp",
            "#include <answer.hpp>\nint main() { return answer({}) == "
                + std::to_string(value) + " ? 0 : 1; }\n"
        );
        facade_command(root.path(), { "cmake", "--build", build.string() });
        facade_command(root.path(), { (build / "mvp").string() });
    };
    select("main");
    expect(42);
    facade_command(provider, { "git", "-c", "tag.gpgsign=false", "tag", "v1" });
    write_text(
        provider / "src/answer.cpp",
        "#include <answer.hpp>\nint answer(std::span<const int>) { return 43; "
        "}\n"
    );
    const auto next = commit_facade(provider);
    expect(42); // A branch moving upstream must not change an ordinary rebuild.
    select(next);
    require_true(
        !fs::exists(build / "mvp"),
        "selection changes retire the old facade entry before building"
    );
    expect(43);
    select("v1");
    expect(42);
    // Even a successful provider build cannot reuse the prior selection's
    // installed Config when the newly selected revision stops exporting it.
    write_text(
        provider / "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.20)\nproject(numbers LANGUAGES "
        "NONE)\ninstall(CODE \"message(STATUS no-package)\")\n"
    );
    select(commit_facade(provider));
    const auto failed = ecosystem::capture_command_result(
        { "cmake", "--build", build.string() }, root.path()
    );
    require_true(
        failed.exit_code != 0, "a selected revision without a package must fail"
    );
    require_contains(
        failed.output, "Provider numbers must install exactly one",
        "stale packages cannot satisfy new source intent"
    );
    require_true(
        !fs::exists(build / "mvp"),
        "failed selection cannot expose the previous executable"
    );
}

void test_source_facade_preserves_shared_and_interface_contracts() {
    for (const auto& kind : { "shared_lib", "interface_lib" }) {
        temp_dir root;
        write_source_dependency_fixture(root.path());
        const auto provider = root.path() / "provider";
        const auto consumer = root.path() / "consumer";
        const auto build = root.path() / "build";
        for (const auto& project : { provider, consumer }) {
            auto manifest = json::parse(read_text(project / "manifest.json"));
            manifest["artifacts"][0]["kind"] = kind;
            write_text(project / "manifest.json", manifest.dump(2));
        }
        if (std::string(kind) == "interface_lib") {
            fs::remove(provider / "src/answer.cpp");
            write_text(
                provider / "include/answer.hpp",
                "#pragma once\n#include <span>\ninline int "
                "answer(std::span<const int>) { return 42; }\n"
            );
            auto manifest = json::parse(read_text(provider / "manifest.json"));
            manifest["install_artifacts"] = json::array({ "extra:library" });
            manifest["artifacts"].push_back(
                { { "id", "extra:library" },
                  { "kind", "interface_lib" },
                  { "owns", json::array({ "extra" }) } }
            );
            write_text(provider / "manifest.json", manifest.dump(2));
            write_text(
                provider / "include/extra.hpp",
                "#pragma once\ninline constexpr int extra = 42;\n"
            );
        }
        write_tracked_cmake(provider);
        use_facade_repository(provider, consumer);
        if (std::string(kind) == "interface_lib") {
            auto manifest = json::parse(read_text(consumer / "manifest.json"));
            auto additional = manifest["artifacts"][0];
            additional["id"] = "extra:imported";
            additional["packages"]["external_project"]["artifact"]
                = "extra:library";
            manifest["artifacts"][1]["dependencies"].push_back(
                "extra:imported"
            );
            manifest["artifacts"].push_back(additional);
            write_text(consumer / "manifest.json", manifest.dump(2));
            write_text(
                consumer / "src/main.cpp",
                "#include <answer.hpp>\n#include <extra.hpp>\nint main() { "
                "return answer({}) == extra ? 0 : 1; }\n"
            );
        }
        write_tracked_cmake(consumer);
        fs::remove(consumer / "manifest.json");
        facade_command(
            root.path(),
            { "cmake", "-S", consumer.string(), "-B", build.string(),
              "-DCMAKE_INSTALL_LIBDIR=lib64" }
        );
        facade_command(root.path(), { "cmake", "--build", build.string() });
        facade_command(root.path(), { (build / "mvp").string() });
        int checkouts = 0, provider_builds = 0;
        for (const auto& entry :
             fs::recursive_directory_iterator(build / ".manifesto-facade")) {
            if (entry.path().filename() == ".git")
                ++checkouts;
            if (entry.path().filename() == "CMakeCache.txt"
                && entry.path().parent_path().parent_path().filename()
                    == "providers")
                ++provider_builds;
        }
        require_true(
            checkouts == 1 && provider_builds == 1,
            "multiple requested artifacts share one provider acquisition and "
            "build"
        );
    }
}

void test_source_facade_reports_provider_failures() {
    for (const bool nested : { false, true }) {
        for (const std::string stage :
             { "acquire", "revision", "configure", "build", "install",
               "package", "target", "cycle" }) {
            temp_dir root;
            write_source_dependency_fixture(root.path());
            const auto provider = root.path() / "provider";
            const auto consumer = root.path() / "consumer";
            const auto build = root.path() / "build";
            if (stage == "cycle") {
                auto manifest
                    = json::parse(read_text(provider / "manifest.json"));
                auto recursive = json::parse(
                    read_text(consumer / "manifest.json")
                )["artifacts"][0];
                recursive["packages"]["external_project"]["repository"]
                    = provider.string();
                recursive["packages"]["external_project"]["revision"] = "main";
                manifest["artifacts"][0]["dependencies"]
                    = json::array({ "imported:math" });
                manifest["artifacts"].push_back(recursive);
                write_text(provider / "manifest.json", manifest.dump(2));
            }
            write_tracked_cmake(provider);
            std::string expected;
            if (stage == "cycle") {
                expected = "Cyclic facade provider selection";
            } else if (stage == "configure") {
                expected = "fixture-provider-configure";
                write_text(
                    provider / "CMakeLists.txt",
                    read_text(provider / "CMakeLists.txt")
                        + "\nmessage(FATAL_ERROR fixture-provider-configure)\n"
                );
            } else if (stage == "build") {
                expected = "fixture-provider-compile";
                write_text(
                    provider / "src/answer.cpp",
                    "#error fixture-provider-compile\n"
                );
            } else if (stage == "install") {
                expected = "fixture-provider-install";
                write_text(
                    provider / "CMakeLists.txt",
                    read_text(provider / "CMakeLists.txt")
                        + "\ninstall(CODE \"message(FATAL_ERROR "
                          "fixture-provider-install)\")\n"
                );
            } else if (stage == "package") {
                expected = "Provider numbers must install exactly one";
                write_text(
                    provider / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\nproject(numbers "
                    "LANGUAGES NONE)\ninstall(CODE \"message(STATUS "
                    "no-package)\")\n"
                );
            }
            use_facade_repository(provider, consumer);
            auto manifest = json::parse(read_text(consumer / "manifest.json"));
            auto& dependency
                = manifest["artifacts"][0]["packages"]["external_project"];
            if (stage == "acquire") {
                expected = "missing-repository";
                dependency["repository"] = (root.path() / expected).string();
            } else if (stage == "revision") {
                expected = "missing-revision";
                dependency["revision"] = expected;
            } else if (stage == "target") {
                expected = "numbers::math__missing";
                dependency["artifact"] = "math:missing";
            }
            write_text(consumer / "manifest.json", manifest.dump(2));
            write_tracked_cmake(consumer);
            auto visitor = consumer;
            auto configure
                = ecosystem::string_list { "cmake", "-S", visitor.string(),
                                           "-B", build.string() };
            const auto top_configured = root.path() / "top-consumer-configured";
            if (nested) {
                // The original consumer becomes a provider of the outer
                // visitor. Keep its failing leaf intent, including cycle/target
                // failures.
                auto outer = manifest;
                manifest["facade"] = "core:wrapper";
                manifest["artifacts"][1]
                    = { { "id", "core:wrapper" },
                        { "kind", "static_lib" },
                        { "owns", json::array({ "wrapper" }) },
                        { "dependencies", json::array({ "imported:math" }) } };
                write_text(consumer / "manifest.json", manifest.dump(2));
                write_text(
                    consumer / "include/wrapper.hpp",
                    "#pragma once\nint wrapped_answer();\n"
                );
                write_text(
                    consumer / "src/wrapper.cpp",
                    "#include <answer.hpp>\nint wrapped_answer() { return "
                    "answer({}); }\n"
                );
                write_tracked_cmake(consumer);
                visitor = root.path() / "top";
                outer["id"] = "top";
                auto& imported
                    = outer["artifacts"][0]["packages"]["external_project"];
                imported["package"] = "client";
                imported["artifact"] = "core:wrapper";
                write_text(visitor / "manifest.json", outer.dump(2));
                write_text(
                    visitor / "src/main.cpp",
                    "#include <wrapper.hpp>\nint main() { return "
                    "wrapped_answer() == 42 ? 0 : 1; }\n"
                );
                use_facade_repository(consumer, visitor);
                write_tracked_cmake(visitor);
                fs::remove(visitor / "manifest.json");
                const auto hook = root.path() / "observe-top.cmake";
                write_text(
                    hook,
                    "if(MANIFESTO_FACADE_INNER)\nfile(WRITE [=["
                        + top_configured.string() + "]=] configured)\nendif()\n"
                );
                configure = { "cmake",
                              "-S",
                              visitor.string(),
                              "-B",
                              build.string(),
                              "-DCMAKE_PROJECT_top_INCLUDE=" + hook.string() };
            }
            facade_command(root.path(), configure);
            const auto failed = ecosystem::capture_command_result(
                { "cmake", "--build", build.string() }, root.path()
            );
            require_true(
                failed.exit_code != 0, "provider stage must fail: " + stage
            );
            require_contains(
                failed.output, expected,
                "native provider failure must remain attributable: " + stage
            );
            require_contains(
                failed.output, "numbers",
                "provider identity must survive orchestration"
            );
            if (nested) {
                require_contains(
                    failed.output, "Provider client:",
                    "nested failure must retain its enclosing provider identity"
                );
                require_true(
                    !fs::exists(top_configured),
                    "leaf provider failure must prevent outer consumer "
                    "configuration: "
                        + stage
                );
                if (stage == "target") {
                    // Prove the observation hook runs before a missing package
                    // can abort an actual outer-consumer configure.
                    const auto control = ecosystem::capture_command_result(
                        { "cmake", "-S", visitor.string(), "-B",
                          (root.path() / "control").string(),
                          "-DMANIFESTO_FACADE_INNER=ON",
                          "-DCMAKE_PROJECT_top_INCLUDE="
                              + (root.path() / "observe-top.cmake").string() },
                        root.path()
                    );
                    require_true(
                        control.exit_code != 0 && fs::exists(top_configured),
                        "configure observation must detect an attempted inner "
                        "configure: "
                            + control.output
                    );
                }
            }
            if (stage != "target") {
                for (const auto& entry : fs::recursive_directory_iterator(
                         build / ".manifesto-facade"
                     )) {
                    require_true(
                        entry.path().filename() != "CMakeCache.txt"
                            || entry.path().parent_path().filename()
                                != "consumer",
                        "provider failure must precede consumer configuration: "
                            + stage
                    );
                }
            }
            require_true(
                !fs::exists(build / "mvp"),
                "failed providers cannot produce a visitor executable"
            );
        }
    }
}

void test_external_project_generates_imported_library() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_external_project_manifest();
    require_true(
        ecosystem::validate_manifest(manifest_value).empty(),
        "valid external project intent must pass manifest validation"
    );

    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());
    require_contains(
        generated_cmake, "find_package(packing CONFIG REQUIRED)",
        "external components must consume installed CMake packages"
    );
    require_contains(
        generated_cmake,
        "add_library(packing_library__core ALIAS packing::library__core)",
        "the local authored identity must alias the provider target and all "
        "its usage requirements"
    );
    const auto inner
        = ecosystem::generate_developer_cmakelists(manifest_value, root.path());
    require_contains(
        generated_cmake, "if(NOT MANIFESTO_FACADE_INNER)",
        "tracked source consumers separate orchestration from the package graph"
    );
    require_not_contains(
        inner, "ExternalProject_Add",
        "Marx already prepares providers before configuring its consumer graph"
    );
    for (const auto* forbidden :
         { "IMPORTED_LOCATION", "_binary_dir", "copy_directory" }) {
        require_not_contains(
            generated_cmake, forbidden,
            "consumers must not synthesize provider build layouts"
        );
    }

    auto grouped = manifest_value;
    auto second = grouped.components.front();
    second.id = "packing_extra";
    second.stack["external_project"]["artifact"] = "library:extra";
    grouped.components.back().artifacts.front().link.push_back(
        "packing_extra:core"
    );
    grouped.components.push_back(second);
    const auto grouped_cmake
        = ecosystem::generate_cmakelists(grouped, root.path());
    require_contains(
        grouped_cmake, "ALIAS packing::library__extra",
        "each requested target retains its own installed target contract"
    );
    for (const auto* field : { "repository", "revision" }) {
        auto conflict = grouped;
        conflict.components.back().stack["external_project"][field]
            = "conflicting-selection";
        bool rejected = false;
        try {
            ecosystem::generate_cmakelists(conflict, root.path());
        } catch (const std::exception& error) {
            require_contains(
                error.what(),
                "conflicting facade provider source selections for package "
                "packing",
                "conflicting direct provider identity must be actionable"
            );
            rejected = true;
        }
        require_true(
            rejected,
            "conflicting identities must fail before producing a facade"
        );
    }

    manifest_value.components.front().modules = { "packing/geometry" };
    const ecosystem::string_list errors
        = ecosystem::validate_manifest(manifest_value);
    require_true(
        std::any_of(
            errors.begin(), errors.end(),
            [](const std::string& error) {
                return error.find("must not redeclare repository-owned")
                    != std::string::npos;
            }
        ),
        "external source modules must remain owned by their repository"
    );
}

void test_android_source_facade(const fs::path& ndk) {
    const auto toolchain = ndk / "build/cmake/android.toolchain.cmake";
    require_true(
        fs::is_regular_file(toolchain), "Android NDK toolchain must exist"
    );
    for (const std::string abi : { "arm64-v8a", "x86_64" }) {
        for (const auto* kind : { "static_lib", "shared_lib" }) {
            temp_dir root;
            const auto project = root.path() / "cross build with spaces";
            write_source_dependency_fixture(project);
            const auto provider = project / "provider";
            const auto consumer = project / "consumer";
            const auto build = project / "build";
            const auto prefix = project / "installed";
            const auto architecture
                = abi == "arm64-v8a" ? "__aarch64__" : "__x86_64__";
            write_text(
                provider / "include/answer.hpp",
                "#pragma once\n#include <span>\n#if !defined(__ANDROID__) || "
                "!defined("
                    + std::string(architecture)
                    + ")\n#error wrong provider or consumer target\n#endif\n"
                      "static_assert(__ANDROID_API__ == 28);\n"
                      "int answer(std::span<const int>);\n"
            );
            write_text(
                provider / "src/answer.cpp",
                "#include <answer.hpp>\nint provider_answer = 42;\n"
                "int answer(std::span<const int>) { return provider_answer; }\n"
            );
            for (const auto& source : { provider, consumer }) {
                auto manifest
                    = json::parse(read_text(source / "manifest.json"));
                manifest["artifacts"][0]["kind"] = kind;
                write_text(source / "manifest.json", manifest.dump(2));
            }
            write_tracked_cmake(provider);
            use_facade_repository(provider, consumer);
            write_tracked_cmake(consumer);
            fs::remove(consumer / "manifest.json");
            const auto configure = [&](const fs::path& source,
                                       const fs::path& binary,
                                       const ecosystem::string_list& extra) {
                auto args
                    = ecosystem::string_list { "cmake",
                                               "-S",
                                               source.string(),
                                               "-B",
                                               binary.string(),
                                               "-DCMAKE_BUILD_TYPE=Debug",
                                               "-DCMAKE_TOOLCHAIN_FILE="
                                                   + toolchain.string(),
                                               "-DANDROID_ABI=" + abi,
                                               "-DANDROID_PLATFORM=android-28",
                                               "-DANDROID_STL=c++_static" };
                args.insert(args.end(), extra.begin(), extra.end());
                facade_command(project, args);
            };
            const auto build_target = [&](const fs::path& binary) {
                facade_command(
                    project,
                    { "cmake", "--build", binary.string(), "--config", "Debug",
                      "--parallel", "2" }
                );
            };
            const auto verify_elf = [&](const fs::path& binary) {
                const auto bytes = read_text(binary);
                require_true(
                    bytes.size() >= 20
                        && bytes.substr(0, 4)
                            == "\x7f"
                               "ELF"
                        && bytes[4] == 2 && bytes[5] == 1,
                    "expected a 64-bit little-endian ELF: " + binary.string()
                );
                const auto machine = static_cast<unsigned char>(bytes[18])
                    + 256 * static_cast<unsigned char>(bytes[19]);
                require_true(
                    machine == (abi == "arm64-v8a" ? 183 : 62),
                    "ELF architecture must match requested ABI: "
                        + binary.string()
                );
            };
            configure(consumer, build, {});
            build_target(build);
            require_true(
                !fs::exists(build / "mvp") && !fs::exists(build / "mvp.exe"),
                "Android builds must not expose a desktop visitor entry"
            );
            facade_command(
                project,
                { "cmake", "--install", build.string(), "--config", "Debug",
                  "--prefix", prefix.string() }
            );
            for (const auto& entry : fs::recursive_directory_iterator(prefix)) {
                if (entry.path().extension() == ".cmake")
                    require_not_contains(
                        read_text(entry.path()), project.string(),
                        "cross-built package metadata must not retain private "
                        "paths"
                    );
            }
            fs::rename(provider, project / "hidden-provider");
            fs::rename(consumer, project / "hidden-consumer");
            fs::rename(build, project / "hidden-build");
            const auto relocated = project / "relocated";
            fs::rename(prefix, relocated);
            verify_elf(relocated / "bin/main");
            const auto plain = project / "plain";
            write_text(
                plain / "CMakeLists.txt",
                R"(cmake_minimum_required(VERSION 3.20)
project(relocated LANGUAGES CXX)
if(NOT CMAKE_CROSSCOMPILING OR NOT ANDROID)
    message(FATAL_ERROR "Android cross compilation required")
endif()
find_package(numbers CONFIG REQUIRED)
add_library(relocated SHARED main.cpp)
target_link_libraries(relocated PRIVATE numbers::math__library)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/binary-$<CONFIG>.txt" CONTENT "$<TARGET_FILE:relocated>")
)"
            );
            write_text(
                plain / "main.cpp",
                "#include <answer.hpp>\nint relocated_answer() { return "
                "answer({}); }\n"
            );
            configure(
                plain, plain / "build",
                { "-Dnumbers_DIR="
                  + (relocated / "lib/cmake/numbers").string() }
            );
            build_target(plain / "build");
            verify_elf(read_text(plain / "build/binary-Debug.txt"));
            std::cout << "[ok] Android " << abi << " " << kind
                      << " visitor/install/relocation (compile and ELF "
                         "inspection only)\n";
        }
    }
}

void test_real_facade_packages(const fs::path& prefix) {
    require_true(
        fs::is_directory(prefix), "installed provider prefix must exist"
    );
    temp_dir root;
    write_text(
        root.path() / "CMakeLists.txt", R"(cmake_minimum_required(VERSION 3.20)
project(provider_contract VERSION 9.8.7 LANGUAGES CXX)
find_package(packing CONFIG REQUIRED PATHS "${PROVIDER_PREFIX}" NO_DEFAULT_PATH)
find_package(monitor CONFIG REQUIRED PATHS "${PROVIDER_PREFIX}" NO_DEFAULT_PATH)
add_executable(provider_contract main.cpp)
target_compile_definitions(provider_contract PRIVATE ECOSYSTEM_PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(provider_contract PRIVATE
    packing::library__core monitor::watchdog_client__client monitor::watchdog_qt__qt)
enable_testing()
add_test(NAME provider_contract COMMAND provider_contract)
if(MSVC)
    target_compile_options(provider_contract PRIVATE /W4 /WX)
else()
    target_compile_options(provider_contract PRIVATE -Wall -Wextra -Werror)
endif()
)"
    );
    write_text(
        root.path() / "main.cpp",
        R"(#include <packing/layout/equal_rectangles.hpp>
#include <monitor/client.hpp>
#include <monitor/qt/gui_heartbeat.hpp>
#include <QCoreApplication>
#include <string_view>
static_assert(std::string_view(ECOSYSTEM_PROJECT_VERSION) == "9.8.7");
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    monitor::client client;
    monitor::qt::gui_heartbeat heartbeat(&app);
    packing::equal_packing_request request;
    request.container = {100.0, 100.0};
    request.item = {10.0, 20.0};
    request.count = 4;
    const auto result = packing::pack_equal_rectangles(request);
    return result.complete(4) && !client.available() ? 0 : 1;
}
)"
    );
    const auto build = root.path() / "build";
    facade_command(
        root.path(),
        { "cmake", "-S", root.path().string(), "-B", build.string(),
          "-DPROVIDER_PREFIX=" + prefix.string(),
          "-DCMAKE_PREFIX_PATH=" + prefix.string() }
    );
    facade_command(
        root.path(),
        { "cmake", "--build", build.string(), "--config", "Debug", "--parallel",
          "2" }
    );
    facade_command(
        root.path(),
        { "ctest", "--test-dir", build.string(), "--build-config", "Debug",
          "--output-on-failure", "--no-tests=error" }
    );
    std::cout << "installed packing/monitor client/Qt contract passed\n";
}

} // namespace ecosystem_test_support
