#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

void test_file_writers_report_late_io_failures() {
    for (const bool authored : { false, true }) {
        for (const std::size_t size : { 1024U, 32768U }) {
            temp_dir root;
            const fs::path path = root.path() / "output";
            const pid_t child = ::fork();
            require_true(
                child >= 0, "must fork the isolated I/O failure probe"
            );
            if (child == 0) {
                std::signal(SIGXFSZ, SIG_IGN);
                const rlimit limit { 64, 64 };
                if (::setrlimit(RLIMIT_FSIZE, &limit) != 0) {
                    ::_exit(2);
                }
                std::string error;
                auto value = sample_manifest();
                value.description = std::string(size, 'x');
                const bool saved = authored
                    ? ecosystem::save_manifest(path, value, &error)
                    : ecosystem::write_text_file(
                          path, value.description, &error
                      );
                ::_exit(
                    !saved
                            && error.find("unable to write " + path.string())
                                != std::string::npos
                        ? 0
                        : 1
                );
            }
            int status = 0;
            require_true(
                ::waitpid(child, &status, 0) == child,
                "must wait for the write probe"
            );
            require_true(
                WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "buffered and direct write failures must report failure with "
                "the path"
            );
            require_true(
                fs::file_size(path) == 64,
                "failure injection must exercise a partial write after a "
                "successful open"
            );
        }
    }
}

void test_self_manifest_loads() {
    const ecosystem::manifest_report report = ecosystem::load_manifest(
        fs::path(ECOS_TEST_SOURCE_DIR) / "manifest.json"
    );
    require_true(
        report.errors.empty(),
        "self manifest must load without validation errors"
    );
    require_true(report.value.has_value(), "self manifest must be parsed");
    require_true(
        report.value->facade_entry_artifact == "marx:marx",
        "self manifest facade entry must point to Marx"
    );
    std::set<std::string> binaries;
    for (const auto& owner : report.value->components) {
        for (const auto& item : owner.artifacts) {
            if (item.kind == "exe" || item.kind == "qt_app") {
                binaries.insert(
                    ecosystem::artifact_output_name(*report.value, owner, item)
                );
            }
        }
    }
    require_true(
        binaries == std::set<std::string> { "marx", "engels" },
        "public local tooling binaries must remain exactly Marx and Engels"
    );
    require_true(
        report.value->install_artifacts
            == ecosystem::string_list { "marx:marx", "engels:engels" },
        "both actors must be distributed together"
    );
    const auto cmake = ecosystem::generate_cmakelists(
        *report.value, fs::path(ECOS_TEST_SOURCE_DIR)
    );
    require_not_contains(
        cmake, "add_executable(manifesto",
        "no third runtime target may be generated"
    );
    require_contains(
        cmake, "install(TARGETS marx__marx", "Marx must be installed"
    );
    require_contains(
        cmake, "install(TARGETS engels__engels", "Engels must be installed"
    );
}

