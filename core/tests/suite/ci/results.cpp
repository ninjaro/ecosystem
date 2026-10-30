#include "../ci.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_ci_stage_failures() {
    const std::string action = ecosystem::render_required_text_template(
        "tracked/.github/actions/run-ecosystem-stage/action.yml.tpl", {}
    );
    const std::string marker = "      run: |\n";
    const auto start = action.find(marker);
    require_true(
        start != std::string::npos,
        "stage action must contain its shell program"
    );
    std::istringstream lines(action.substr(start + marker.size()));
    std::string program, line;
    while (std::getline(lines, line)) {
        program
            += (line.starts_with("        ") ? line.substr(8) : line) + "\n";
    }
    temp_dir project;
    const auto generated = ecosystem::generate_tracked_surface_files(
        sample_manifest(), project.path()
    );
    const auto* publisher = find_tracked_surface_file(
        generated, ".github/actions/publish-ecosystem-report/action.yml"
    );
    require_true(publisher != nullptr, "shared report publisher must exist");
    require_contains(
        publisher->contents,
        "always() && (inputs.report-key == 'checks' || inputs.report-key == "
        "'codeql')",
        "Checks/CodeQL diagnostics must not require their private upstream "
        "inputs "
        "from the existing Pages report caller"
    );
    for (const auto& filename : { "tests.yml", "codeql.yml", "html.yml" }) {
        const auto* workflow = find_tracked_surface_file(
            generated, std::string(".github/workflows/") + filename
        );
        require_true(workflow != nullptr, "workflow must be generated");
        if (std::string(filename) == "html.yml") {
            require_contains(
                workflow->contents, "on:\n  workflow_dispatch:\n",
                "presentation workflow must retain manual dispatch"
            );
            require_not_contains(
                workflow->contents,
                "  push:", "later-stage presentation must not run on pushes"
            );
            require_not_contains(
                workflow->contents, "  pull_request:",
                "later-stage presentation must not run on pull requests"
            );
            require_not_contains(
                workflow->contents, "steps.coverage.outputs.enabled",
                "coverage uploads must not depend on a nonexistent output"
            );
        } else if (std::string(filename) == "codeql.yml") {
            require_not_contains(
                workflow->contents, "paths:",
                "CodeQL must cover arbitrary manifest-owned source directories"
            );
            require_contains(
                workflow->contents,
                "uses: ./.github/actions/select-ecosystem-ci",
                "CodeQL must share event selection with required checks"
            );
        }
        if (std::string(filename) == "tests.yml") {
            require_contains(
                workflow->contents,
                "COVERAGE_STATUS: ${{ steps.coverage.outputs.status }}",
                "coverage gates and evidence must use the real stage status"
            );
        }
        if (std::string(filename) != "html.yml") {
            require_not_contains(
                workflow->contents, "check sphinx",
                "ordinary CI must not depend on later-stage presentation"
            );
            require_not_contains(
                workflow->contents, "install-docs: 'true'",
                "ordinary CI must not install presentation dependencies"
            );
        }
    }

    struct stage_case {
        std::string command;
        std::string status;
        int exit_code;
        bool broken_log = false;
    };

    for (const auto& item : std::vector<stage_case> {
             { "false\nprintf should-not-run", "failed", 1 },
             { "false | cat", "failed", 1 },
             { "printf success", "passed", 0 },
             { "exit 3", "skipped", 3 },
             { "exit 4", "failed", 4 },
             { "exit 127", "failed", 127 },
             { "printf streamed", "failed", 9, true },
             { "printf streamed", "failed", 3, true },
         }) {
        temp_dir root;
        std::string script = program;
        for (const auto& [name, value] :
             std::vector<std::pair<std::string, std::string>> {
                 { "stage-id", "audit" },
                 { "stage-label", "Audit" },
                 { "shell-command", item.command },
                 { "skipped-exit-codes", "3" },
             }) {
            const std::string token = "${{ inputs." + name + " }}";
            std::size_t offset = 0;
            while ((offset = script.find(token, offset)) != std::string::npos) {
                script.replace(offset, token.size(), value);
                offset += value.size();
            }
        }
        const fs::path command = root.path() / "stage.sh";
        const fs::path outputs = root.path() / "outputs";
        write_text(command, script);
        if (item.broken_log)
            write_executable_script(
                root.path() / "bin/tee",
                "#!/bin/sh\n/usr/bin/tee \"$@\"\nexit "
                    + std::to_string(item.exit_code) + "\n"
            );
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", command.string() }, root.path(),
            { { "PATH",
                (root.path() / "bin").string() + ":" + current_path_env() },
              { "GITHUB_OUTPUT", outputs.string() },
              { "GITHUB_STEP_SUMMARY", (root.path() / "summary").string() } }
        );
        require_true(
            result.exit_code == 0,
            "stage must keep the workflow alive to report: " + result.output
        );
        require_contains(
            read_text(outputs), "status=" + item.status + "\n",
            "stage status must reflect every failure"
        );
        require_contains(
            read_text(outputs),
            "exit-code=" + std::to_string(item.exit_code) + "\n",
            "stage must retain the child exit status"
        );
        require_not_contains(
            read_text(
                root.path() / ".ecosystem/github/reports/audit/output.log"
            ),
            "should-not-run", "stage must stop after an intermediate failure"
        );
        require_contains(
            read_text(root.path() / "summary"),
            "duration seconds:", "every stage records elapsed duration"
        );
        if (item.command.starts_with("printf"))
            require_contains(
                result.output,
                item.command == "printf success" ? "success" : "streamed",
                "child progress must reach hosted output while retaining logs"
            );
    }
}

