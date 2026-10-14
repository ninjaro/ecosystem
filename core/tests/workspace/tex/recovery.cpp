#include "../../suite/test_support.hpp"
#include "../tex.hpp"

namespace ecosystem_test_support {

void test_tex_failures() {
    temp_dir root;
    write_sample_build_project(root.path(), "tex_failures");
    write_tex_tools(root.path());
    scoped_env path("PATH", (root.path() / "bin").string());
    write_text(root.path() / "tex/a.tex", "Original a");
    write_text(root.path() / "tex/b.tex", "Original b");
    auto result = run_marx_cli(root.path(), "build tex");
    require_true(result.exit_code == 0, result.output);
    const auto index = read_text(root.path() / "assets/showcase/index.tsv");
    const auto a = read_text(root.path() / "assets/showcase/tex/a.pdf");
    const auto b = read_text(root.path() / "assets/showcase/tex/b.pdf");
    write_text(root.path() / "retained.pdf", "%PDF-1.4\nRetained");
    write_text(root.path() / "tex/a.tex", "New a");
    for (const auto* mode : { "failure", "missing", "invalid", "symlink",
                              "hardlink", "directory", "fifo" }) {
        scoped_env fail("ECOSYSTEM_TEX_FIXTURE", mode);
        result = run_marx_cli(root.path(), "build tex");
        require_true(
            result.exit_code == 5
                && read_text(root.path() / "assets/showcase/index.tsv") == index
                && read_text(root.path() / "assets/showcase/tex/a.pdf") == a
                && read_text(root.path() / "assets/showcase/tex/b.pdf") == b,
            "native errors and invalid fresh output preserve the whole "
            "previous batch: "
                + std::string(mode)
        );
        require_true(
            !fs::exists(root.path() / ".ecosystem/tex/pending"),
            "failed compilation cannot become a later fresh candidate"
        );
        require_contains(
            read_text(root.path() / ".ecosystem/tex/build.log"), "latexmk",
            "native command logs survive failure"
        );
        if (std::string(mode) == "failure") {
            require_contains(
                result.output, "b.tex", "identify the failed selected document"
            );
            require_contains(
                read_text(root.path() / ".ecosystem/tex/build.log"),
                "fixture TeX compilation failed", "retain upstream diagnostics"
            );
        }
    }
    require_true(
        read_text(root.path() / "retained.pdf") == "%PDF-1.4\nRetained",
        "aliased native output cannot alter retained files"
    );
}

void test_tex_rollback() {
    temp_dir root;
    write_sample_build_project(root.path(), "tex_rollback");
    write_tex_tools(root.path());
    scoped_env path("PATH", (root.path() / "bin").string());
    write_text(root.path() / "tex/a.tex", "Original");
    auto result = run_marx_cli(root.path(), "build tex");
    require_true(result.exit_code == 0, result.output);
    const auto index = read_text(root.path() / "assets/showcase/index.tsv");
    const auto pdf = read_text(root.path() / "assets/showcase/tex/a.pdf");
    write_text(root.path() / "tex/a.tex", "Replacement");
    write_text(root.path() / "tex/b.tex", "New document");
    for (const auto* mode : { "index-lock", "metadata-error" }) {
        scoped_env fail("ECOSYSTEM_TEX_FIXTURE", mode);
        result = run_marx_cli(root.path(), "build tex");
        require_true(
            result.exit_code == 5
                && read_text(root.path() / "assets/showcase/index.tsv") == index
                && read_text(root.path() / "assets/showcase/tex/a.pdf") == pdf
                && !fs::exists(root.path() / "assets/showcase/tex/b.pdf"),
            "index publication rejection restores prior PDFs and removes new "
            "ones"
        );
        require_true(
            !fs::exists(root.path() / ".ecosystem/tex/previous"),
            "successful rollback retires its backups"
        );
        if (std::string(mode) == "index-lock") {
            require_true(
                read_text(
                    root.path() / "assets/showcase/.index.tsv.pending/keep"
                ) == "concurrent recovery",
                "rollback must retain another index writer's recovery state"
            );
            fs::remove_all(root.path() / "assets/showcase/.index.tsv.pending");
        } else {
            fs::remove(root.path() / "assets/showcase/readme.md");
        }
    }
    for (const auto* name :
         { ".ecosystem/tex/pending", ".ecosystem/tex/previous",
           "assets/showcase/.index.tsv.pending" }) {
        write_text(root.path() / name / "keep", "Retained recovery");
        const auto calls = read_text(root.path() / "tex_calls.jsonl");
        result = run_marx_cli(root.path(), "build tex");
        require_true(
            result.exit_code == 5
                && read_text(root.path() / name / "keep") == "Retained recovery"
                && read_text(root.path() / "tex_calls.jsonl") == calls,
            "retained recovery fails before native execution or cleanup"
        );
        fs::remove_all(root.path() / name);
    }
    {
        scoped_env fail("ECOSYSTEM_TEX_FIXTURE", "output-obstruction");
        result = run_marx_cli(root.path(), "build tex");
        require_true(
            result.exit_code == 5
                && read_text(root.path() / "assets/showcase/index.tsv") == index
                && read_text(root.path() / "assets/showcase/tex/a.pdf") == pdf
                && read_text(root.path() / "assets/showcase/tex/b.pdf/keep")
                    == "Retained obstruction",
            "publication rechecks output types after compilation and preserves "
            "an obstruction"
        );
    }
}

} // namespace ecosystem_test_support
