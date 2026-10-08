#include "../documentation.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_sphinx_native_pipeline() {
    temp_dir root;
    const auto project = root.path() / "native docs with # spaces";
    write_doxygen_project(project, "native_docs");
    write_text(project / "docs/index.rst", R"rst(Public API
==========

.. toctree::

   guide
   api
)rst");
    write_text(
        project / "docs/guide.md", "# User guide\n\nNative Markdown page.\n"
    );
    write_text(project / "docs/archive/broken.rst", ".. absent-directive::\n");
    write_text(
        project / "docs/sphinx.json",
        "{\"exclude_patterns\":[\"archive/**\"]}\n"
    );
    write_text(project / "docs/api.rst", R"rst(C++ reference
=============

.. doxygenstruct:: selected_public_type
   :members:
)rst");
    auto result = run_engels_cli(project, "check sphinx");
    require_true(
        result.exit_code == 0,
        "native Doxygen/Breathe/Sphinx must build: " + result.output
    );
    const auto directory = ecosystem::local_sphinx_dir(project);
    const auto api = read_text(directory / "html/api.html");
    require_contains(
        api, "_manifesto/index.html", "public pages must link project results"
    );
    require_true(
        fs::is_regular_file(directory / "html/_manifesto/doxygen/index.html"),
        "standalone Doxygen HTML must be available from the same site"
    );
    require_contains(
        read_text(directory / "html/_manifesto/index.html"),
        "No verification evidence was selected",
        "local docs must not claim unselected verification"
    );
    require_contains(
        api, "selected_public_type", "Breathe must render the C++ symbol"
    );
    require_contains(
        api, "Selected public type", "Doxygen comments must reach Sphinx HTML"
    );
    require_not_contains(
        api, "unselected_public_type",
        "curated API must not publish unrelated internals"
    );
    require_contains(
        read_text(directory / "html/guide.html"), "Native Markdown page",
        "MyST pages must render"
    );
    require_contains(
        read_text(directory / "conf.py"), "'breathe'",
        "generated config must select Breathe"
    );
    const auto missing_extension = root.path() / "missing-extension";
    write_text(
        missing_extension / "breathe/__init__.py",
        "raise ImportError('fixture Breathe unavailable')\n"
    );
    {
        scoped_env pythonpath("PYTHONPATH", missing_extension.string());
        result = run_engels_cli(project, "check sphinx");
        require_true(
            result.exit_code == 5
                && read_text(directory / "html/api.html") == api,
            "missing Breathe must preserve the published site"
        );
        require_contains(
            result.output, "fixture Breathe unavailable",
            "native import diagnostics must survive"
        );
    }

    write_text(
        project / "docs/api.rst",
        "C++ API\n=======\n\n.. doxygenstruct:: absent_public_type\n"
    );
    result = run_engels_cli(project, "check sphinx");
    require_true(
        result.exit_code == 5 && read_text(directory / "html/api.html") == api,
        "missing Breathe references must fail and preserve the site"
    );
    require_contains(
        result.output, "absent_public_type",
        "retain the native Breathe diagnostic"
    );
    write_text(
        project / "docs/api.rst",
        "C++ API\n=======\n\n.. doxygenstruct:: selected_public_type\n"
    );
    result = run_engels_cli(
        project, "check sphinx --theme manifesto_missing_theme"
    );
    require_true(
        result.exit_code == 5 && read_text(directory / "html/api.html") == api,
        "missing theme must fail without replacing the site"
    );
    require_contains(
        result.output, "manifesto_missing_theme",
        "missing module errors must be actionable"
    );
    fs::remove(project / "docs/guide.md");
    write_text(
        project / "docs/index.rst",
        "Public API\n==========\n\n.. toctree::\n\n   api\n"
    );
    result = run_engels_cli(project, "check sphinx --theme alabaster");
    require_true(
        result.exit_code == 0 && !fs::exists(directory / "html/guide.html"),
        "RST-only rebuild must remove retired pages: " + result.output
    );
    require_not_contains(
        read_text(directory / "conf.py"), "'myst_parser'",
        "RST-only projects need no Markdown parser"
    );
    const auto published = read_text(directory / "html/index.html");
    write_text(
        project / "assets/showcase/index.tsv",
        "id\tpath\ttype\tdescription\tdatetime\n"
        "missing\tmissing.png\timage\tAbsent image\t2026-09-27\n"
    );
    result = run_engels_cli(project, "check sphinx --theme alabaster");
    require_true(
        result.exit_code == 5
            && read_text(directory / "html/index.html") == published,
        "failed presentation must preserve the whole previous site"
    );
    require_contains(
        result.output, "missing.png", "presentation errors must name the input"
    );
    write_text(project / "assets/showcase/missing.png", "fixture image");
    result = run_engels_cli(project, "check sphinx --theme alabaster");
    require_true(result.exit_code == 0, result.output);
    require_contains(
        read_text(directory / "html/_manifesto/index.html"), "Absent image",
        "indexed showcase must render using the selected Sphinx theme"
    );
    fs::remove_all(project / "assets/showcase");
    result = run_engels_cli(project, "check sphinx --theme alabaster");
    require_true(
        result.exit_code == 0
            && !fs::exists(directory / "html/_manifesto/showcase/missing.png"),
        "rebuilding must retire removed showcase output"
    );
    test_native_asset_pages(project);
    test_native_diagnostics(project);
    test_native_benchmarks(project);
}

} // namespace ecosystem_test_support
