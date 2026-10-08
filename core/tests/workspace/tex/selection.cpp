#include "../../suite/test_support.hpp"
#include "../tex.hpp"
#include "workspace/assets.hpp"
#include "workspace/tex.hpp"

namespace ecosystem_test_support {

void test_tex_selection() {
    temp_dir root;
    write_sample_build_project(root.path(), "tex_selection");
    write_tex_tools(root.path());
    scoped_env path("PATH", (root.path() / "bin").string());
    auto result = run_marx_cli(root.path(), "build tex");
    require_true(
        result.exit_code == 3 && !fs::exists(root.path() / ".ecosystem/tex"),
        "missing TeX sources report unsupported without staging or tool "
        "execution"
    );
    write_text(root.path() / "tex/parts/nested.tex", "Supporting source");
    write_text(root.path() / "tex/styles/plain.sty", "Supporting style");
    result = run_marx_cli(root.path(), "build tex");
    require_true(
        result.exit_code == 3, "nested TeX alone does not activate generation"
    );
    write_text(root.path() / "tex/z.tex", "Z document");
    write_text(root.path() / "tex/a draft.tex", "A document");
    write_text(
        root.path() / "tex/ignored.TEX", "Case-sensitive supporting file"
    );
    const auto sources = ecosystem::tex_sources(root.path());
    require_true(
        sources.size() == 2 && sources[0].filename() == "a draft.tex"
            && sources[1].filename() == "z.tex",
        "only root .tex files are selected in deterministic order"
    );
    result = run_marx_cli(root.path(), "build tex");
    require_true(result.exit_code == 0, result.output);
    const auto entries = ecosystem::read_asset_index(root.path(), "showcase");
    require_true(
        entries.size() == 2 && entries[0].id == "tex:a draft"
            && entries[1].id == "tex:z",
        "automatic registration follows selected source order"
    );
    std::istringstream calls(read_text(root.path() / "tex_calls.jsonl"));
    std::string line;
    std::size_t count = 0;
    while (std::getline(calls, line)) {
        const auto call = json::parse(line);
        require_true(
            call.at("cwd") == (root.path() / "tex").string(),
            "supporting inputs resolve from tex/"
        );
        require_contains(
            call.dump(), "-norc",
            "compiler configuration stays in the selected invocation"
        );
        require_contains(
            call.dump(), "-no-shell-escape", "TeX does not need shell escape"
        );
        require_contains(
            call.dump(), "-halt-on-error", "native failures stop generation"
        );
        ++count;
    }
    require_true(
        count == 2 && !fs::exists(root.path() / ".ecosystem/source")
            && !fs::exists(root.path() / ".ecosystem/tex/pending")
            && !fs::exists(root.path() / ".ecosystem/tex/previous"),
        "only selected documents run, with no C++ configuration or retained "
        "successful staging"
    );
    require_true(
        read_text(root.path() / "tex/parts/nested.tex") == "Supporting source",
        "supporting sources remain unchanged"
    );
    result = run_marx_cli(root.path(), "build tex app:main");
    require_true(
        result.exit_code == 2, "TeX generation rejects artifact filters"
    );
    fs::remove(root.path() / "manifest.json");
    result = run_marx_cli(root.path(), "build tex");
    require_true(
        result.exit_code == 2, "TeX generation requires a valid manifest"
    );
}

void test_tex_registration() {
    temp_dir root;
    write_sample_build_project(root.path(), "tex_registration");
    write_tex_tools(root.path());
    scoped_env path("PATH", (root.path() / "bin").string());
    write_text(root.path() / "assets/showcase/still.png", "Authored image");
    std::string error;
    require_true(
        ecosystem::put_asset_entry(
            root.path(), "showcase",
            { "still", "still.png", "image", "Keep", {} }, {}, &error
        ),
        error
    );
    write_text(root.path() / "tex/a.tex", "First");
    write_text(root.path() / "tex/b.tex", "Second");
    auto result = run_marx_cli(root.path(), "build tex");
    require_true(result.exit_code == 0, result.output);
    auto entries = ecosystem::read_asset_index(root.path(), "showcase");
    require_true(
        entries.size() == 3 && entries[0].id == "still"
            && entries[1].path == "tex/a.pdf" && entries[2].type == "pdf",
        "generated PDFs append without changing existing ownership"
    );
    auto b = entries[2];
    b.description = "Caption with \"quotes\"\tand\nlines";
    b.datetime = "2026-10-08";
    require_true(
        ecosystem::put_asset_entry(root.path(), "showcase", b, "still", &error),
        error
    );
    const auto original_index
        = read_text(root.path() / "assets/showcase/index.tsv");
    write_text(root.path() / "tex/a.tex", "Rebuilt first");
    fs::remove(root.path() / "assets/showcase/tex/b.pdf");
    result = run_marx_cli(root.path(), "build tex");
    require_true(result.exit_code == 0, result.output);
    require_true(
        read_text(root.path() / "assets/showcase/index.tsv") == original_index
            && read_text(root.path() / "assets/showcase/tex/a.pdf")
                == "%PDF-1.4\nRebuilt first"
            && read_text(root.path() / "assets/showcase/tex/b.pdf")
                == "%PDF-1.4\nSecond",
        "rebuilds restore missing generated media and retain metadata/order "
        "exactly"
    );
    require_true(
        read_text(root.path() / "assets/showcase/still.png")
            == "Authored image",
        "generation preserves unrelated media"
    );
    fs::remove(root.path() / "tex/b.tex");
    result = run_marx_cli(root.path(), "build tex");
    require_true(
        result.exit_code == 0
            && read_text(root.path() / "assets/showcase/index.tsv")
                == original_index
            && fs::is_regular_file(root.path() / "assets/showcase/tex/b.pdf"),
        "removing a source does not silently discard a retained showcase "
        "artifact"
    );
}

} // namespace ecosystem_test_support
