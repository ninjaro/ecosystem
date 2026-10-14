#include "../documentation.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_doxygen_ownership() {
    temp_dir root;
    const auto project = root.path() / "project with # spaces";
    write_doxygen_project(project, "docs_sample");
    const auto loaded = ecosystem::load_manifest(project / "manifest.json");
    require_true(
        loaded.value.has_value() && loaded.errors.empty(),
        "Doxygen fixture must load: " + join_lines(loaded.errors)
    );
    const ecosystem::artifact_ref selected { "lib", "first" };
    const auto directory = ecosystem::local_doxygen_dir(project, selected);
    const auto config = directory / "Doxyfile";
    std::string error;
    require_true(
        ecosystem::write_local_doxygen_config(
            project, *loaded.value, selected, &error
        ),
        "selected configuration must render: " + error
    );
    const auto contents = read_text(config);
    require_contains(
        contents, "PROJECT_NAME           = \"docs_sample\"",
        "identity must come from the manifest"
    );
    require_contains(
        contents, "PROJECT_NUMBER         = \"2.3.4\"",
        "version must come from the manifest"
    );
    require_contains(
        contents,
        "OUTPUT_DIRECTORY       = \".ecosystem/doxygen/artifacts/lib/first\"",
        "artifact output must be isolated"
    );
    for (const std::string input :
         { "first/include/model.hpp", "first/src/model.cpp",
           "first/include/model.tpp", "first/tests/model_tests.cpp",
           "first/benchmarks/model_benchmarks.cpp", "first/include/extra.hpp" })
        require_contains(
            contents, "\"" + input + "\"",
            "owned input must be listed explicitly: " + input
        );
    for (const std::string excluded :
         { "second/", "stray.hpp", "README.md", "docs/" })
        require_not_contains(
            contents, excluded,
            "artifact configuration must exclude unrelated inputs"
        );
    require_contains(
        contents, "RECURSIVE              = NO",
        "Doxygen must not broaden explicit ownership by walking directories"
    );
    require_contains(
        contents, "GENERATE_XML           = YES",
        "the same owned inputs must supply Breathe's XML representation"
    );
    require_contains(
        contents, "CLANG_DATABASE_PATH    = \"\"",
        "missing compile state must not invent an obsolete database path"
    );
    require_true(
        !fs::exists(project / "Doxyfile"),
        "configuration must stay inside service state"
    );

    write_text(
        ecosystem::local_build_dir(project, "debug") / "compile_commands.json",
        "[]\n"
    );
    require_true(
        ecosystem::write_local_doxygen_config(
            project, *loaded.value, std::nullopt, &error
        ),
        "project configuration must render"
    );
    const auto full
        = read_text(ecosystem::local_doxygen_dir(project) / "Doxyfile");
    require_contains(
        full, "\"second/include/api.hpp\"",
        "project documentation must include all owned artifacts"
    );
    require_contains(
        full, "\"README.md\"",
        "project-wide documentation may include its root overview"
    );
    require_contains(
        full,
        "CLANG_DATABASE_PATH    = "
        "\".ecosystem/build/project/desktop/debug/default\"",
        "optional Clang configuration must use the current build layout"
    );
    require_true(
        read_text(config) == contents,
        "project generation must not replace artifact configuration"
    );
    fs::remove(project / "first/include/model.hpp");
    require_true(
        !ecosystem::write_local_doxygen_config(
            project, *loaded.value, selected, &error
        ) && read_text(config) == contents,
        "missing owned inputs must fail before replacing configuration"
    );
    require_contains(
        error, "first/include/model.hpp",
        "input failure must identify the selected path"
    );
}

