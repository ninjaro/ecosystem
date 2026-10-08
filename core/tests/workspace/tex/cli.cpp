#include "../../suite/test_support.hpp"
#include "../tex.hpp"

namespace ecosystem_test_support {

void test_tex_tooling() {
    temp_dir root;
    write_sample_build_project(root.path(), "tex_tooling");
    require_sync_success(
        root.path(), "doctor fixture must have current surfaces"
    );
    write_tex_tools(root.path());
    write_text(root.path() / "tex/a.tex", "Selected document");
    scoped_env path("PATH", (root.path() / "bin").string());
    auto inventory = ecosystem::toolchains_report("tex", root.path());
    require_true(
        inventory.size() == 2 && inventory.contains("latexmk")
            && inventory.contains("pdflatex"),
        "TeX generation and doctor select the same upstream tool table"
    );
    const auto ordinary = ecosystem::toolchains_report("debug", root.path());
    require_true(
        !ordinary.contains("latexmk") && !ordinary.contains("pdflatex"),
        "C++ builds do not acquire TeX requirements"
    );
    auto result = run_engels_cli(root.path(), "doctor --profile tex");
    require_true(
        result.exit_code == 0 && !fs::exists(root.path() / ".ecosystem/source"),
        result.output
    );
    require_contains(
        result.output, "latexmk: ok", "doctor identifies required TeX tooling"
    );
    require_true(
        !fs::exists(root.path() / "assets/showcase")
            && !fs::exists(root.path() / "tex_calls.jsonl"),
        "TeX diagnostics do not compile or register assets"
    );
    for (const auto* name : { "latexmk", "pdflatex" }) {
        fs::rename(
            root.path() / "bin" / name, root.path() / "bin/retained-tool"
        );
        result = run_marx_cli(root.path(), "build tex");
        require_true(
            result.exit_code == 4
                && !fs::exists(root.path() / ".ecosystem/tex"),
            "missing selected native tooling fails before staging"
        );
        require_contains(
            result.output, name,
            "missing tool diagnostics identify the requirement"
        );
        result = run_engels_cli(root.path(), "doctor --profile tex");
        require_true(
            result.exit_code == 4, "doctor and execution agree on missing tools"
        );
        fs::rename(
            root.path() / "bin/retained-tool", root.path() / "bin" / name
        );
    }
    write_executable_script(
        root.path() / "bin/pdflatex", "#!/bin/sh\necho version-failed\nexit 7\n"
    );
    result = run_marx_cli(root.path(), "build tex");
    require_true(
        result.exit_code == 4,
        "failed native version probes cannot be treated as available tooling"
    );
    result = run_engels_cli(root.path(), "doctor --profile tex");
    require_true(
        result.exit_code == 4, "doctor rejects the same failed version probe"
    );
}

void test_tex_workspace() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    write_tex_tools(root.path());
    write_text(root.path() / "alpha/tex/a.tex", "Alpha");
    scoped_env path("PATH", (root.path() / "bin").string());
    auto result = run_marx_cli(root.path(), "build tex --project alpha");
    require_true(
        result.exit_code == 0
            && fs::is_regular_file(
                root.path() / "alpha/assets/showcase/tex/a.pdf"
            )
            && !fs::exists(root.path() / "beta/assets/showcase"),
        result.output
    );
    result = run_marx_cli(root.path(), "build tex");
    require_true(result.exit_code == 0, result.output);
    require_contains(
        result.output, "skipped (no root-level TeX documents)",
        "workspace defaults skip unsupported projects"
    );
    result = run_marx_cli(root.path(), "build tex --project beta");
    require_true(
        result.exit_code == 3,
        "explicit unsupported project selection remains an error"
    );
    result = run_marx_cli(root.path(), "build tex --project alpha app:main");
    require_true(
        result.exit_code == 2,
        "workspace artifact selection cannot trigger TeX generation"
    );
    const auto calls = read_text(root.path() / "alpha/tex_calls.jsonl");
    write_text(root.path() / "broken/manifest.json", "{ invalid\n");
    result = run_marx_cli(root.path(), "build tex");
    require_true(
        result.exit_code == 2
            && read_text(root.path() / "alpha/tex_calls.jsonl") == calls,
        "invalid workspace projects fail before selected generation starts"
    );
}

} // namespace ecosystem_test_support
