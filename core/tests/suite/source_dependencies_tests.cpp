#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

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
    for (const auto* forbidden : { "ExternalProject_Add", "IMPORTED_LOCATION",
                                   "_binary_dir", "copy_directory" }) {
        require_not_contains(
            generated_cmake, forbidden,
            "consumers must not synthesize provider build layouts"
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

void test_source_dependency_local_override_builds_and_installs_before_consumer() {
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto provider = root.path() / "provider";
    const auto consumer = root.path() / "consumer";
    scoped_env override_path("NUMBERS_SOURCE_DIR", provider.string());
    auto result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0,
        "provider must install before consumer configuration: " + result.output
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
        scoped_env override_path("PROVIDER_SOURCE_DIR", provider.string());
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
    scoped_env override_path("NUMBERS_SOURCE_DIR", provider.string());
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

void test_source_dependency_shared_and_interface_usage_requirements() {
    for (const auto* kind : { "shared_lib", "interface_lib" }) {
        temp_dir root;
        write_source_dependency_fixture(root.path());
        const auto provider = root.path() / "provider";
        const auto consumer = root.path() / "consumer";
        auto provider_manifest
            = json::parse(read_text(provider / "manifest.json"));
        provider_manifest["artifacts"][0]["kind"] = kind;
        auto consumer_manifest
            = json::parse(read_text(consumer / "manifest.json"));
        consumer_manifest["artifacts"][0]["kind"] = kind;
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
        scoped_env override_path("NUMBERS_SOURCE_DIR", provider.string());
        const auto result = run_marx_cli(consumer, "build debug");
        require_true(
            result.exit_code == 0
                && run_marx_cli(consumer, "run debug").exit_code == 0,
            std::string("installed usage requirements must work for ") + kind
                + ": " + result.output
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
            { "outside project",
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
        scoped_env override_path("NUMBERS_SOURCE_DIR", provider.string());
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
        scoped_env relative("NUMBERS_SOURCE_DIR", "../provider");
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
        "\"$MANIFESTO_TEST_REAL_CMAKE\" \"$@\"\n"
    );
    scoped_env cmake("MANIFESTO_TEST_REAL_CMAKE", real_cmake);
    scoped_env path("PATH", tools.string() + ":" + current_path_env());
    scoped_env override_path("NUMBERS_SOURCE_DIR", provider.string());
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