void test_doxygen_native_scope() {
    temp_dir root;
    const auto project = root.path() / "project with # spaces";
    write_doxygen_project(project, "docs_native");
    write_text(project / "Doxyfile", "# unrelated root configuration\n");
    auto result = run_engels_cli(project, "check doxy lib:first");
    require_true(
        result.exit_code == 0,
        "native Doxygen must document selected ownership:\n" + result.output
    );
    const auto directory = ecosystem::local_doxygen_dir(
        project, ecosystem::artifact_ref { "lib", "first" }
    );
    require_true(
        fs::is_regular_file(directory / "html/index.html"),
        "native Doxygen must produce its advertised HTML entry"
    );
    const auto xml = read_text(directory / "xml/index.xml");
    require_contains(
        xml, "selected_public_type", "owned types must reach native XML"
    );
    require_not_contains(
        xml, "unselected_public_type", "XML must preserve artifact scope"
    );
    require_not_contains(
        xml, "stray_public_type", "XML must exclude unowned declarations"
    );
    std::string html;
    for (const auto& entry :
         fs::recursive_directory_iterator(directory / "html"))
        if (entry.is_regular_file() && entry.path().extension() == ".html")
            html += read_text(entry.path());
    require_contains(
        html, "selected_public_type",
        "owned public declarations must reach the native result"
    );
    require_contains(
        html, "quoted_input_type",
        "owned headers must be documented when the project path contains "
        "spaces and hashes"
    );
    require_not_contains(
        html, "unselected_public_type",
        "a shared component namespace must not widen documentation"
    );
    require_not_contains(
        html, "stray_public_type",
        "undeclared root source directories must not be scanned"
    );
    require_true(
        read_text(project / "Doxyfile") == "# unrelated root configuration\n",
        "the root Doxyfile must remain untouched"
    );
    const auto before = read_text(directory / "Doxyfile");
    result = run_engels_cli(project, "check doxy lib:absent");
    require_true(
        result.exit_code == 2 && read_text(directory / "Doxyfile") == before,
        "unknown artifacts must fail before changing service state"
    );
}

void test_doxygen_workspace_scope() {
    temp_dir root;
    write_doxygen_project(root.path() / "alpha", "alpha");
    write_doxygen_project(root.path() / "beta", "beta");
    write_text(root.path() / "broken/manifest.json", "invalid JSON\n");
    const auto bin = root.path() / "bin";
    write_fake_doxygen_tool(bin / "doxygen");
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    const auto calls = root.path() / "calls.log";
    scoped_env log("DOXYGEN_TEST_LOG", calls.string());
    auto result = run_engels_cli(root.path(), "check doxy");
    require_true(
        result.exit_code == 2 && !fs::exists(calls),
        "workspace validity must be checked before any documentation run"
    );
    result = run_engels_cli(
        root.path(),
        "check doxy alpha/lib:first alpha/lib:second beta/lib:first"
    );
    require_true(
        result.exit_code == 0,
        "multiple qualified Doxygen requests must succeed:\n" + result.output
    );
    for (const std::string project : { "alpha", "beta" }) {
        const auto first = ecosystem::local_doxygen_dir(
            root.path() / project, ecosystem::artifact_ref { "lib", "first" }
        );
        require_true(
            fs::is_regular_file(first / "html/index.html"),
            "every selected project must retain its documentation"
        );
        require_not_contains(
            read_text(first / "Doxyfile"), "second/include",
            "artifact config must not widen to its namespace"
        );
    }
    const auto second = ecosystem::local_doxygen_dir(
        root.path() / "alpha", ecosystem::artifact_ref { "lib", "second" }
    );
    require_true(
        fs::is_regular_file(second / "html/index.html"),
        "a second owner in the same project must retain its own HTML"
    );
    require_not_contains(
        read_text(second / "Doxyfile"), "first/include",
        "peer configuration must not be overwritten"
    );
    write_text(
        root.path() / "ecosystem.workspace.json",
        "{\"groups\":{\"docs\":[\"alpha\",\"beta\"]}}\n"
    );
    fs::create_directory(
        ecosystem::local_doxygen_dir(root.path() / "beta") / "doxygen.log"
    );
    result = run_engels_cli(root.path(), "check doxy --group docs");
    require_true(
        result.exit_code == 5
            && fs::is_regular_file(
                ecosystem::local_doxygen_dir(root.path() / "alpha")
                / "html/index.html"
            ),
        "workspace failure must retain successful peer output and fail the "
        "aggregate"
    );
    require_contains(
        result.output,
        "== beta (beta) ==", "workspace errors must retain project attribution"
    );
    require_true(
        fs::is_regular_file(second / "html/index.html"),
        "project-wide generation must not erase existing artifact results"
    );
}

} // namespace ecosystem_test_support
