#include "../documentation.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_doxygen_graphviz_errors() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_doxygen_project(project, "docs_graph_failure");
    write_text(
        project / "first/include/model.hpp",
        "struct base {};\nstruct derived : base {};\n"
    );
    const auto bin = root.path() / "bin";
    write_executable_script(
        bin / "dot",
        "#!/bin/sh\n"
        "if [ \"$1\" = \"-V\" ]; then echo 'dot fixture'; exit 0; fi\n"
        "echo 'native_graph_fixture_failed' >&2\nexit 7\n"
    );
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    const auto result = run_engels_cli(project, "check doxy lib:first");
    const auto directory = ecosystem::local_doxygen_dir(
        project, ecosystem::artifact_ref { "lib", "first" }
    );
    require_true(
        result.exit_code == 5
            && fs::is_regular_file(directory / "html/index.html"),
        "native graph failures must fail even when Doxygen has produced an "
        "HTML index"
    );
    require_contains(
        result.output, "doxygen reported errors",
        "zero-exit native errors must remain operation failures"
    );
    require_contains(
        result.output, "Problems running dot",
        "terminal must display the error from Doxygen's warning log"
    );
    require_contains(
        read_text(directory / "warnings.log"), "exit code=7",
        "native graph process evidence must be retained"
    );
    require_contains(
        read_text(directory / "doxygen.log"), "native_graph_fixture_failed",
        "graph stderr must be retained with tool output"
    );
}

void test_doxygen_fresh_output() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_sample_build_project(project, "docs_failures");
    const auto bin = root.path() / "bin";
    fs::create_directories(bin);
    const auto calls = root.path() / "calls.log";
    scoped_env log("DOXYGEN_TEST_LOG", calls.string());
    const auto directory = ecosystem::local_doxygen_dir(project);
    {
        scoped_env path("PATH", bin.string());
        auto result = run_engels_cli(project, "check doxy");
        require_true(
            result.exit_code == 4 && !fs::exists(directory),
            "missing Doxygen must fail before writing config"
        );
        require_contains(
            result.output, "doxygen is not available",
            "missing tooling must name Doxygen"
        );
        write_fake_doxygen_tool(bin / "doxygen");
        result = run_engels_cli(project, "check doxy");
        require_true(
            result.exit_code == 4 && !fs::exists(directory)
                && !fs::exists(calls),
            "missing Graphviz must not start generation"
        );
        require_contains(
            result.output, "Graphviz dot",
            "shared graph policy must name its missing dependency"
        );
        result = run_engels_cli(project, "doctor doxy");
        require_true(
            result.exit_code == 4,
            "doctor must use the same required tool inventory"
        );
        require_contains(
            result.output, "Graphviz dot: missing",
            "doctor must expose the missing graph renderer"
        );
    }
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    {
        scoped_env mode("DOXYGEN_TEST_MODE", "fail");
        const auto result = run_engels_cli(project, "check doxy");
        require_true(
            result.exit_code == 5,
            "nonzero Doxygen status must fail the operation"
        );
        require_contains(
            result.output, "exit 7", "failure must retain the tool exit status"
        );
        require_contains(
            result.output, ".ecosystem/doxygen/Doxyfile",
            "failure must identify the exact config"
        );
        require_contains(
            result.output, "unable to execute graph renderer",
            "failure must display the native diagnostic"
        );
        require_contains(
            read_text(directory / "doxygen.log"),
            "unable to execute graph renderer",
            "tool output must persist on failure"
        );
    }
    write_text(directory / "html/index.html", "stale success\n");
    write_text(directory / "html/removed_owner.html", "stale owner\n");
    {
        scoped_env mode("DOXYGEN_TEST_MODE", "empty");
        const auto result = run_engels_cli(project, "check doxy");
        require_true(
            result.exit_code == 5 && !fs::exists(directory / "html/index.html"),
            "zero exit with no fresh HTML must not accept an old result"
        );
        require_contains(
            result.output, "no HTML index", "incomplete output must be explicit"
        );
    }
    const auto result = run_engels_cli(project, "check doxy");
    require_true(
        result.exit_code == 0
            && fs::is_regular_file(directory / "html/index.html")
            && !fs::exists(directory / "html/removed_owner.html"),
        "successful generation must replace stale output within this scope"
    );
    write_text(directory / "xml/removed_owner.xml", "stale owner\n");
    {
        scoped_env mode("DOXYGEN_TEST_MODE", "html-only");
        const auto incomplete = run_engels_cli(project, "check doxy");
        require_true(
            incomplete.exit_code == 5
                && !fs::exists(directory / "xml/index.xml")
                && !fs::exists(directory / "xml/removed_owner.xml"),
            "a successful HTML-only run must not reuse stale XML"
        );
        require_contains(
            incomplete.output, "no XML index", "missing XML must fail clearly"
        );
    }
    require_contains(
        result.output,
        "doxygen warnings:", "successful runs must locate retained warnings"
    );
    require_contains(
        read_text(directory / "warnings.log"), "fixture documentation warning",
        "warnings must remain inspectable without changing their current "
        "advisory policy"
    );
    require_contains(
        read_text(directory / "doxygen.log"), "fixture documentation generated",
        "successful tool output must persist too"
    );
}

