#include "test_cases.hpp"
#include "test_support.hpp"
#include "workspace/source_dependencies.hpp"

namespace ecosystem_test_support {
namespace {
    void
    command(const fs::path& root, const ecosystem::string_list& arguments) {
        const auto result = ecosystem::capture_command_result(arguments, root);
        require_true(result.exit_code == 0, result.output);
    }

    void expect_answer(const fs::path& consumer, int answer) {
        write_text(
            consumer / "src/main.cpp",
            "#include <answer.hpp>\nint main() { return answer({}) == "
                + std::to_string(answer) + " ? 0 : 1; }\n"
        );
        const auto result = run_marx_cli(consumer, "build debug");
        require_true(
            result.exit_code == 0,
            "mapped provider must build: " + result.output
        );
        require_true(
            run_marx_cli(consumer, "run debug").exit_code == 0,
            "consumer must observe the selected provider's value"
        );
    }

    void set_answer(const fs::path& provider, int answer) {
        write_text(
            provider / "src/answer.cpp",
            "#include <answer.hpp>\nint answer(std::span<const int>) { return "
                + std::to_string(answer) + "; }\n"
        );
    }
}

void test_source_local_map_selects_mutable_checkouts_and_preserves_facade() {
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto provider = root.path() / "provider";
    const auto local = root.path() / "local checkout";
    const auto consumer = root.path() / "consumer";
    fs::copy(provider, local, fs::copy_options::recursive);
    const auto loaded = ecosystem::load_manifest(provider / "manifest.json");
    require_true(loaded.errors.empty(), join_lines(loaded.errors));
    write_text(
        provider / "CMakeLists.txt",
        ecosystem::generate_cmakelists(*loaded.value, provider)
    );
    command(provider, { "git", "init", "-q", "-b", "main" });
    command(provider, { "git", "add", "." });
    command(
        provider,
        { "git", "-c", "user.name=Fixture", "-c",
          "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false",
          "commit", "-qm", "provider" }
    );
    auto manifest = json::parse(read_text(consumer / "manifest.json"));
    auto& dependency = manifest["artifacts"][0]["packages"]["external_project"];
    dependency["repository"] = provider.string();
    dependency["revision"] = "main";
    write_text(consumer / "manifest.json", manifest.dump(2));
    require_true(
        run_marx_cli(consumer, "sync").exit_code == 0,
        "consumer sync must succeed"
    );
    const auto tracked = read_text(consumer / "CMakeLists.txt");
    const auto map = consumer / "manifesto.local.json";
    write_text(
        map,
        json(
            { { "sources", { { provider.string(), local.string() } } } }
        ).dump(2)
    );
    command(consumer, { "git", "init", "-q" });
    command(consumer, { "git", "check-ignore", "-q", "manifesto.local.json" });
    {
        scoped_env legacy(
            "PROVIDER_SOURCE_DIR",
            (root.path() / "invalid-environment").string()
        );
        set_answer(local, 45);
        expect_answer(consumer, 45);
        set_answer(local, 46);
        expect_answer(consumer, 46);
        require_true(
            run_marx_cli(consumer, "sync").exit_code == 0,
            "sync must ignore local selections"
        );
        require_true(
            read_text(consumer / "CMakeLists.txt") == tracked,
            "sync and local builds must preserve portable tracked CMake "
            "byte-for-byte"
        );
        require_true(
            !fs::exists(local / ".ecosystem")
                && !fs::exists(local / "CMakeLists.txt"),
            "local checkout receives no generated files"
        );
    }
    // A retired override must not silently select remote code after map
    // removal.
    write_text(map, R"({"sources":{}})");
    {
        scoped_env legacy("PROVIDER_SOURCE_DIR", local.string());
        const auto result = run_marx_cli(consumer, "build debug");
        require_true(result.exit_code == 5, "legacy selection must fail");
        require_contains(
            result.output, "PROVIDER_SOURCE_DIR no longer selects",
            "failure must identify the retired environment input"
        );
        require_contains(
            result.output, "manifesto.local.json",
            "failure must explain migration to the root source map"
        );
    }
    scoped_env no_legacy("PROVIDER_SOURCE_DIR", "");
    // Repository keys match exactly, and unused paths need not exist locally.
    write_text(
        map,
        json(
            { { "sources",
                { { provider.string() + ".git",
                    (root.path() / "unused").string() } } } }
        ).dump()
    );
    expect_answer(consumer, 42);
    fs::remove(map);
    expect_answer(consumer, 42);
    require_true(
        read_text(consumer / "CMakeLists.txt") == tracked,
        "remote fallback must preserve tracked CMake"
    );
    // A visitor neither parses the local map nor consults developer overrides.
    write_text(map, "{ malformed local settings\n");
    require_true(
        run_marx_cli(consumer, "sync").exit_code == 0,
        "sync must not interpret the local map"
    );
    scoped_env ignored_legacy("PROVIDER_SOURCE_DIR", local.string());
    const auto visitor = root.path() / "visitor";
    command(
        root.path(),
        { "cmake", "-S", consumer.string(), "-B", visitor.string() }
    );
    command(root.path(), { "cmake", "--build", visitor.string() });
    command(root.path(), { (visitor / "mvp").string() });
}

void test_source_local_map_rejects_invalid_configuration() {
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto consumer = root.path() / "consumer";
    const auto provider = root.path() / "provider";
    const auto loaded = ecosystem::load_manifest(consumer / "manifest.json");
    require_true(loaded.errors.empty(), join_lines(loaded.errors));
    const auto map = consumer / "manifesto.local.json";
    scoped_env legacy("NUMBERS_SOURCE_DIR", provider.string());
    const auto reject = [&](const std::string& expected) {
        ecosystem::string_list options;
        std::string error;
        const auto result = ecosystem::prepare_source_dependencies(
            consumer, *loaded.value, "debug", &options, &error
        );
        require_true(
            result == ecosystem::command_error::task_failed,
            "invalid explicit configuration must fail instead of falling back"
        );
        require_contains(
            error, "manifesto.local.json",
            "failure must identify the configuration file"
        );
        require_contains(
            error, expected, "failure must identify the invalid selection"
        );
        require_true(
            !fs::exists(consumer / ".ecosystem/dependencies"),
            "invalid source selection must fail before provider preparation"
        );
    };
    for (const auto& text :
         { "{", "[]", "{}", R"({"sources":[]})",
           R"({"sources":{},"source":{}})", R"({"sources":{},"sources":{}})",
           R"({"sources":{"repo":"/first","repo":"/second"}})" }) {
        write_text(map, text);
        reject("invalid local source map");
    }
    for (const auto& path : { json(nullptr), json(42), json(""),
                              json("relative/path"), json("/bad\npath") }) {
        write_text(
            map,
            json(
                { { "sources",
                    { { "https://example.invalid/numbers.git", path } } } }
            ).dump()
        );
        reject("absolute path");
    }
    for (const auto& path :
         { root.path() / "missing", provider / "include/answer.hpp" }) {
        write_text(
            map,
            json(
                { { "sources",
                    { { "https://example.invalid/numbers.git",
                        path.string() } } } }
            ).dump()
        );
        reject("sources[https://example.invalid/numbers.git]");
    }
    fs::remove(map);
    fs::create_directory(map);
    reject("regular file");
    fs::remove(map);
    fs::create_symlink(root.path() / "missing-map", map);
    reject("regular file");
    fs::remove(map);
    // Even a valid legacy checkout is rejected before materialization, whether
    // the map is absent, empty or has only a nonmatching repository spelling.
    reject("NUMBERS_SOURCE_DIR no longer selects");
    write_text(map, R"({"sources":{}})");
    reject("NUMBERS_SOURCE_DIR no longer selects");
    write_text(
        map,
        json(
            { { "sources",
                { { "https://example.invalid/numbers", provider.string() } } } }
        ).dump()
    );
    reject("NUMBERS_SOURCE_DIR no longer selects");
}

void test_source_local_map_applies_to_recursive_providers() {
    temp_dir root;
    write_source_dependency_fixture(root.path());
    write_source_dependency_fixture(root.path() / "base");
    const auto consumer = root.path() / "consumer";
    const auto provider = root.path() / "provider";
    const auto base = root.path() / "base/provider";
    auto foundation = json::parse(read_text(base / "manifest.json"));
    foundation["id"] = "foundation";
    foundation["artifacts"][0]["owns"] = json::array({ "base" });
    write_text(base / "manifest.json", foundation.dump(2));
    write_text(base / "include/base.hpp", "#pragma once\nint base_answer();\n");
    write_text(
        base / "src/base.cpp",
        "#include <base.hpp>\nint base_answer() { return 42; }\n"
    );
    auto manifest = json::parse(read_text(provider / "manifest.json"));
    auto imported
        = json::parse(read_text(consumer / "manifest.json"))["artifacts"][0];
    imported["packages"]["external_project"]["repository"]
        = "https://example.invalid/foundation.git";
    imported["packages"]["external_project"]["package"] = "foundation";
    manifest["artifacts"][0]["dependencies"] = json::array({ "imported:math" });
    manifest["artifacts"].push_back(imported);
    write_text(provider / "manifest.json", manifest.dump(2));
    write_text(
        provider / "src/answer.cpp",
        "#include <answer.hpp>\n#include <base.hpp>\nint "
        "answer(std::span<const int>) { return base_answer(); }\n"
    );
    write_text(
        consumer / "manifesto.local.json",
        json(
            { { "sources",
                { { "https://example.invalid/numbers.git", provider.string() },
                  { "https://example.invalid/foundation.git",
                    base.string() } } } }
        ).dump()
    );
    // One consumer-owned map governs the closure; provider/ancestor settings
    // must not silently change the selected topology.
    write_text(provider / "manifesto.local.json", "invalid provider-local map");
    write_text(root.path() / "manifesto.local.json", "invalid ancestor map");
    scoped_env legacy("NUMBERS_SOURCE_DIR", "/invalid-environment");
    scoped_env base_legacy("FOUNDATION_SOURCE_DIR", "/invalid-environment");
    expect_answer(consumer, 42);
    write_text(
        base / "src/base.cpp",
        "#include <base.hpp>\nint base_answer() { return 43; }\n"
    );
    expect_answer(consumer, 43);
    require_true(
        !fs::exists(provider / ".ecosystem")
            && !fs::exists(base / ".ecosystem"),
        "recursive preparation stays in consumer-owned state"
    );
}

} // namespace ecosystem_test_support
