#include "../test_cases.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_cli_report_cxx_preserves_artifact_scope_and_failure_status() {
    scoped_env progress("MANIFESTO_CI_PROGRESS", "1");
    temp_dir root;
    scoped_env flags("CXXFLAGS", "-Wno-error=unused-variable");
    write_cxx_report_project(root.path(), "reports");
    const auto report_path = root.path() / ".ecosystem/reports/cxx.json";
    auto result = run_engels_cli(root.path(), "report cxx core:app --json");
    require_true(
        result.exit_code == 0,
        "scoped C++ report must succeed:\n" + result.output
    );
    auto report = json::parse(read_text(report_path));
    require_true(
        json::parse(result.output) == report,
        "fresh CMake output must not contaminate machine-readable stdout"
    );
    require_true(
        report.at("status") == "clean" && report.at("files_analyzed") == 1
            && report.at("artifact") == "core:app"
            && report.at("sources").at(0).at("artifact") == "core:app",
        "report and source must retain full artifact identity without sibling "
        "sources"
    );
    require_not_contains(
        result.output, "other.cpp", "terminal must not widen artifact scope"
    );
    const auto before = read_text(report_path);
    for (const std::string command :
         { "report cxx core:absent", "report matrix core:absent",
           "report toolchains core:app" }) {
        result = run_engels_cli(root.path(), command);
        require_true(
            result.exit_code == 2 && read_text(report_path) == before,
            "invalid report requests must fail before replacing results: "
                + command
        );
    }
    result = run_engels_cli(root.path(), "report cxx core:other");
    report = json::parse(read_text(report_path));
    require_true(
        result.exit_code == 5 && report.at("status") == "failed"
            && !report.at("errors").empty()
            && report.at("total_errors").get<int>() > 0,
        "source parse errors must persist an incomplete report and fail the "
        "operation"
    );
    require_true(
        !report.at("findings").empty(),
        "compiler diagnostics must use structured findings"
    );
    const auto& finding = report.at("findings").at(0);
    require_true(
        finding.at("authority") == "external"
            && finding.at("enforcement") == "required"
            && finding.at("severity") == "error"
            && finding.at("file") == "src/other.cpp" && finding.at("line") == 1
            && finding.at("artifact") == "core:other"
            && finding.at("entity_kind") == "translation_unit",
        "compiler findings must preserve authority, rule context, severity, "
        "location and owner"
    );
    require_contains(
        result.output, finding.at("rule").get<std::string>(),
        "terminal and JSON must identify the same compiler diagnostic"
    );
    require_contains(
        result.output, "unselected artifact",
        "failure must preserve Clang's reason"
    );

    write_text(
        root.path() / "src/main.cpp",
        "int main() { int unused = 0; return 0; }\n"
    );
    result = run_engels_cli(root.path(), "report cxx core:app --json");
    report = json::parse(read_text(report_path));
    require_true(
        result.exit_code == 0 && report.at("status") == "findings"
            && report.at("total_warnings").get<int>() > 0
            && report.at("errors").empty(),
        "successful analysis with warnings must remain distinct from failed "
        "analysis:\n"
            + result.output
    );
    temp_dir failed;
    write_cxx_report_project(failed.path(), "failed_configure");
    write_text(
        failed.path() / ".ecosystem/reports/cxx.json",
        "{\"status\":\"clean\"}\n"
    );
    scoped_env invalid_flags("CXXFLAGS", "-fmanifesto-invalid-compiler-option");
    result = run_engels_cli(failed.path(), "report cxx core:app --json");
    report
        = json::parse(read_text(failed.path() / ".ecosystem/reports/cxx.json"));
    require_true(
        result.exit_code != 0 && report.at("status") == "failed"
            && report.at("artifact") == "core:app"
            && report.at("files_analyzed") == 0,
        "configure failures must replace stale success with a failed scoped "
        "report"
    );
    require_true(
        json::parse(result.output) == report,
        "failed configuration must also emit one complete JSON object"
    );
    require_contains(
        report.at("errors").dump(), "-fmanifesto-invalid-compiler-option",
        "report must retain captured compiler configuration diagnostics"
    );
    require_contains(
        report.at("errors").dump(), "configure failed",
        "failed report must retain the configure failure reason"
    );
}

void test_cli_workspace_report_cxx_preserves_each_selected_owner() {
    temp_dir root;
    write_cxx_report_project(root.path() / "alpha", "alpha");
    write_cxx_report_project(root.path() / "beta", "beta");
    write_text(root.path() / "broken/manifest.json", "invalid JSON\n");
    const auto report_path
        = root.path() / ".ecosystem/reports/workspace_cxx.json";
    auto result = run_engels_cli(root.path(), "report cxx");
    require_true(
        result.exit_code == 2 && !fs::exists(report_path),
        "analysis must preflight selected workspace validity before writing "
        "reports"
    );
    result = run_engels_cli(
        root.path(), "report cxx alpha/core:app beta/core:app --json"
    );
    require_true(
        result.exit_code == 0,
        "qualified workspace C++ reports must succeed:\n" + result.output
    );
    auto report = json::parse(read_text(report_path));
    require_true(
        json::parse(result.output) == report,
        "workspace stdout must equal the complete persisted JSON result"
    );
    require_true(
        report.at("status") == "clean" && report.at("projects").size() == 2,
        "workspace must retain each selected analysis"
    );
    for (const auto& project : report.at("projects"))
        require_true(
            project.at("artifact") == "core:app"
                && project.at("files_analyzed") == 1
                && project.at("sources").at(0).at("artifact") == "core:app",
            "each project result must select the full owner, including shared "
            "namespaces"
        );
    result = run_engels_cli(
        root.path(), "report cxx alpha/core:app beta/core:other"
    );
    report = json::parse(read_text(report_path));
    require_true(
        result.exit_code == 5 && report.at("status") == "failed"
            && report.at("projects").at(0).at("status") == "clean"
            && report.at("projects").at(1).at("status") == "failed",
        "workspace failures must retain successful peers and per-project status"
    );
}

} // namespace ecosystem_test_support