void test_qml_modules_preserve_optional_ownership_and_generation() {
    temp_dir root;
    temp_dir outside;
    const json authored {
        { "id", "qml_sample" },
        { "description", "Optional QML acceptance" },
        { "facade", "app:main" },
        { "artifacts",
          json::array(
              { { { "id", "app:main" },
                  { "kind", "qt_app" },
                  { "root", "app" },
                  { "owns", json::array() },
                  { "entry", "src/main.cpp" },
                  { "packages",
                    { { "qt", json::array({ "Core", "Qml", "Quick" }) } } },
                  { "qml",
                    { { "uri", "Sample.Ui" },
                      { "version", "1.0" },
                      { "files",
                        json::array(
                            { "qml/Main.qml", "qml/nested/Tile.qml" }
                        ) } } } } }
          ) }
    };
    write_text(root.path() / "app/src/main.cpp", "int main() { return 0; }\n");
    write_text(
        root.path() / "app/src/second.cpp", "int main() { return 0; }\n"
    );
    const auto main_qml = root.path() / "app/qml/Main.qml";
    write_text(main_qml, "import QtQuick\nItem {}\n");
    write_text(
        root.path() / "app/qml/nested/Tile.qml", "import QtQuick\nItem {}\n"
    );
    const auto load = [&](const json& document) {
        write_text(root.path() / "manifest.json", document.dump(2));
        return ecosystem::load_manifest(root.path() / "manifest.json");
    };
    const auto loaded = load(authored);
    require_true(
        loaded.value.has_value() && loaded.errors.empty(),
        join_lines(loaded.errors)
    );
    require_true(
        ecosystem::to_json(*loaded.value) == authored,
        "QML intent must round-trip"
    );
    const auto cmake
        = ecosystem::generate_developer_cmakelists(*loaded.value, root.path());
    require_contains(
        cmake, "qt_add_qml_module(app__main",
        "Qt must own QML module generation"
    );
    require_contains(
        cmake, "URI Sample.Ui", "authored module URI must be preserved"
    );
    require_contains(
        cmake, "QT_RESOURCE_ALIAS nested/Tile.qml",
        "nested resources must retain module-relative aliases"
    );
    require_contains(
        cmake, "/qml/app__main/Sample/Ui",
        "QML output must have an artifact-specific URI directory"
    );
    require_contains(
        cmake, "VERSION_GREATER_EQUAL 6.8",
        "new QML directory policy must be version guarded"
    );

    const auto reject
        = [&](const json& document, const std::string& diagnostic) {
              const auto invalid = load(document);
              require_contains(
                  join_lines(invalid.errors), diagnostic,
                  "invalid QML must fail before generation"
              );
          };
    for (const auto& [field, value, diagnostic] :
         std::vector<std::tuple<std::string, json, std::string>> {
             { "uri", "bad-uri", "artifact.qml.uri" },
             { "version", "1", "artifact.qml.version" },
             { "files", json::array(), "artifact.qml.files must not be empty" },
             { "files", json::array({ "qml/../outside.qml" }),
               "safe qml/*.qml" },
             { "files", json::array({ "qml/Main.qml", "qml/Main.qml" }),
               "duplicate" },
             { "files", json::array({ "qml/Missing.qml" }),
               "QML file must exist" } }) {
        auto invalid = authored;
        invalid["artifacts"][0]["qml"][field] = value;
        reject(invalid, diagnostic);
    }
    auto invalid_kind = authored;
    invalid_kind["artifacts"][0]["kind"] = "exe";
    reject(invalid_kind, "requires a qt_app");
    auto missing_package = authored;
    missing_package["artifacts"][0]["packages"]["qt"]
        = json::array({ "Core", "Widgets" });
    reject(missing_package, "requires packages.qt to include Qml");
    auto wrong_root = authored;
    wrong_root["artifacts"][0]["root"] = "frontend";
    reject(wrong_root, "must be rooted at app/");

    write_text(outside.path() / "Outside.qml", "import QtQuick\nItem {}\n");
    fs::remove(main_qml);
    fs::create_symlink(outside.path() / "Outside.qml", main_qml);
    reject(authored, "outside project");
    require_true(
        !ecosystem::validate_manifest_paths(*loaded.value, root.path()).empty(),
        "direct generation preflight must also reject escaped QML paths"
    );
    fs::remove(main_qml);
    fs::create_directory(main_qml);
    reject(authored, "QML file must exist and be a regular file");
    fs::remove(main_qml);
    write_text(main_qml, "import QtQuick\nItem {}\n");

    auto shared = authored;
    auto second = authored["artifacts"][0];
    second["id"] = "app:second";
    second["entry"] = "src/second.cpp";
    second["qml"]["uri"] = "Sample.Second";
    shared["artifacts"].push_back(second);
    reject(shared, "overlapping artifact ownership");
    fs::create_symlink("Main.qml", root.path() / "app/qml/Alias.qml");
    shared["artifacts"][1]["qml"]["files"] = json::array({ "qml/Alias.qml" });
    reject(shared, "overlapping artifact ownership through filesystem paths");

    shared["artifacts"][1]["qml"]["files"] = json::array({ "qml/Second.qml" });
    write_text(root.path() / "app/qml/Second.qml", "import QtQuick\nItem {}\n");
    const auto separate = load(shared);
    require_true(
        separate.value.has_value() && separate.errors.empty(),
        "separate QML modules must coexist"
    );
    const auto separate_cmake = ecosystem::generate_developer_cmakelists(
        *separate.value, root.path()
    );
    require_contains(
        separate_cmake, "/qml/app__main/Sample/Ui",
        "first module must retain its own output directory"
    );
    require_contains(
        separate_cmake, "/qml/app__second/Sample/Second",
        "second module must not overwrite first module metadata"
    );

    auto widgets = authored;
    widgets["artifacts"][0].erase("qml");
    widgets["artifacts"][0]["packages"]["qt"]
        = json::array({ "Core", "Widgets" });
    const auto widgets_loaded = load(widgets);
    require_true(
        widgets_loaded.value.has_value() && widgets_loaded.errors.empty(),
        "Widgets must remain independent of QML"
    );
    const auto widgets_cmake = ecosystem::generate_developer_cmakelists(
        *widgets_loaded.value, root.path()
    );
    for (const auto* text :
         { "qt_add_qml_module", "QT_RESOURCE_ALIAS", "QTP0004", "Qt6::Qml" })
        require_not_contains(
            widgets_cmake, text,
            "Widgets-only generation must omit QML mechanics"
        );
}

