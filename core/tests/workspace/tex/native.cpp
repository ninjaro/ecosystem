#include "../../suite/test_support.hpp"
#include "../tex.hpp"
#include "workspace/assets.hpp"

namespace ecosystem_test_support {

void test_tex_native() {
    temp_dir root;
    const auto project = root.path() / "TeX project with spaces";
    write_doxygen_project(project, "native_tex");
    write_text(project / "docs/index.rst", "Native TeX\n==========\n");
    const auto source = project / "tex/paper draft.tex";
    write_text(source, R"tex(\documentclass{article}
\usepackage{sample}
\begin{document}
\section{Generated document}\label{intro}
\sampleword
\include{parts/content}
Reference: section~\ref{intro}. Citation~\cite{reference}.
\bibliographystyle{plain}
\bibliography{bib/sources}
\end{document}
)tex");
    write_text(
        project / "tex/sample.sty",
        "\\ProvidesPackage{sample}\n\\newcommand{\\sampleword}{Supporting "
        "style.}\n"
    );
    write_text(
        project / "tex/parts/content.tex", "Nested supporting content.\n"
    );
    write_text(
        project / "tex/parts/not_a_document.tex", "\\notavalidcommand\n"
    );
    write_text(
        project / "tex/bib/sources.bib",
        "@book{reference, title={Bibliography support}, author={Example, "
        "Author}, year={2026}}\n"
    );
    auto result = run_marx_cli(project, "build tex");
    require_true(result.exit_code == 0, result.output);
    const auto entries = ecosystem::read_asset_index(project, "showcase");
    require_true(
        entries.size() == 1 && entries[0].id == "tex:paper draft",
        "native root document is registered once"
    );
    const auto pdf = project / "assets/showcase/tex/paper draft.pdf";
    const auto original = read_text(pdf);
    require_true(
        original.starts_with("%PDF-"), "upstream compiler produces a native PDF"
    );
    const auto text = ecosystem::capture_command_result(
        { "pdftotext", pdf.string(), "-" }, project
    );
    require_true(text.exit_code == 0, text.output);
    for (const auto* expected :
         { "Supporting style", "Nested supporting content",
           "Bibliography support", "Reference: section 1" })
        require_contains(
            text.output, expected,
            "native generation resolves supporting inputs and reruns references"
        );
    require_true(
        !fs::exists(source.parent_path() / "paper draft.aux")
            && !fs::exists(source.parent_path() / "paper draft.pdf"),
        "auxiliary output stays outside authored TeX sources"
    );
    const auto index = read_text(project / "assets/showcase/index.tsv");
    result = run_engels_cli(project, "check sphinx");
    require_true(result.exit_code == 0, result.output);
    const auto page = ecosystem::local_sphinx_dir(project)
        / "html/_ecosystem/catalog/showcase/index.html";
    const auto html = read_text(page);
    require_contains(
        html, "First page: paper draft",
        "registered TeX PDFs reach the native carousel preview"
    );
    require_contains(
        html, "paper%20draft.pdf", "native presentation links the generated PDF"
    );
    require_true(
        read_text(project / "assets/showcase/index.tsv") == index
            && read_text(pdf) == original,
        "Sphinx leaves generated artifact and controller ownership unchanged"
    );
    write_text(
        source,
        "\\documentclass{article}\n\\begin{document}\n\\invalidcommand\n\\end{"
        "document}\n"
    );
    result = run_marx_cli(project, "build tex");
    require_true(
        result.exit_code == 5 && read_text(pdf) == original
            && read_text(project / "assets/showcase/index.tsv") == index
            && read_text(page) == html,
        "a real native TeX failure preserves registered PDFs, index and "
        "published site"
    );
    require_contains(
        read_text(project / ".ecosystem/tex/build.log"),
        "Undefined control sequence", "retain upstream TeX error details"
    );
}

} // namespace ecosystem_test_support