void test_doxygen_output_aliases() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_sample_build_project(project, "docs_aliases");
    const auto bin = root.path() / "bin";
    write_fake_doxygen_tool(bin / "doxygen");
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    const auto calls = root.path() / "calls.log";
    scoped_env log("DOXYGEN_TEST_LOG", calls.string());
    const auto directory = ecosystem::local_doxygen_dir(project);
    const auto config = directory / "Doxyfile";
    write_text(config, "stale config\n");
    write_text(directory / "html/index.html", "previous output\n");
    const auto authored = project / "authored.txt";
    const auto outside = root.path() / "outside.txt";
    write_text(authored, "authored content\n");
    write_text(outside, "outside content\n");
    for (const auto& name :
         { "doxygen.log", "warnings.log", "html/alias.html", "xml" }) {
        fs::create_symlink(
            name == std::string("warnings.log") ? outside : authored,
            directory / name
        );
        const auto result = run_engels_cli(project, "check doxy");
        require_true(
            result.exit_code == 5 && !fs::exists(calls)
                && read_text(config) == "stale config\n"
                && read_text(directory / "html/index.html")
                    == "previous output\n",
            "output aliases must fail before config replacement, cleanup or "
            "execution"
        );
        require_true(
            read_text(authored) == "authored content\n"
                && read_text(outside) == "outside content\n",
            "service writes must not reach aliased authored or external files"
        );
        fs::remove(directory / name);
    }
    fs::rename(config, directory / "saved-config");
    fs::create_symlink(authored, config);
    auto result = run_engels_cli(project, "check doxy");
    require_true(
        result.exit_code == 5 && read_text(authored) == "authored content\n",
        "a config symlink inside the project must also be rejected"
    );
    fs::remove(config);
    fs::rename(directory / "saved-config", config);
    fs::create_directory(directory / "doxygen.log");
    result = run_engels_cli(project, "check doxy");
    require_true(
        result.exit_code == 5 && !fs::exists(calls)
            && read_text(config) == "stale config\n",
        "an unwritable log shape must fail before generation"
    );
    require_contains(
        result.output, "doxygen.log",
        "output failures must identify the failing path"
    );
    fs::remove(directory / "doxygen.log");
    const auto saved = project / ".ecosystem/doxygen-saved";
    fs::rename(directory, saved);
    fs::create_directory_symlink(saved, directory);
    result = run_engels_cli(project, "check doxy");
    require_true(
        result.exit_code == 5 && !fs::exists(calls)
            && read_text(saved / "Doxyfile") == "stale config\n",
        "service directory aliases must not redirect configuration writes"
    );
}

void test_doxygen_config_errors() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_sample_build_project(project, "sample");
    const auto bin = root.path() / "bin";
    const auto log = root.path() / "doxygen.log";
    write_fake_doxygen_tool(bin / "doxygen");
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    scoped_env tool_log("DOXYGEN_TEST_LOG", log.string());
    const auto overrides = root.path() / "templates";
    scoped_env templates("MANIFESTO_TEMPLATE_ROOT", overrides.string());
    const auto config = ecosystem::local_doxygen_dir(project) / "Doxyfile";
    write_text(project / "Doxyfile", "# root config must remain untouched\n");
    write_text(config, "# stale configuration\n");
    write_text(overrides / "tooling/Doxyfile.tpl", "{{missing_binding}}\n");
    auto require_failed = [&]() {
        const auto result = run_engels_cli(project, "check doxy");
        require_true(result.exit_code != 0, "invalid Doxygen config must fail");
        require_contains(
            result.output, "error[task_failed]", "failure must reach the CLI"
        );
        require_contains(
            result.output, "Doxyfile", "failure must name its config"
        );
        require_true(
            !fs::exists(log), "Doxygen must not run with invalid config"
        );
        return result;
    };
    const auto malformed = require_failed();
    require_contains(
        malformed.output, "tooling/Doxyfile.tpl",
        "render failure must name its template"
    );
    require_true(
        read_text(config) == "# stale configuration\n",
        "render failure must preserve existing config"
    );

    write_text(
        overrides / "tooling/Doxyfile.tpl", "PROJECT_NAME = {{project_name}}\n"
    );
    fs::rename(config, project / "Doxyfile.saved");
    fs::create_directory(config);
    require_failed();
    fs::rename(config, project / "Doxyfile.directory");
    const auto outside = root.path() / "outside-config";
    write_text(outside, "# outside project\n");
    fs::create_symlink(outside, config);
    require_failed();
    require_true(
        read_text(outside) == "# outside project\n",
        "config generation must not follow escaping symlinks"
    );

    fs::rename(config, project / "Doxyfile.symlink");
    const auto valid = run_engels_cli(project, "check doxy");
    require_true(
        valid.exit_code == 0,
        "valid Doxygen config must still run:\n" + valid.output
    );
    require_contains(
        read_text(config), "PROJECT_NAME = \"sample\"",
        "Doxygen must receive the rendered config"
    );
    require_true(
        read_text(project / "Doxyfile")
            == "# root config must remain untouched\n",
        "service materialization must not alter a root Doxyfile"
    );
    require_contains(
        read_text(log), ".ecosystem/doxygen/Doxyfile",
        "successful generation must invoke Doxygen"
    );
}

} // namespace ecosystem_test_support