void test_authored_artifacts_round_trip_and_mutate() {
    temp_dir root;
    json authored
        = { { "id", "owned" },
            { "description", "Explicit artifact intent" },
            { "version", "1.2.3" },
            { "facade", "core:app" },
            { "artifacts",
              json::array(
                  { { { "id", "core:lib" },
                      { "kind", "static_lib" },
                      { "owns", json::array({ "math" }) } },
                    { { "id", "core:app" },
                      { "kind", "exe" },
                      { "owns", json::array() },
                      { "entry", "src/launch.cpp" },
                      { "dependencies", json::array({ "core:lib" }) } } }
              ) } };
    write_text(root.path() / "manifest.json", authored.dump(2) + "\n");
    write_text(root.path() / "include/math.hpp", "int answer();\n");
    write_text(root.path() / "src/math.cpp", "int answer() { return 42; }\n");
    write_text(
        root.path() / "src/launch.cpp",
        "#include \"math.hpp\"\nint main() { return answer() == 42 ? 0 : 1; }\n"
    );
    auto loaded = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        loaded.errors.empty() && loaded.value.has_value(),
        join_lines(loaded.errors)
    );
    require_true(
        loaded.value->cpp_standard == 20,
        "default language standard must not need repeated authorship"
    );
    require_true(
        ecosystem::to_json(*loaded.value) == authored,
        "only authored scope intent must round-trip"
    );
    write_text(
        root.path() / "include/math/extra.hpp",
        "// inferred after the next load\n"
    );
    loaded = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        loaded.errors.empty()
            && loaded.value->components.front().ownership->headers.size() == 2,
        "new companions must be discovered without manifest edits"
    );
    const auto mutated
        = run_marx_cli(root.path(), "mutate add module core:lib added");
    require_true(
        mutated.exit_code == 0,
        "qualified artifact ownership must scaffold: " + mutated.output
    );
    const auto saved = json::parse(read_text(root.path() / "manifest.json"));
    require_true(
        !saved.contains("components")
            && saved.at("artifacts").at(0).at("owns")
                == json::array({ "math", "added" }),
        "mutation must persist scopes without a module/file inventory"
    );
    require_true(
        saved.at("artifacts").at(1) == authored.at("artifacts").at(1),
        "mutation must preserve other artifacts and declared dependencies"
    );
    require_true(
        fs::exists(root.path() / "include/added.hpp")
            && fs::exists(root.path() / "src/added.cpp"),
        "new module companions must be scaffolded"
    );
    auto updated = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(updated.errors.empty(), join_lines(updated.errors));
    const auto templated = ecosystem::add_file_unit(
        root.path(), &*updated.value, "core:lib", "include/template",
        "header_template_impl"
    );
    require_true(templated.errors.empty(), join_lines(templated.errors));
    std::string save_error;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", *updated.value, &save_error
        ),
        save_error
    );
    write_text(
        root.path() / "src/launch.cpp",
        "#include \"math.hpp\"\n#include \"template.hpp\"\nint main() { return "
        "answer() == 42 ? 0 : 1; }\n"
    );
    write_text(root.path() / "include/unowned.hpp", "#error unowned header\n");
    const auto built = run_marx_cli(root.path(), "build debug");
    require_true(
        built.exit_code == 0, "authored core+app must build: " + built.output
    );
    const auto run = run_marx_cli(root.path(), "run debug");
    require_true(
        run.exit_code == 0,
        "authored entry and library dependency must run: " + run.output
    );
    auto core_only = ecosystem::to_json(*updated.value);
    core_only["facade"] = "core:lib";
    core_only["artifacts"].erase(core_only["artifacts"].begin() + 1);
    write_text(root.path() / "manifest.json", core_only.dump(2) + "\n");
    require_sync_success(root.path(), "core-only authored facade must sync");
    const fs::path visitor = root.path() / "visitor";
    const fs::path prefix = root.path() / "installed";
    require_true(
        ecosystem::run_command(
            { "cmake", "-S", root.path().string(), "-B", visitor.string() },
            root.path()
        ) == 0,
        "core-only visitor facade must configure"
    );
    require_true(
        ecosystem::run_command(
            { "cmake", "--build", visitor.string(), "--parallel", "2" },
            root.path()
        ) == 0,
        "core-only visitor facade must build"
    );
    require_true(
        ecosystem::run_command(
            { "cmake", "--install", visitor.string(), "--prefix",
              prefix.string() },
            root.path()
        ) == 0,
        "core-only facade must install owned headers"
    );
    require_true(
        fs::exists(prefix / "include/math.hpp")
            && fs::exists(prefix / "include/math/extra.hpp")
            && fs::exists(prefix / "include/template.tpp")
            && !fs::exists(prefix / "include/unowned.hpp"),
        "installation must include selected nested/template companions and "
        "exclude unowned headers"
    );
    auto app_only = authored;
    app_only["facade"] = "solo:app";
    app_only["artifacts"] = json::array(
        { { { "id", "solo:app" },
            { "kind", "exe" },
            { "owns", json::array() },
            { "entry", "src/standalone.cpp" } } }
    );
    write_text(root.path() / "manifest.json", app_only.dump(2) + "\n");
    write_text(
        root.path() / "src/standalone.cpp",
        "#include <cstdio>\nint main() { std::puts(\"standalone-owned\"); }\n"
    );
    require_true(
        run_marx_cli(root.path(), "build debug").exit_code == 0,
        "app-only owned entry must build without other artifacts"
    );
    const auto standalone = run_marx_cli(root.path(), "run debug");
    require_true(standalone.exit_code == 0, standalone.output);
    require_contains(
        standalone.output, "standalone-owned",
        "app-only execution must use the declared entry"
    );
}

