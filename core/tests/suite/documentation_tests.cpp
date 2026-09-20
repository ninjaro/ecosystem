#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

void test_doxygen_configuration_uses_exact_ownership_and_service_state() {
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

void test_cli_doxygen_generates_native_scoped_documentation() {
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

void test_cli_doxy_propagates_native_graphviz_errors_with_zero_tool_exit() {
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

void test_cli_doxy_retains_tool_failures_and_requires_fresh_output() {
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

void test_cli_doxy_rejects_service_output_aliases_before_writing() {
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
         { "doxygen.log", "warnings.log", "html/alias.html" }) {
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

void test_cli_workspace_doxy_keeps_selected_artifact_outputs_separate() {
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
        root.path() / "manifesto.workspace.json",
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

void test_cli_check_doxy_rejects_failed_configuration() {
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

void test_cli_check_sphinx_generates_local_conf_with_rtd_theme() {
    temp_dir root;
    write_sample_sphinx_project(root.path());

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path sphinx_log = fake_root / "sphinx.log";
    fs::create_directories(fake_bin);

    write_executable_script(
        fake_bin / "sphinx-build",
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "printf 'theme=%s\\n' "
        "\"${MANIFESTO_SPHINX_THEME:-${ECOSYSTEM_SPHINX_THEME:-}}\" >> "
        "\"$FAKE_SPHINX_LOG\"\n"
        "printf '%s\\n' \"$*\" >> \"$FAKE_SPHINX_LOG\"\n"
        "build_dir=\"\"\n"
        "conf_dir=\"\"\n"
        "source_dir=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  case \"$1\" in\n"
        "    -b)\n"
        "      shift 2\n"
        "      ;;\n"
        "    -c)\n"
        "      conf_dir=\"$2\"\n"
        "      shift 2\n"
        "      ;;\n"
        "    *)\n"
        "      if [ -z \"$source_dir\" ]; then\n"
        "        source_dir=\"$1\"\n"
        "      elif [ -z \"$build_dir\" ]; then\n"
        "        build_dir=\"$1\"\n"
        "      fi\n"
        "      shift\n"
        "      ;;\n"
        "  esac\n"
        "done\n"
        "test -f \"$conf_dir/conf.py\"\n"
        "test -f \"$source_dir/index.md\"\n"
        "mkdir -p \"$build_dir\"\n"
        "exit 0\n"
    );

    const std::string original_path = []() {
        const char* value = std::getenv("PATH");
        return value == nullptr ? std::string() : std::string(value);
    }();
    scoped_env path_env("PATH", fake_bin.string() + ":" + original_path);
    scoped_env sphinx_log_env("FAKE_SPHINX_LOG", sphinx_log.string());

    const cli_result result = run_cli(root.path(), "check sphinx");
    require_true(
        result.exit_code == 0,
        "ecos check sphinx must build a conventional docs tree"
    );
    require_contains(
        result.output, "sphinx generated in .ecosystem/sphinx/html",
        "ecos check sphinx must report the generated html output path"
    );
    require_contains(
        read_text(root.path() / ".ecosystem/sphinx/conf.py"),
        "MANIFESTO_SPHINX_THEME",
        "ecos check sphinx must default to the Read the Docs theme"
    );
    require_contains(
        read_text(root.path() / ".ecosystem/sphinx/conf.py"), "'myst_parser'",
        "ecos check sphinx must enable MyST when markdown docs are present"
    );
    require_contains(
        read_text(sphinx_log), "-b html",
        "ecos check sphinx must invoke sphinx-build in html mode"
    );
}

void test_cli_check_sphinx_theme_flag_sets_theme_override() {
    temp_dir root;
    write_sample_sphinx_project(root.path());

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path sphinx_log = fake_root / "sphinx.log";
    fs::create_directories(fake_bin);

    write_executable_script(
        fake_bin / "sphinx-build",
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "printf 'theme=%s\\n' "
        "\"${MANIFESTO_SPHINX_THEME:-${ECOSYSTEM_SPHINX_THEME:-}}\" >> "
        "\"$FAKE_SPHINX_LOG\"\n"
        "printf '%s\\n' \"$*\" >> \"$FAKE_SPHINX_LOG\"\n"
        "build_dir=\"\"\n"
        "conf_dir=\"\"\n"
        "source_dir=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  case \"$1\" in\n"
        "    -b)\n"
        "      shift 2\n"
        "      ;;\n"
        "    -c)\n"
        "      conf_dir=\"$2\"\n"
        "      shift 2\n"
        "      ;;\n"
        "    *)\n"
        "      if [ -z \"$source_dir\" ]; then\n"
        "        source_dir=\"$1\"\n"
        "      elif [ -z \"$build_dir\" ]; then\n"
        "        build_dir=\"$1\"\n"
        "      fi\n"
        "      shift\n"
        "      ;;\n"
        "  esac\n"
        "done\n"
        "test -f \"$conf_dir/conf.py\"\n"
        "test -f \"$source_dir/index.md\"\n"
        "mkdir -p \"$build_dir\"\n"
        "exit 0\n"
    );

    const std::string original_path = []() {
        const char* value = std::getenv("PATH");
        return value == nullptr ? std::string() : std::string(value);
    }();
    scoped_env path_env("PATH", fake_bin.string() + ":" + original_path);
    scoped_env sphinx_log_env("FAKE_SPHINX_LOG", sphinx_log.string());

    const cli_result result = run_cli(root.path(), "check sphinx --theme furo");
    require_true(
        result.exit_code == 0,
        "ecos check sphinx --theme must accept an explicit theme override"
    );
    require_contains(
        read_text(sphinx_log), "theme=furo",
        "ecos check sphinx --theme must forward the theme override "
        "into the Sphinx environment"
    );
}

} // namespace ecosystem_test_support
