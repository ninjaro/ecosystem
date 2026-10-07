#include "../documentation.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_sphinx_output_aliases() {
    for (const auto* relative :
         { ".ecosystem", ".ecosystem/sphinx", ".ecosystem/sphinx/conf.py",
           ".ecosystem/sphinx/manifesto_presentation.py",
           ".ecosystem/sphinx/html", ".ecosystem/sphinx/html/nested/index.html",
           ".ecosystem/sphinx/html/.doctrees", ".ecosystem/sphinx/html.pending",
           ".ecosystem/sphinx/html.previous",
           ".ecosystem/sphinx/sphinx.log" }) {
        temp_dir root;
        const auto project = root.path() / "project";
        write_sample_sphinx_project(project);
        const auto authored = read_text(project / "manifest.json");
        const auto destination = project / "authored";
        const auto alias = project / relative;
        const bool file_alias
            = alias.extension() == ".py" || alias.extension() == ".html";
        const bool html_alias
            = std::string(relative).starts_with(".ecosystem/sphinx/html");
        if (html_alias)
            write_text(
                project / ".ecosystem/sphinx/conf.py", "# previous config\n"
            );
        write_text(destination / "keep.txt", "keep authored content\n");
        fs::create_directories(alias.parent_path());
        if (file_alias)
            fs::create_symlink(destination / "keep.txt", alias);
        else
            fs::create_directory_symlink(destination, alias);
        const auto bin = root.path() / "bin";
        const auto invocation = root.path() / "sphinx-invoked";
        write_executable_script(
            bin / "sphinx-build",
            "#!/bin/sh\n"
            "if [ \"$1\" = --version ]; then echo 'sphinx-build 8.0'; exit 0; "
            "fi\n"
            "echo invoked > \"$MANIFESTO_TEST_SPHINX_INVOCATION\"\n"
            "exit 0\n"
        );
        scoped_env path("PATH", bin.string() + ":" + current_path_env());
        scoped_env log("MANIFESTO_TEST_SPHINX_INVOCATION", invocation.string());
        const auto result = run_engels_cli(project, "check sphinx");
        require_true(
            result.exit_code == 5,
            std::string("Sphinx must reject output alias ") + relative
                + " before writing: " + result.output
        );
        require_contains(
            result.output, relative, "output failure must identify its path"
        );
        require_true(
            !fs::exists(invocation)
                && read_text(destination / "keep.txt")
                    == "keep authored content\n"
                && read_text(project / "manifest.json") == authored
                && read_text(project / "docs/index.md") == "# Docs Sample\n",
            "rejected output aliases must preserve authored state and avoid "
            "Sphinx"
        );
        require_true(
            !fs::exists(destination / "conf.py"),
            "preflight must not write through an ancestor alias"
        );
        if (html_alias)
            require_true(
                read_text(project / ".ecosystem/sphinx/conf.py")
                    == "# previous config\n",
                "output preflight must preserve the previous configuration"
            );
        else if (!file_alias || alias.filename() != "conf.py")
            require_true(
                !fs::exists(project / ".ecosystem/sphinx/conf.py"),
                "all output paths must be checked before replacing "
                "configuration"
            );
    }
}