void test_authored_artifacts_reject_obsolete_and_ambiguous_intent() {
    temp_dir root;
    const json base = { { "id", "owned" },
                        { "description", "Owned app" },
                        { "facade", "app:app" },
                        { "artifacts",
                          json::array(
                              { { { "id", "app:app" },
                                  { "kind", "exe" },
                                  { "owns", json::array() },
                                  { "entry", "src/start.cc" } } }
                          ) } };
    auto check = [&](const json& value, const std::string& diagnostic) {
        write_text(root.path() / "manifest.json", value.dump());
        const auto loaded
            = ecosystem::load_manifest(root.path() / "manifest.json");
        require_true(
            loaded.has_manifest, "invalid authored projects must remain visible"
        );
        require_contains(
            join_lines(loaded.errors), diagnostic,
            "invalid authored intent must fail before generation"
        );
        require_true(
            !fs::exists(root.path() / "CMakeLists.txt"),
            "loading invalid intent must not generate state"
        );
    };
    for (const std::string field :
         { "components", "modules", "file_units", "relations",
           "android_sdk_path", "release_state" }) {
        auto value = base;
        value[field] = json::array();
        check(value, "unsupported field '" + field + "'");
    }
    for (const std::string field :
         { "modules", "file_units", "stack", "link" }) {
        auto value = base;
        value["artifacts"][0][field] = json::array();
        check(value, "unsupported field '" + field + "'");
    }
    auto value = base;
    value["artifacts"][0].erase("entry");
    check(value, "requires an explicit entry");
    value = base;
    value["artifacts"][0]["owns"] = ".";
    check(value, "owns must be an array");
    value = base;
    value["artifacts"][0]["entry"] = "src/start.hpp";
    check(value, "must name a C++ source file");
    value = base;
    value["artifacts"][0]["dependencies"] = json::array({ "missing:lib" });
    check(value, "unresolved artifact link");
    value = base;
    value["artifacts"].push_back(
        { { "id", "lib:lib" },
          { "kind", "static_lib" },
          { "owns", json::array({ "." }) } }
    );
    check(value, "overlapping artifact ownership");
    value = base;
    value["artifacts"][0]["owns"] = json::array({ "../escape" });
    check(value, "safe project-relative scopes");
    value = base;
    value["artifacts"][0]["entry"] = "../start.cpp";
    check(value, "safe root-relative file");
    value = base;
    value["artifacts"][0]["root"] = "../outside";
    check(value, "safe project-relative path");
    fs::create_directories(root.path() / "src/empty");
    fs::create_directory_symlink("empty", root.path() / "src/alias");
    value = base;
    value["artifacts"].push_back(
        { { "id", "first:lib" },
          { "kind", "static_lib" },
          { "owns", json::array({ "src/empty" }) } }
    );
    value["artifacts"].push_back(
        { { "id", "second:lib" },
          { "kind", "static_lib" },
          { "owns", json::array({ "src/alias" }) } }
    );
    check(value, "overlapping artifact ownership through filesystem paths");
    value = base;
    auto& imported = value["artifacts"][0];
    imported["kind"] = "static_lib";
    imported["name"] = "remote";
    imported.erase("entry");
    imported["packages"]
        = { { "external_project",
              { { "repository", "https://example.invalid/provider.git" },
                { "revision", "main" },
                { "package", "provider" },
                { "artifact", "core:lib" } } } };
    imported["owns"] = json::array({ "math" });
    check(value, "external artifacts must not declare local owns");
    imported["owns"] = json::array();
    imported["id"] = "app:external";
    value["facade"] = "app:external";
    write_text(root.path() / "manifest.json", value.dump());
    const auto external
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(external.errors.empty(), join_lines(external.errors));
    const auto cmake
        = ecosystem::generate_cmakelists(*external.value, root.path());
    require_contains(
        cmake, "find_package(provider CONFIG REQUIRED)",
        "providers must enter through their installed package"
    );
    require_contains(
        cmake, "add_library(app__external ALIAS provider::core__lib)",
        "the authored imported identity must remain stable"
    );
}

