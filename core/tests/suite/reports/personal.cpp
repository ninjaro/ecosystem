#include "../test_cases.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_cli_personal_reports_share_findings_and_presentation() {
    temp_dir root;
    write_cxx_report_project(root.path(), "reports");
    auto manifest = json::parse(read_text(root.path() / "manifest.json"));
    manifest["artifacts"][0]["owns"] = json::array({ "include/model.hpp" });
    write_text(root.path() / "manifest.json", manifest.dump(2));
    write_text(
        root.path() / "include/model.hpp",
        "#pragma once\ninline int BadName(int value) { if (value) return 1; "
        "return 0; }\n"
    );
    write_text(
        root.path() / "src/main.cpp",
        "#include \"model.hpp\"\nint main() { return BadName(0); }\n"
    );
    for (const std::string profile : { "naming", "style" }) {
        auto result
            = run_engels_cli(root.path(), "check " + profile + " core:app");
        require_true(
            result.exit_code == 0,
            "personal checks must accept advisory findings"
        );
        const auto path
            = root.path() / (".ecosystem/reports/" + profile + ".json");
        const auto checked = json::parse(read_text(path));
        result = run_engels_cli(
            root.path(), "report " + profile + " core:app --json"
        );
        require_true(
            result.exit_code == 0 && json::parse(result.output) == checked
                && json::parse(read_text(path)) == checked
                && checked.at("status") == "findings",
            "check, report, persisted JSON and stdout must use the same result"
        );
        const auto terminal
            = run_engels_cli(root.path(), "report " + profile + " core:app");
        require_true(
            terminal.exit_code == 0,
            "terminal reporting must accept advisory findings"
        );
        for (const auto& finding : checked.at("findings")) {
            require_true(
                finding.at("authority") == "manifesto"
                    && finding.at("enforcement") == "advisory"
                    && finding.at("severity") == "hint"
                    && !finding.at("category").get<std::string>().empty(),
                "personal diagnostics must expose their policy and severity"
            );
            require_contains(
                terminal.output, finding.at("rule").get<std::string>(),
                "terminal must preserve the rule"
            );
            require_contains(
                terminal.output, finding.at("message").get<std::string>(),
                "terminal must preserve the explanation"
            );
            require_contains(
                terminal.output,
                "artifact=" + finding.at("artifact").get<std::string>(),
                "terminal must preserve full ownership"
            );
            const auto location = finding.at("file").get<std::string>() + ":"
                + std::to_string(finding.at("line").get<unsigned>()) + ":"
                + std::to_string(finding.at("column").get<unsigned>());
            require_contains(
                terminal.output, location,
                "terminal must preserve the source location"
            );
            if (!finding.at("measurements").empty())
                require_contains(
                    terminal.output, finding.at("measurements").dump(),
                    "terminal must preserve measurements"
                );
            if (!finding.at("thresholds").empty())
                require_contains(
                    terminal.output, finding.at("thresholds").dump(),
                    "terminal must preserve thresholds"
                );
            if (profile == "style") {
                require_true(
                    !finding.at("referent").is_null(),
                    "header diagnostics must retain their indexed referent"
                );
                require_contains(
                    terminal.output,
                    "referent=" + finding.at("referent").get<std::string>(),
                    "terminal must identify the referent"
                );
            }
        }
        require_not_contains(
            terminal.output, "other.cpp", "reports must not widen ownership"
        );
        const auto before = read_text(path);
        for (const std::string suffix :
             { "core:absent --json", "core:app --json --json",
               "core:app --unsupported" }) {
            result = run_engels_cli(
                root.path(),
                std::string("report ").append(profile).append(" ").append(suffix
                )
            );
            require_true(
                result.exit_code == 2 && read_text(path) == before,
                "invalid report requests must preserve the last result"
            );
        }
    }
    auto result
        = run_engels_cli(root.path(), "report naming core:other --json");
    const auto failed = json::parse(result.output);
    require_true(
        result.exit_code == 5 && failed.at("status") == "failed"
            && !failed.at("errors").empty(),
        "personal parser failures must retain a machine-readable result and "
        "fail"
    );
    const auto path = root.path() / ".ecosystem/reports/style.json";
    fs::remove(path);
    fs::create_directory(path);
    result = run_engels_cli(root.path(), "report style core:app --json");
    require_true(
        result.exit_code == 5,
        "unwritable report destinations must fail the operation"
    );
    result = run_engels_cli(root.path(), "report matrix core:app --json");
    require_true(
        result.exit_code == 0
            && json::parse(result.output).at("artifacts").size() == 1,
        "matrix must retain JSON inspection with explicit output selection"
    );
}

void test_cli_workspace_personal_reports_keep_all_selected_results() {
    temp_dir root;
    write_cxx_report_project(root.path() / "alpha", "alpha");
    write_cxx_report_project(root.path() / "beta", "beta");
    write_text(root.path() / "broken/manifest.json", "invalid JSON\n");
    write_text(
        root.path() / "alpha/src/main.cpp",
        "int main() { int BadName = 0; if (BadName) ++BadName; return BadName; "
        "}\n"
    );
    write_text(
        root.path() / "alpha/src/other.cpp", "int main() { return 0; }\n"
    );
    write_text(
        root.path() / "ecosystem.workspace.json",
        "{\"groups\":{\"apps\":[\"alpha\",\"beta\"]}}\n"
    );
    for (const std::string kind : { "naming", "style" }) {
        const auto path
            = root.path() / (".ecosystem/reports/workspace_" + kind + ".json");
        auto result = run_engels_cli(root.path(), "report " + kind + " --json");
        require_true(
            result.exit_code == 2 && !fs::exists(path),
            "invalid workspace selection must fail before reporting"
        );
        result = run_engels_cli(
            root.path(),
            "report " + kind
                + " alpha/core:app alpha/core:other beta/core:app --json"
        );
        const auto report = json::parse(result.output);
        require_true(
            result.exit_code == 0 && report == json::parse(read_text(path))
                && report.at("status") == "findings"
                && report.at("projects").size() == 3,
            "workspace reports must retain every artifact request, including "
            "two owners in one project"
        );
        require_true(
            report.at("projects").at(0).at("artifact") == "core:app"
                && report.at("projects").at(1).at("artifact") == "core:other"
                && report.at("projects").at(1).at("status") == "clean",
            "same-project requests must not overwrite each other"
        );
        result = run_engels_cli(
            root.path(), "report " + kind + " --group apps --json"
        );
        const auto failed = json::parse(result.output);
        require_true(
            result.exit_code == 5 && failed.at("status") == "failed"
                && failed.at("projects").at(0).at("status") == "findings"
                && failed.at("projects").at(1).at("status") == "failed",
            "workspace group failures must retain findings and failures "
            "separately"
        );
        const auto terminal
            = run_engels_cli(root.path(), "report " + kind + " alpha/core:app");
        require_contains(
            terminal.output, "alpha (alpha) [core:app]",
            "terminal must qualify project, root and artifact"
        );
        require_contains(
            terminal.output, "workspace_" + kind + ".json",
            "terminal must locate the complete workspace report"
        );
    }
}

} // namespace ecosystem_test_support
