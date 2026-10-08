#include "../test_cases.hpp"
#include "../test_support.hpp"
#include "analysis/diagnostic_view.hpp"

namespace ecosystem_test_support {
namespace {

    json report_fixture() {
        ecosystem::diagnostic_finding hint;
        hint.rule = "style.scope";
        hint.category = "style";
        hint.artifact = "core:app";
        hint.file = "include/model.hpp";
        hint.entity_kind = "function";
        hint.entity = "BadName";
        hint.referent = "include/model";
        hint.message = "<script>alert(1)</script>\n```\n[link](bad)";
        hint.measurements = { { "lines", 12 } };
        hint.thresholds = { { "lines", 8 } };
        auto error = hint;
        error.rule = "clang.error";
        error.category = "compiler";
        error.severity = "error";
        error.authority = "external";
        error.enforcement = "required";
        return { { "project", "example" },
                 { "profile", "style" },
                 { "artifact", "core:app" },
                 { "status", "failed" },
                 { "findings",
                   { ecosystem::to_json(hint), ecosystem::to_json(error) } },
                 { "errors", { "incomplete <analysis>" } },
                 { "files",
                   { { { "file", hint.file }, { "effective_lines", 12 } } } },
                 { "scopes", json::array() },
                 { "referents", { "include/model" } },
                 { "modules", json::array() } };
    }

} // namespace

void test_diagnostic_views() {
    const auto report = report_fixture();
    auto mixed = report;
    mixed["findings"][1]["category"] = "style";
    auto warning = mixed["findings"][1];
    warning["severity"] = "warning";
    mixed["findings"].push_back(warning);
    const auto groups = ecosystem::group_diagnostics(mixed, {});
    require_true(
        groups.size() == 3 && groups[0].severity == "error"
            && groups[1].severity == "warning" && groups[2].severity == "hint",
        "each category must present errors and warnings before hints"
    );
    const auto before = report.dump();
    ecosystem::diagnostic_view view;
    view.category = "style";
    view.severity = "hint";
    std::ostringstream terminal;
    ecosystem::render_diagnostic_report(report, terminal, view);
    require_contains(
        terminal.str(), "failed, 2 finding(s)",
        "status describes the full analysis"
    );
    require_contains(
        terminal.str(), "display: 1/2",
        "filters expose displayed and total counts"
    );
    require_contains(
        terminal.str(), "style / hint: 1",
        "terminal groups categories and severities"
    );
    require_not_contains(
        terminal.str(), "clang.error", "filters hide unselected findings"
    );
    require_contains(
        terminal.str(), "analysis failure: incomplete",
        "filters retain failures"
    );
    require_contains(
        terminal.str(), "referent=include/model", "referent remains visible"
    );
    require_contains(terminal.str(), "\"lines\":12", "measurements remain raw");
    view.format = ecosystem::diagnostic_format::markdown;
    std::ostringstream markdown;
    ecosystem::render_diagnostic_report(report, markdown, view);
    require_contains(
        markdown.str(), "### style / hint (1)",
        "CI Markdown uses the same group"
    );
    require_contains(
        markdown.str(), "entity: <code>BadName", "CI shows entity details"
    );
    require_contains(
        markdown.str(), "**Analysis failure:** incomplete",
        "CI retains incomplete analysis"
    );
    require_not_contains(
        markdown.str(), "<script>", "CI escapes authored HTML"
    );
    require_not_contains(
        markdown.str(), "[link](bad)", "CI escapes authored Markdown"
    );
    require_not_contains(
        markdown.str(), "```", "diagnostics cannot close a Markdown fence"
    );
    require_contains(
        markdown.str(), "measurements: <code>",
        "CI exposes raw finding measurements"
    );
    require_contains(
        markdown.str(), "Raw scope and measurements",
        "CI retains file and scope data"
    );
    view.severity = "absent";
    std::ostringstream empty;
    ecosystem::render_diagnostic_report(report, empty, view);
    require_contains(
        empty.str(), "Displayed: 0", "empty filters stay explicit"
    );
    require_contains(
        empty.str(), "Status: <code>failed",
        "empty filters never hide failure status"
    );
    require_true(
        report.dump() == before,
        "renderers must not mutate the canonical snapshot"
    );
    const json workspace = { { "workspace_root", "/workspace" },
                             { "status", "failed" },
                             { "projects", { report, report } },
                             { "selection", { { "group", "apps" } } } };
    std::ostringstream peers;
    ecosystem::render_diagnostic_report(workspace, peers, view);
    require_contains(
        peers.str(), "Workspace", "workspace summary survives filtering"
    );
    require_contains(
        peers.str(), "Selection", "CI retains workspace scope selection"
    );
    const auto heading = peers.str().find("## example");
    require_true(
        heading != std::string::npos
            && peers.str().find("## example", heading + 1) != std::string::npos,
        "each selected workspace owner keeps a report"
    );
}

void test_report_display_scope() {
    temp_dir root;
    write_cxx_report_project(root.path(), "views");
    write_text(
        root.path() / "src/main.cpp",
        "int main() { int BadName = 0; return BadName; }\n"
    );
    auto result = run_engels_cli(root.path(), "report naming core:app --json");
    require_true(result.exit_code == 0, "advisory analysis must succeed");
    const auto path = root.path() / ".ecosystem/reports/naming.json";
    const auto original = read_text(path);
    const auto report = json::parse(original);
    const auto category
        = report.at("findings").at(0).at("category").get<std::string>();
    result = run_engels_cli(
        root.path(),
        "report naming --category " + category + " core:app --severity hint"
    );
    require_true(
        result.exit_code == 0 && read_text(path) == original,
        "display filtering must preserve the complete scoped snapshot"
    );
    require_contains(
        result.output, "severity=hint",
        "interleaved display options must be parsed"
    );
    for (const std::string options :
         { "--json --severity hint", "--markdown --json", "--category",
           "--severity --markdown", "--category naming --category style" }) {
        result
            = run_engels_cli(root.path(), "report naming core:app " + options);
        require_true(
            result.exit_code == 2 && read_text(path) == original,
            "invalid views must fail before replacing snapshots: " + options
        );
    }
    for (const std::string kind : { "matrix", "toolchains", "changes" }) {
        result = run_engels_cli(root.path(), "report " + kind + " --markdown");
        require_true(
            result.exit_code == 2,
            "display options must reject non-diagnostic reports"
        );
    }
    write_text(root.path() / ".ecosystem/github/summary.md", "");
    result = run_engels_cli(
        root.path(),
        "report naming core:app --markdown >> .ecosystem/github/summary.md"
    );
    require_true(
        result.exit_code == 0 && read_text(path) == original,
        "CI rendering uses one analysis and saves the same snapshot"
    );
    require_contains(
        read_text(root.path() / ".ecosystem/github/summary.md"),
        "## views — naming",
        "Markdown stdout can be appended directly to a CI step summary"
    );
    result = run_engels_cli(
        root.path(), "report naming core:other --markdown --severity hint"
    );
    require_true(
        result.exit_code == 5,
        "Markdown display filters must preserve parser failure exit status"
    );
    require_contains(
        result.output, "Analysis failure",
        "failed Markdown reports preserve reasons"
    );

    temp_dir workspace;
    write_cxx_report_project(workspace.path() / "alpha", "alpha");
    write_cxx_report_project(workspace.path() / "beta", "beta");
    result = run_engels_cli(
        workspace.path(),
        "report naming alpha/core:app beta/core:other --markdown --category "
        "absent"
    );
    const auto peers = json::parse(
        read_text(workspace.path() / ".ecosystem/reports/workspace_naming.json")
    );
    require_true(
        result.exit_code == 5 && peers.at("projects").size() == 2
            && peers.at("projects")[0].at("status") == "clean"
            && peers.at("projects")[1].at("status") == "failed",
        "workspace display filters retain selected owners and independent "
        "status"
    );
    require_contains(result.output, "## alpha", "CI retains successful peers");
    require_contains(result.output, "## beta", "CI retains failed peers");
    require_contains(
        result.output, "Displayed: 0", "empty matches stay explicit"
    );
    require_contains(
        result.output, "Analysis failure",
        "empty workspace views retain failures"
    );
}

} // namespace ecosystem_test_support
