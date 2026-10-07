#include "../ci.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_ci_stage_failures() {
    const std::string action = ecosystem::render_required_text_template(
        "tracked/.github/actions/run-manifesto-stage/action.yml.tpl", {}
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
            require_contains(
                workflow->contents, "steps.coverage.outputs.status == 'passed'",
                "coverage uploads must use the status output provided by the "
                "stage action"
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
                "uses: ./.github/actions/select-manifesto-ci",
                "CodeQL must share event selection with required checks"
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
    };

    for (const auto& item : std::vector<stage_case> {
             { "false\nprintf should-not-run", "failed", 1 },
             { "false | cat", "failed", 1 },
             { "printf success", "passed", 0 },
             { "exit 3", "skipped", 3 },
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
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", command.string() }, root.path(),
            { { "GITHUB_OUTPUT", outputs.string() },
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
    }
}

void test_ci_result_gates() {
    temp_dir root;
    const auto files = ecosystem::generate_tracked_surface_files(
        sample_manifest(), root.path()
    );
    const auto* workflow
        = find_tracked_surface_file(files, ".github/workflows/tests.yml");
    require_true(workflow != nullptr, "required workflow must exist");
    require_not_contains(
        workflow->contents, "paths:",
        "required checks must cover arbitrary manifest-owned source directories"
    );
    require_contains(
        workflow->contents,
        "- name: Upload check reports\n        if: ${{ always() && "
        "needs.select.outputs.scope != 'reuse' }}",
        "fresh attempts must preserve diagnostics without replacing reused "
        "reports"
    );
    for (const auto* name :
         { "Publish check report", "Enforce required result" }) {
        require_contains(
            workflow->contents,
            std::string("- name: ") + name + "\n        if: ${{ always() }}",
            "reporting and enforcement must also run after setup or stage "
            "failures"
        );
    }
    const auto gate = root.path() / "gate.sh";
    write_text(
        gate, ci_shell(workflow->contents, "Enforce required result", 8)
    );
    const auto* action = find_tracked_surface_file(
        files, ".github/actions/run-manifesto-stage/action.yml"
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
            { { "CHECK_SCOPE", "cheap" },
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
            script = ci_bind(script, "inputs." + name, value);
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
        { "CODEQL_SCOPE", "full" },
        { "REPOSITORY_OUTCOME", "success" },
        { "REPOSITORY_EXIT_CODE", "0" },
        { "REPOSITORY_STATUS", "passed" },
        { "CODEQL_INIT_STATUS", "success" },
        { "CODEQL_BUILD_STATUS", "passed" },
        { "CODEQL_ANALYZE_STATUS", "success" }
    };
    require_true(
        ecosystem::capture_command_result(
            { "bash", gate.string() }, root.path(), clean
        )
                .exit_code
            == 0,
        "completed CodeQL operations must pass"
    );
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
            "- name: Setup manifesto tool\n        if: ${{ "
            "needs.select.outputs.scope != 'reuse' }}",
            "validated reuse must skip tooling bootstrap and native operations"
        );
        const auto name = consumer == workflow ? "Enforce required result"
                                               : "Enforce CodeQL result";
        write_text(gate, ci_shell(consumer->contents, name, 8));
        for (const auto* verified : { "true", "false", "" }) {
            const auto reused = ecosystem::capture_command_result(
                { "bash", "-e", "-o", "pipefail", gate.string() }, root.path(),
                { { "CHECK_SCOPE", "reuse" },
                  { "REUSE_VERIFIED", verified },
                  { "REUSE_SOURCE", "{\"checks\":101,\"codeql\":202}" },
                  { "GITHUB_STEP_SUMMARY",
                    (root.path() / "summary").string() } }
            );
            require_true(
                (reused.exit_code == 0) == (std::string(verified) == "true"),
                "only the selector's validated pair may replace native results"
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