void test_owned_test_targets_are_independent() {
    temp_dir root;
    write_text(root.path() / "manifest.json", R"({
        "id":"parallel_tests", "description":"Independent artifact test owners",
        "facade":"core:alpha",
        "artifacts":[
            {"id":"core:alpha","kind":"static_lib","owns":["alpha"],"tests":{"selftest":true}},
            {"id":"core:beta","kind":"static_lib","owns":["beta"],"tests":{"selftest":true}}
        ]
    })");
    for (const auto* name : { "alpha", "beta" }) {
        write_text(
            root.path() / (std::string("src/") + name + ".cpp"),
            std::string("int ") + name + "() { return 42; }\n"
        );
        write_text(
            root.path() / (std::string("tests/") + name + "_tests.cpp"),
            std::string("int ") + name + "(); int main() { return " + name
                + "() == 42 ? 0 : 1; }\n"
        );
    }
    auto loaded = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(loaded.errors.empty(), join_lines(loaded.errors));
    auto collision = *loaded.value;
    collision.components.back().artifacts.front().name = "core__alpha__tests";
    collision.components.back().artifacts.front().kind = "exe";
    collision.components.back().ownership->entry = "src/entry.cpp";
    require_contains(
        join_lines(ecosystem::validate_manifest(collision)),
        "artifact output collision:",
        "new automatic test filenames must remain reserved"
    );

    const auto all = run_cli(root.path(), "check tests");
    require_true(
        all.exit_code == 0,
        "both test owners must build and run:\n" + all.output
    );
    for (const auto* name : { "core__alpha__tests", "core__beta__tests" })
        require_contains(
            all.output, name,
            "each artifact must have an independent test identity"
        );
    const auto build
        = root.path() / ecosystem::local_build_dir(root.path(), "debug");
    const auto commands
        = json::parse(read_text(build / "compile_commands.json"));
    bool found_defaults = false;
    for (const auto& command : commands) {
        const auto text = command.at("command").get<std::string>();
        if (text.find("core__alpha__tests.dir") != std::string::npos) {
            require_contains(
                text, "-Wall", "generated tests must receive common warnings"
            );
            require_contains(
                text, "-std=c++20",
                "generated tests must receive the project standard"
            );
            found_defaults = true;
        }
    }
    require_true(found_defaults, "test compile commands must be inspectable");
    write_text(
        root.path() / "tests/beta_tests.cpp", "int main() { return 1; }\n"
    );
    const auto alpha = run_cli(root.path(), "check tests core:alpha");
    require_true(
        alpha.exit_code == 0,
        "selection must exclude the failing adjacent test owner:\n"
            + alpha.output
    );
    require_not_contains(
        alpha.output, "core__beta__tests",
        "selection must use the full artifact identity"
    );
    const auto beta = run_cli(root.path(), "check tests core:beta");
    require_true(beta.exit_code != 0, "selecting the failing owner must fail");
    require_contains(
        beta.output, "core__beta__tests",
        "failure must identify the selected owner"
    );

    if (ecosystem::probe_tool("llvm-profdata").available
        && ecosystem::probe_tool("llvm-cov").available) {
        const auto coverage = run_cli(root.path(), "check coverage core:alpha");
        require_true(
            coverage.exit_code == 0,
            "coverage must use the same selected test identity:\n"
                + coverage.output
        );
        const auto report = json::parse(
            read_text(root.path() / ".ecosystem/reports/coverage.json")
        );
        require_true(
            !report.at("data").empty(),
            "LLVM coverage must contain measured test data"
        );
    }
}

void test_owned_scope_discovery_and_build() {
    require_true(fs::equivalent(ecosystem::locate_template_path({"cmake/generated_tests_block.tpl"}),
        fs::path(ECOS_TEST_SOURCE_DIR) / "templates/cmake/generated_tests_block.tpl"),
        "default templates must come from the generator's own version");
    temp_dir root;
    write_text(root.path() / "include/math.hpp", "int answer();\n");
    write_text(root.path() / "include/math.tpp", "// companion\n");
    write_text(
        root.path() / "src/math.cpp",
        "#include \"math.hpp\"\nint answer() { return 42; }\n"
    );
    write_text(
        root.path() / "src/launch.cc",
        "#include \"math.hpp\"\nint main() { return answer() == 42 ? 0 : 1; }\n"
    );
    write_text(
        root.path() / "tests/math_tests.cpp",
        "#include \"math.hpp\"\nint main() { return answer() == 42 ? 0 : 1; }\n"
    );
    write_text(
        root.path() / "tests/unrelated.cpp", "#error outside the owned scope\n"
    );
    write_text(
        root.path() / "src/unrelated.cpp", "#error outside the owned scope\n"
    );
    write_text(root.path() / "include/unrelated.hpp", "#error not installed\n");
    auto value = sample_library_manifest();
    auto& library = value.components.front();
    library.modules.clear();
    library.ownership.emplace();
    library.ownership->scopes = { "math" };
    library.tests = json::object({ { "selftest", true } });
    auto app = library;
    app.ownership.emplace();
    app.ownership->entry = "src/launch.cc";
    app.tests.clear();
    app.artifacts = { { "app", "exe", "owned_app", { "core:lib" } } };
    value.components.push_back(app);
    value.facade_entry_artifact = "core:app";
    require_true(
        ecosystem::discover_owned_files(&value, root.path()).empty(),
        "explicit ownership must discover its companions"
    );
    require_true(
        value.components.front().ownership->sources.size() == 1
            && value.components.front().ownership->headers.size() == 2,
        "source/header/template companions must be inferred"
    );
    require_true(
        value.components.front().ownership->tests.size() == 1,
        "test discovery must stay within owned scope"
    );
    require_true(
        ecosystem::resolve_artifact(
            value, ecosystem::artifact_ref { "core", "app" }
        )
            .has_value(),
        "multiple artifacts in a namespace must resolve independently"
    );
    const std::string developer
        = ecosystem::generate_developer_cmakelists(value, root.path());
    require_not_contains(
        developer, "unrelated", "unowned sources and tests must not enter CMake"
    );
    write_text(root.path() / ".ecosystem/source/CMakeLists.txt", developer);
    require_true(
        ecosystem::run_command(
            { "cmake", "-S", (root.path() / ".ecosystem/source").string(), "-B",
              (root.path() / "build").string(), "-DECOSYSTEM_BUILD_TESTS=ON" },
            root.path()
        ) == 0,
        "owned developer surface must configure"
    );
    require_true(
        ecosystem::run_command(
            { "cmake", "--build", (root.path() / "build").string(),
              "--parallel", "2" },
            root.path()
        ) == 0,
        "entry names must not rely on a main filename heuristic"
    );
    require_true(
        ecosystem::run_command(
            { (root.path() / "build/owned_app").string() }, root.path()
        ) == 0,
        "declared library dependency must link and run"
    );
    require_true(
        ecosystem::run_command(
            { "ctest", "--test-dir", (root.path() / "build").string(),
              "--output-on-failure" },
            root.path()
        ) == 0,
        "owned test companions must run"
    );
    value.facade_entry_artifact = "core:lib";
    const auto synced = ecosystem::sync_project(root.path(), value);
    require_true(synced.errors.empty(), join_lines(synced.errors));
    const std::string facade = read_text(root.path() / "CMakeLists.txt");
    require_not_contains(
        facade, "launch.cc",
        "library facade must not acquire an adjacent executable"
    );
    require_not_contains(
        facade, "install(DIRECTORY",
        "owned headers must be installed by selection"
    );
    auto overlapping = value;
    overlapping.components.back().ownership->scopes = { "math" };
    require_contains(
        join_lines(ecosystem::discover_owned_files(&overlapping, root.path())),
        "overlapping artifact ownership",
        "two artifacts must not own the same files"
    );
    auto independent = value;
    independent.facade_entry_artifact = "core:app";
    independent.components.back().artifacts.front().link.clear();
    require_not_contains(
        ecosystem::generate_cmakelists(independent, root.path()),
        "add_library(core__lib",
        "adjacent libraries must not become implicit dependencies"
    );
}

