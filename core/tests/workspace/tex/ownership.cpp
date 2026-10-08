#include "../../suite/test_support.hpp"
#include "../tex.hpp"
#include "workspace/assets.hpp"

namespace ecosystem_test_support {

void test_tex_aliases() {
    for (const auto* name :
         { "tex", "tex/a.tex", "tex/parts/content.tex", ".ecosystem",
           ".ecosystem/tex/build.log", "assets/showcase/tex/a.pdf",
           "assets/showcase/index.tsv" }) {
        temp_dir root, outside;
        write_sample_build_project(root.path(), "tex_aliases");
        write_tex_tools(root.path());
        scoped_env path("PATH", (root.path() / "bin").string());
        write_text(root.path() / "tex/a.tex", "Selected document");
        const auto alias = root.path() / name;
        const bool directory
            = std::string(name) == "tex" || std::string(name) == ".ecosystem";
        if (directory) {
            fs::remove_all(alias);
            write_text(outside.path() / "a.tex", "Outside source");
            fs::create_directory_symlink(outside.path(), alias);
        } else {
            fs::remove(alias);
            write_text(outside.path() / "retained", "Outside input/output");
            fs::create_directories(alias.parent_path());
            fs::create_symlink(outside.path() / "retained", alias);
        }
        const auto result = run_marx_cli(root.path(), "build tex");
        require_true(
            result.exit_code == (std::string(name) == ".ecosystem" ? 2 : 5)
                && !fs::exists(root.path() / "tex_calls.jsonl"),
            "aliased TeX inputs and destinations fail before native execution: "
                + std::string(name) + " (exit "
                + std::to_string(result.exit_code) + "): " + result.output
        );
        require_true(
            read_text(outside.path() / (directory ? "a.tex" : "retained"))
                == (directory ? "Outside source" : "Outside input/output"),
            "external aliases remain untouched"
        );
    }
    temp_dir root;
    write_sample_build_project(root.path(), "tex_hardlinks");
    write_tex_tools(root.path());
    scoped_env path("PATH", (root.path() / "bin").string());
    write_text(root.path() / "tex/a.tex", "Selected document");
    write_text(root.path() / "retained.pdf", "%PDF-1.4\nKeep");
    fs::create_directories(root.path() / "assets/showcase/tex");
    fs::create_hard_link(
        root.path() / "retained.pdf", root.path() / "assets/showcase/tex/a.pdf"
    );
    auto result = run_marx_cli(root.path(), "build tex");
    require_true(
        result.exit_code == 5 && !fs::exists(root.path() / "tex_calls.jsonl"),
        "aliased existing PDFs fail before overwriting"
    );
    require_true(
        read_text(root.path() / "retained.pdf") == "%PDF-1.4\nKeep",
        "hard-linked originals remain unchanged"
    );
}

void test_tex_ownership() {
    for (const auto* mode : { "unindexed", "id", "path", "malformed", "readme",
                              "missing-other" }) {
        temp_dir root;
        write_sample_build_project(root.path(), "tex_ownership");
        write_tex_tools(root.path());
        scoped_env path("PATH", (root.path() / "bin").string());
        write_text(root.path() / "tex/a.tex", "Selected document");
        write_text(root.path() / "assets/showcase/tex/a.pdf", "Authored PDF");
        std::string error;
        const std::string kind(mode);
        if (kind == "id") {
            write_text(root.path() / "assets/showcase/manual.pdf", "Manual");
            require_true(
                ecosystem::put_asset_entry(
                    root.path(), "showcase",
                    { "tex:a", "manual.pdf", "pdf", "Manual", {} }, {}, &error
                ),
                error
            );
        } else if (kind == "path") {
            require_true(
                ecosystem::put_asset_entry(
                    root.path(), "showcase",
                    { "manual", "tex/a.pdf", "pdf", "Manual", {} }, {}, &error
                ),
                error
            );
        } else if (kind == "malformed") {
            write_text(
                root.path() / "assets/showcase/index.tsv", "Invalid index"
            );
        } else if (kind == "readme") {
            write_text(root.path() / "assets/showcase/readme.md", "No marker");
        } else if (kind == "missing-other") {
            require_true(
                ecosystem::put_asset_entry(
                    root.path(), "showcase",
                    { "tex:a", "tex/a.pdf", "pdf", "Generated", {} }, {}, &error
                ),
                error
            );
            write_text(root.path() / "assets/showcase/b.pdf", "Other");
            require_true(
                ecosystem::put_asset_entry(
                    root.path(), "showcase",
                    { "b", "b.pdf", "pdf", "Other", {} }, {}, &error
                ),
                error
            );
            fs::remove(root.path() / "assets/showcase/b.pdf");
        }
        const auto index = root.path() / "assets/showcase/index.tsv";
        const auto original = fs::exists(index) ? read_text(index) : "";
        const auto result = run_marx_cli(root.path(), "build tex");
        require_true(
            result.exit_code == 5
                && !fs::exists(root.path() / "tex_calls.jsonl")
                && read_text(root.path() / "assets/showcase/tex/a.pdf")
                    == "Authored PDF"
                && (fs::exists(index) ? read_text(index) : "") == original,
            "ownership conflicts and invalid metadata fail before generation: "
                + kind
        );
    }
}

} // namespace ecosystem_test_support