void test_ci_result_gates() {
    temp_dir root;
    write_text(
        root.path() / ".ecosystem/github/reports/00-workflow/result.json",
        R"({"status":"passed"})"
    );
    const auto files = ecosystem::generate_tracked_surface_files(
        sample_manifest(), root.path()
    );
    const auto* workflow
        = find_tracked_surface_file(files, ".github/workflows/tests.yml");
    require_true(workflow != nullptr, "required workflow must exist");
    const auto* publisher = find_tracked_surface_file(
        files, ".github/actions/publish-ecosystem-report/action.yml"
    );
    require_true(publisher != nullptr, "report collector must exist");
    {
        temp_dir empty_reports;
        auto combine
            = ci_shell(publisher->contents, "Combine stage reports", 6);
        for (const auto& [key, replacement] :
             std::vector<std::pair<std::string, std::string>> {
                 { "report-directory",
                   (empty_reports.path() / "reports").string() },
                 { "report-key", "checks" },
                 { "project-id", "fixture" },
                 { "report-title", "Checks" } }) {
            combine = ci_bind(
                combine, std::string("inputs.").append(key), replacement
            );
        }
        write_text(empty_reports.path() / "combine.sh", combine);
        const auto output = empty_reports.path() / "outputs";
        const auto collected = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail",
              (empty_reports.path() / "combine.sh").string() },
            empty_reports.path(),
            { { "GITHUB_OUTPUT", output.string() },
              { "GITHUB_STEP_SUMMARY",
                (empty_reports.path() / "summary").string() },
              { "RUNNER_TEMP", empty_reports.path().string() },
              { "GITHUB_WORKFLOW", "Checks" },
              { "GITHUB_REF_NAME", "main" },
              { "GITHUB_REPOSITORY", "fixture/project" },
              { "GITHUB_RUN_ID", "1" } }
        );
        require_true(collected.exit_code == 0, collected.output);
        require_contains(
            read_text(output), "has-failures=true",
            "missing stage reports must never close a failure issue or claim "
            "success"
        );
    }
    require_not_contains(
        workflow->contents, "paths:",
        "required checks must cover arbitrary manifest-owned source directories"
    );
    require_contains(
        workflow->contents,
        "- name: Upload check reports\n        if: ${{ always() }}",
        "fresh attempts must preserve diagnostics without replacing reused "
        "reports"
    );
    for (const auto* name :
         { "Publish check report", "Enforce required result" }) {
        require_contains(
            workflow->contents,
            std::string("- name: ") + name
                + (std::string(name) == "Publish check report"
                       ? "\n        id: published"
                       : "")
                + "\n        if: ${{ always() }}",
            "reporting and enforcement must also run after setup or stage "
            "failures"
        );
    }
    const auto gate = root.path() / "gate.sh";
    write_text(
        gate, ci_shell(workflow->contents, "Enforce required result", 8)
    );
    const auto* action = find_tracked_surface_file(
        files, ".github/actions/run-ecosystem-stage/action.yml"
    );
    require_true(action != nullptr, "stage runner must exist");
    const auto program = ci_shell(action->contents, "Run stage command", 6);
    const auto report
        = root.path() / ".ecosystem/github/reports/01-required-checks";
    const auto outputs = root.path() / "outputs";
    const auto stage = root.path() / "stage.sh";
    const auto run_gate = [&](const std::string& outcome,
                              const std::string& status,
                              const std::string& code) {
        return ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", gate.string() }, root.path(),
            { { "DIAGNOSTIC_OUTCOME", "success" },
              { "DIAGNOSTIC_STATUS", "passed" },
              { "CHECK_SCOPE", "cheap" },
              { "CHECK_OUTCOME", outcome },
              { "CHECK_STATUS", status },
              { "CHECK_EXIT_CODE", code } }
        );
    };
    require_true(
        run_gate("skipped", "", "").exit_code != 0,
        "failed bootstrap cannot yield a successful required result"
    );
    for (const auto& [command, code] :
         std::vector<std::pair<std::string, int>> {
             { "printf 'beauty finding: advisory\\n'", 0 },
             { "false\nprintf unreachable", 1 },
             { "false | cat", 1 },
             { "exit 3", 3 },
             { "exit 5", 5 },
             { "exit 127", 127 } }) {
        auto script = program;
        for (const auto& [name, value] :
             std::vector<std::pair<std::string, std::string>> {
                 { "stage-id", "01-required-checks" },
                 { "stage-label", "Required local checks" },
                 { "shell-command", command },
                 { "skipped-exit-codes", "" } }) {
            script
                = ci_bind(script, std::string("inputs.").append(name), value);
        }
        write_text(stage, script);
        write_text(outputs, "");
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", stage.string() }, root.path(),
            { { "GITHUB_OUTPUT", outputs.string() },
              { "GITHUB_STEP_SUMMARY", (root.path() / "summary").string() } }
        );
        require_true(
            result.exit_code == 0 && fs::exists(report / "summary.md"),
            "every completed stage must preserve its report before enforcement"
        );
        const auto status = code == 0 ? "passed" : "failed";
        require_contains(
            read_text(outputs), "exit-code=" + std::to_string(code) + "\n",
            "stage must expose its actual process exit code"
        );
        require_true(
            (run_gate("success", status, std::to_string(code)).exit_code == 0)
                == (code == 0),
            "required CI outcome must follow the local exit code, including "
            "unsupported and missing tools"
        );
    }
    require_true(
        run_gate("failure", "passed", "0").exit_code != 0,
        "partial outputs from an incomplete reporting step cannot pass"
    );
    require_true(
        run_gate("success", "skipped", "3").exit_code != 0,
        "a required result cannot be skipped"
    );
    fs::remove(report / "output.log");
    require_true(
        run_gate("success", "passed", "0").exit_code != 0,
        "a missing required log must fail closed"
    );
    write_text(report / "output.log", "");
    write_text(report / "summary.md", "");
    require_true(
        run_gate("success", "passed", "0").exit_code != 0,
        "an empty required summary must fail closed"
    );
    fs::remove(report / "summary.md");
    require_true(
        run_gate("success", "passed", "0").exit_code != 0,
        "a missing required summary must fail closed"
    );

    const auto* codeql
        = find_tracked_surface_file(files, ".github/workflows/codeql.yml");
    require_true(codeql != nullptr, "CodeQL adapter must exist");
    write_text(gate, ci_shell(codeql->contents, "Enforce CodeQL result", 8));
    const std::vector<std::pair<std::string, std::string>> clean {
        { "DIAGNOSTIC_OUTCOME", "success" },
        { "DIAGNOSTIC_STATUS", "passed" },
        { "CHECK_SCOPE", "full" },
        { "CODEQL_SCOPE", "full" },
        { "REPOSITORY_OUTCOME", "success" },
        { "REPOSITORY_EXIT_CODE", "0" },
        { "REPOSITORY_STATUS", "passed" },
        { "CODEQL_INIT_STATUS", "success" },
        { "CODEQL_BUILD_STATUS", "passed" },
        { "CODEQL_BUILD_OUTCOME", "success" },
        { "CODEQL_BUILD_EXIT_CODE", "0" },
        { "CODEQL_ANALYZE_STATUS", "success" }
    };
    for (const auto* stage_name :
         { "01-tracked-surface", "02-codeql-build", "03-codeql-analysis" }) {
        write_text(
            root.path() / ".ecosystem/github/reports" / stage_name
                / "summary.md",
            "simulated complete stage"
        );
        write_text(
            root.path() / ".ecosystem/github/reports" / stage_name
                / "output.log",
            "simulated action log"
        );
    }
    require_true(
        ecosystem::capture_command_result(
            { "bash", gate.string() }, root.path(), clean
        )
                .exit_code
            == 0,
        "completed CodeQL operations must pass"
    );
    for (const auto& relative :
         { "01-tracked-surface/summary.md", "01-tracked-surface/output.log",
           "02-codeql-build/summary.md", "02-codeql-build/output.log",
           "03-codeql-analysis/summary.md" }) {
        const auto missing
            = root.path() / ".ecosystem/github/reports" / relative;
        const auto saved = read_text(missing);
        fs::remove(missing);
        require_true(
            ecosystem::capture_command_result(
                { "bash", gate.string() }, root.path(), clean
            )
                    .exit_code
                != 0,
            "successful CodeQL outcomes cannot replace a missing stage "
            "report/log"
        );
        write_text(missing, saved);
    }
    for (std::size_t i = 0; i < clean.size(); ++i) {
        for (const auto* state :
             { "", "failed", "failure", "skipped", "cancelled" }) {
            auto env = clean;
            env[i].second = state;
            require_true(
                ecosystem::capture_command_result(
                    { "bash", gate.string() }, root.path(), env
                )
                        .exit_code
                    != 0,
                "every incomplete CodeQL operation must fail its workflow"
            );
        }
    }

    for (const auto* consumer : { workflow, codeql }) {
        require_contains(
            consumer->contents,
            "- name: Setup ecosystem tool\n        id: ecosystem",
            "generated verification establishes its own environment before "
            "execution"
        );
        const auto name = consumer == workflow ? "Enforce required result"
                                               : "Enforce CodeQL result";
        write_text(gate, ci_shell(consumer->contents, name, 8));
        for (const auto* verified : { "true", "false", "" }) {
            const auto reused = ecosystem::capture_command_result(
                { "bash", "-e", "-o", "pipefail", gate.string() }, root.path(),
                { { "CHECK_SCOPE", "reuse" },
                  { "REUSE_VERIFIED", verified },
                  { "REUSE_SOURCE", R"({"checks":101,"codeql":202})" },
                  { "GITHUB_STEP_SUMMARY",
                    (root.path() / "summary").string() } }
            );
            require_true(
                reused.exit_code != 0,
                "preliminary metadata cannot replace native workflow results"
            );
        }
        const auto missing = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", gate.string() }, root.path(),
            { { "CHECK_SCOPE", "reuse" },
              { "REUSE_VERIFIED", "true" },
              { "REUSE_SOURCE", "" } }
        );
        require_true(
            missing.exit_code != 0, "reuse must retain source provenance"
        );
    }
}

} // namespace ecosystem_test_support