void test_manifest_rejects_unsafe_paths_and_invalid_dependency_graphs() {
    for (const std::string path : { "../outside", "/tmp/outside", "C:/outside",
                                    "bad;cmake", "bad\npath" }) {
        auto value = sample_manifest();
        value.components.front().root = path;
        require_contains(
            join_lines(ecosystem::validate_manifest(value)), "component.root",
            "owned roots must remain inside the project"
        );
        value = sample_manifest();
        value.components.front().modules = { path };
        require_contains(
            join_lines(ecosystem::validate_manifest(value)), "component module",
            "module paths must be safe"
        );
        value = sample_manifest();
        value.components.front().file_units = { { path, "source_only" } };
        require_contains(
            join_lines(ecosystem::validate_manifest(value)), "file_unit.id",
            "exceptional paths must be safe"
        );
        value = sample_manifest();
        value.components.front().artifacts.front().name = path;
        require_contains(
            join_lines(ecosystem::validate_manifest(value)), "output name",
            "output names must not inject paths or CMake lists"
        );
    }
    auto value = sample_manifest();
    const std::string primary = value.facade_entry_artifact;
    value.components.front().artifacts.front().link = { "ghost:lib" };
    require_contains(
        join_lines(ecosystem::validate_manifest(value)),
        "unresolved artifact link", "links must resolve before generation"
    );
    temp_dir root;
    std::string error;
    require_true(
        ecosystem::save_manifest(root.path() / "manifest.json", value, &error),
        error
    );
    const auto rejected = run_marx_cli(root.path(), "sync");
    require_true(
        rejected.exit_code != 0 && !fs::exists(root.path() / "CMakeLists.txt"),
        "an invalid dependency graph must not write generated files"
    );
    auto other = value.components.front();
    other.id = "other";
    other.artifacts.front().id = "lib";
    other.artifacts.front().link.clear();
    value.components.push_back(other);
    value.components.front().artifacts.front().link
        = { "other:lib", "other:lib" };
    require_contains(
        join_lines(ecosystem::validate_manifest(value)),
        "duplicate artifact link", "duplicate links must fail"
    );
    value.components.front().artifacts.front().link = { "other:lib" };
    value.components.back().artifacts.front().link = { primary };
    require_contains(
        join_lines(ecosystem::validate_manifest(value)),
        "artifact dependency cycle:",
        "cycles must fail with an attributable chain"
    );
    value.components.back().artifacts.front().link.clear();
    value.components.back().artifacts.front().kind = "exe";
    require_contains(
        join_lines(ecosystem::validate_manifest(value)),
        "must reference a library",
        "independent executables must not be linked as libraries"
    );
}

void test_owned_paths_reject_symlink_escapes() {
    for (const std::string relative :
         { "src", "src/main.cpp", "include", "tests/external.cpp",
           "tests/nested", "benchmarks/external.cpp", "assets/data",
           "android/res", ".ecosystem" }) {
        temp_dir root;
        const fs::path project = root.path() / "project";
        const fs::path outside = root.path() / "project-other";
        fs::create_directories(outside);
        auto value = sample_manifest();
        value.install_assets = true;
        value.android_package_source_dir = "android";
        std::string error;
        require_true(
            ecosystem::save_manifest(project / "manifest.json", value, &error),
            error
        );
        write_text(outside / "sentinel", "untouched");
        fs::create_directories((project / relative).parent_path());
        fs::create_symlink(outside, project / relative);
        const auto loaded = ecosystem::load_manifest(project / "manifest.json");
        require_true(loaded.has_manifest, "unsafe manifests must stay visible");
        require_contains(
            join_lines(loaded.errors), "outside project", relative
        );
        const auto synced = ecosystem::sync_project(project, value);
        require_true(
            !synced.errors.empty() && synced.written_files.empty(),
            "sync must preflight owned paths"
        );
        require_true(
            !fs::exists(project / "CMakeLists.txt"),
            "failed preflight must not generate files"
        );
        require_true(
            read_text(outside / "sentinel") == "untouched",
            "outside inputs must remain unchanged"
        );
    }

    temp_dir root;
    const fs::path project = root.path() / "project";
    fs::create_directories(project / "src");
    const fs::path missing = root.path() / "missing.cpp";
    fs::create_symlink(missing, project / "src/main.cpp");
    require_contains(
        join_lines(
            ecosystem::validate_manifest_paths(sample_manifest(), project)
        ),
        "unable to resolve owned symlink",
        "dangling links must not be accepted as future destinations"
    );
    require_true(
        !fs::exists(missing),
        "validation must not create a dangling link target"
    );
}