void test_sphinx_failure_recovery() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_sample_sphinx_project(project);
    const auto bin = root.path() / "bin";
    const auto calls = root.path() / "calls";
    const auto directory = ecosystem::local_sphinx_dir(project);
    write_text(directory / "html/index.html", "previous site\n");
    write_text(project / "docs/conf.py", "# authored, not generated\n");
    write_fake_sphinx(bin / "sphinx-build");
    write_fake_doxygen_tool(bin / "doxygen");
    write_executable_script(
        bin / "dot", "#!/bin/sh\necho 'dot version fixture'\n"
    );
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    scoped_env log("FAKE_SPHINX_LOG", calls.string());
    scoped_env doxy_log(
        "DOXYGEN_TEST_LOG", (root.path() / "doxy-calls").string()
    );
    for (const auto* invalid : { "{", "[]", "{\"theme\":\"unknown\"}",
                                 "{\"exclude_patterns\":[false]}" }) {
        write_text(project / "docs/sphinx.json", invalid);
        const auto result = run_engels_cli(project, "check sphinx");
        require_true(
            result.exit_code == 5 && !fs::exists(calls),
            "invalid authored selection must fail before the builder"
        );
        require_contains(
            result.output, "docs/sphinx.json",
            "invalid selection must name its source"
        );
        require_true(
            read_text(directory / "html/index.html") == "previous site\n",
            "invalid config must preserve published output"
        );
    }
    fs::remove(project / "docs/sphinx.json");
    for (const auto* mode : { "fail", "empty", "empty-index", "index-only",
                              "empty-results", "missing-api", "empty-api" }) {
        for (const auto* name : { "index.html", "_manifesto/index.html",
                                  "_manifesto/doxygen/index.html" })
            write_text(directory / "html.pending" / name, "stale candidate\n");
        scoped_env behavior("SPHINX_TEST_MODE", mode);
        const auto result = run_engels_cli(project, "check sphinx");
        require_true(
            result.exit_code == 5,
            "incomplete Sphinx builds must fail: " + result.output
        );
        require_true(
            read_text(directory / "html/index.html") == "previous site\n"
                && read_text(project / "docs/conf.py")
                    == "# authored, not generated\n",
            "failed builds must preserve published and authored state"
        );
        require_not_contains(
            result.output, "sphinx generated",
            "failed candidates are not published"
        );
        require_contains(
            result.output,
            mode == std::string("fail") ? "exit 9" : "no fresh HTML index",
            "failure must explain why output was rejected"
        );
        if (std::string(mode) == "index-only"
            || std::string(mode) == "empty-results")
            require_contains(
                result.output, "html.pending/_manifesto/index.html",
                "identify the missing project results page"
            );
        if (std::string(mode) == "missing-api"
            || std::string(mode) == "empty-api")
            require_contains(
                result.output, "html.pending/_manifesto/doxygen/index.html",
                "identify the missing bundled API index"
            );
        if (mode == std::string("fail"))
            require_contains(
                read_text(directory / "sphinx.log"),
                "missing Breathe dependency", "retain the native failure log"
            );
    }
    fs::remove(calls);
    {
        scoped_env behavior("DOXYGEN_TEST_MODE", "html-only");
        const auto result = run_engels_cli(project, "check sphinx");
        require_true(
            result.exit_code == 5 && !fs::exists(calls),
            "Sphinx must not consume absent or stale XML"
        );
        require_contains(
            result.output, "no XML index",
            "upstream failures must retain attribution"
        );
    }
    write_text(directory / "html/retired.html", "old page\n");
    const auto result
        = run_engels_cli(project, "check sphinx --theme alabaster");
    require_true(
        result.exit_code == 0, "fresh candidate must publish: " + result.output
    );
    require_contains(
        read_text(directory / "html/index.html"), "fresh Sphinx",
        "publish the candidate"
    );
    require_true(
        !fs::exists(directory / "html/retired.html")
            && !fs::exists(directory / "html.previous"),
        "successful publication must retire old pages"
    );
    require_contains(
        read_text(calls), "-E -a -W --keep-going",
        "rebuild fresh and reject documentation warnings"
    );

    fs::rename(bin / "doxygen", bin / "doxygen.saved");
    {
        scoped_env isolated("PATH", bin.string());
        const auto missing = run_engels_cli(project, "check sphinx");
        require_true(
            missing.exit_code == 4, "missing Doxygen must fail Sphinx preflight"
        );
        require_contains(
            missing.output, "doxygen is unavailable", "name the prerequisite"
        );
    }
    fs::rename(bin / "doxygen.saved", bin / "doxygen");
    write_text(directory / "html.previous/keep.html", "recovery state\n");
    const auto recovery = run_engels_cli(project, "check sphinx");
    require_true(
        recovery.exit_code == 5, "recovery state must not be overwritten"
    );
    require_contains(
        recovery.output, "restore or move", "explain how to recover"
    );
    require_true(
        read_text(directory / "html.previous/keep.html") == "recovery state\n",
        "retain interrupted publication recovery"
    );
}

} // namespace ecosystem_test_support