void test_owned_paths_allow_internal_symlinks_and_missing_files() {
    temp_dir root;
    const fs::path project = root.path() / "project";
    fs::create_directories(project / "actual/src");
    fs::create_directories(project / "tests/nested");
    fs::create_directory_symlink("actual/src", project / "src");
    fs::create_directory_symlink("..", project / "tests/nested/loop");
    fs::create_directory_symlink(project, root.path() / "alias");
    auto value = sample_manifest();
    require_true(
        ecosystem::validate_manifest_paths(value, root.path() / "alias")
            .empty(),
        "an aliased project root, internal directory links and cycles must be "
        "handled"
    );
    const auto added = ecosystem::add_module(
        root.path() / "alias", &value, "sample", "nested/feature"
    );
    require_true(
        added.errors.empty() && added.changed_manifest, join_lines(added.errors)
    );
    require_true(
        fs::exists(project / "actual/src/nested/feature.cpp"),
        "safe future files must be scaffolded"
    );
    require_true(
        ecosystem::validate_manifest_paths(
            sample_manifest(), root.path() / "future"
        )
            .empty(),
        "a project that has not been scaffolded yet must be valid"
    );
}

void test_owned_paths_preflight_mutations_without_changes() {
    for (const std::string operation : { "component", "module", "unit" }) {
        temp_dir root;
        auto value = sample_manifest();
        const json before = ecosystem::to_json(value);
        const fs::path project = root.path() / "project";
        ecosystem::mutation_report report;
        if (operation == "component") {
            report = ecosystem::add_component(project, &value, "../escape");
        } else if (operation == "module") {
            report = ecosystem::add_module(
                project, &value, "sample", "../../../escape"
            );
        } else {
            report = ecosystem::add_file_unit(
                project, &value, "sample", "../../../escape", "source_pair_h"
            );
        }
        require_true(
            !report.errors.empty() && !report.changed_manifest
                && report.written_files.empty(),
            operation
        );
        require_true(
            ecosystem::to_json(value) == before && !fs::exists(project),
            "invalid mutation must leave model and filesystem unchanged"
        );
    }

    for (const std::string operation :
         { "component", "module", "files", "unit" }) {
        temp_dir root;
        const fs::path project = root.path() / "project";
        const fs::path outside = root.path() / "outside.cpp";
        const std::string relative = operation == "component" ? "extra/src"
            : operation == "unit" ? "include/feature.tpp"
                                  : "src/feature.cpp";
        fs::create_directories((project / relative).parent_path());
        fs::create_symlink(outside, project / relative);
        auto value = sample_manifest();
        if (operation == "files") {
            value.components.front().modules.push_back("feature");
        }
        const json before = ecosystem::to_json(value);
        std::string error;
        require_true(
            ecosystem::save_manifest(project / "manifest.json", value, &error),
            error
        );
        const std::string manifest_before
            = read_text(project / "manifest.json");
        ecosystem::mutation_report report;
        if (operation == "component") {
            report = ecosystem::add_component(project, &value, "extra");
        } else if (operation == "module") {
            report
                = ecosystem::add_module(project, &value, "sample", "feature");
        } else if (operation == "files") {
            report = ecosystem::add_files(project, value, "sample", "feature");
        } else {
            report = ecosystem::add_file_unit(
                project, &value, "sample", "include/feature",
                "header_template_impl"
            );
        }
        require_contains(
            join_lines(report.errors), "unable to resolve owned symlink",
            operation
        );
        require_true(
            !report.changed_manifest && report.written_files.empty(),
            "rejected mutation must report no changes"
        );
        require_true(
            ecosystem::to_json(value) == before,
            "rejected mutation must preserve the caller's model"
        );
        require_true(
            read_text(project / "manifest.json") == manifest_before,
            "rejected mutation must preserve authored bytes"
        );
        require_true(
            !fs::exists(outside) && !fs::exists(project / "include/feature.hpp")
                && !fs::exists(project / "extra/include"),
            "all scaffold destinations must be checked before writes"
        );
    }
}

void test_owned_paths_preflight_sync_and_manifest_destinations() {
    for (const std::string relative :
         { ".github/workflows/tests.yml", ".github/actions/setup-ecosystem",
           ".github/actions/setup-ecosystem/action.yml", "CMakeLists.txt" }) {
        temp_dir root;
        const fs::path project = root.path() / "project";
        const fs::path outside = root.path() / "outside";
        fs::create_directories(project);
        write_text(outside / "action.yml", "untouched");
        if (relative != "CMakeLists.txt") {
            write_text(project / "CMakeLists.txt", "existing surface");
        }
        fs::create_directories((project / relative).parent_path());
        fs::create_symlink(
            relative.ends_with("setup-ecosystem") ? outside
                                                  : outside / "action.yml",
            project / relative
        );
        const auto report = ecosystem::sync_project(project, sample_manifest());
        require_contains(
            join_lines(report.errors), "outside project", relative
        );
        require_true(
            report.written_files.empty() && report.removed_files.empty(),
            "sync must preflight generation and obsolete paths together"
        );
        require_true(
            read_text(outside / "action.yml") == "untouched",
            "sync must preserve outside files"
        );
        require_true(
            !fs::exists(project / ".gitignore"),
            "sync must reject all destinations before writing the first file"
        );
        if (relative != "CMakeLists.txt") {
            require_true(
                read_text(project / "CMakeLists.txt") == "existing surface",
                "a later unsafe path must not partially update existing "
                "surfaces"
            );
        }
    }

    temp_dir root;
    const fs::path project = root.path() / "project";
    fs::create_directories(project);
    write_text(root.path() / "outside.json", "untouched");
    fs::create_symlink(root.path() / "outside.json", project / "manifest.json");
    std::string error;
    require_true(
        !ecosystem::save_manifest(
            project / "manifest.json", sample_manifest(), &error
        ),
        "manifest save must reject an outside destination"
    );
    require_contains(
        error, "outside project",
        "manifest save must identify the escaped destination"
    );
    require_true(
        read_text(root.path() / "outside.json") == "untouched",
        "manifest save must not truncate outside data"
    );
    require_contains(
        join_lines(ecosystem::load_manifest(project / "manifest.json").errors),
        "outside project",
        "manifest load must reject escaped authored input before parsing it"
    );
}

void test_manifest_rejects_output_and_target_collisions() {
    auto value = sample_dual_run_manifest();
    for (auto& owner : value.components) {
        owner.artifacts.front().name = "same";
    }
    require_contains(
        join_lines(ecosystem::validate_manifest(value)),
        "artifact output collision:",
        "different IDs must not overwrite the same output"
    );
    value.components.back().artifacts.front().kind = "shared_lib";
    require_true(
        ecosystem::validate_manifest(value).empty(),
        "executables and libraries may share a stem when their files differ"
    );
    value.components.front().artifacts.front().name = "libsame.so";
    require_contains(
        join_lines(ecosystem::validate_manifest(value)), "libsame.so",
        "collision checks must use actual output filenames across kinds"
    );
    value = sample_dual_run_manifest();
    value.components.front().id = "a__b";
    value.components.front().artifacts.front().id = "c";
    value.components.back().id = "a";
    value.components.back().artifacts.front().id = "b__c";
    value.facade_entry_artifact = "a__b:c";
    require_contains(
        join_lines(ecosystem::validate_manifest(value)),
        "CMake target collision:",
        "namespace encoding must not alias distinct artifacts"
    );
}

void test_manifest_rejects_generated_test_collisions() {
    auto value = sample_manifest();
    value.components.front().artifacts.front().id = "tests";
    value.facade_entry_artifact = "sample:tests";
    require_contains(
        join_lines(ecosystem::validate_manifest(value)),
        "CMake target collision: sample:tests and generated tests for sample",
        "authored targets must not alias generated test targets"
    );
    value.components.front().tests.clear();
    require_true(
        ecosystem::validate_manifest(value).empty(),
        "the tests id is available without generated tests"
    );
    value.components.front().tests = json::object({ { "selftest", true } });
    value.components.front().file_units
        = { { "tests/test_main", "source_only" } };
    require_true(
        ecosystem::validate_manifest(value).empty(),
        "explicit test-only artifacts must not reserve a second target"
    );

    for (const std::string output : { "app__tests", "app__tests.exe" }) {
        value = sample_dual_run_manifest();
        value.components.front().tests = json::object({ { "selftest", true } });
        value.components.back().artifacts.front().name = output;
        require_contains(
            join_lines(ecosystem::validate_manifest(value)),
            "artifact output collision: tool:cli and generated tests for app",
            "generated executable filenames must be reserved across components "
            "and platforms"
        );
        temp_dir root;
        const auto report = ecosystem::sync_project(root.path(), value);
        require_true(
            !report.errors.empty() && report.written_files.empty(),
            "test collisions must fail before generation writes"
        );
    }

    value = sample_library_manifest();
    value.components.front().tests = json::object({ { "selftest", true } });
    value.components.front().artifacts.front().name = "core__tests";
    require_true(
        ecosystem::validate_manifest(value).empty(),
        "a library may share the test stem when its output filename differs"
    );
    require_contains(
        ecosystem::generate_developer_cmakelists(value),
        "add_executable(core__tests",
        "the generator must use the reserved test target"
    );
}

} // namespace ecosystem_test_support
