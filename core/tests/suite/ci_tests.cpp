#include "test_cases.hpp"
#include "test_support.hpp"

namespace ecosystem_test_support {

void test_ci_verification_evidence_contract() {
    temp_dir root;
    const auto files = ecosystem::generate_tracked_surface_files(
        sample_manifest(), root.path()
    );
    for (const auto& [adapter, fixture] :
         std::vector<std::pair<std::string, std::string>> {
             { "changes.cjs", "ci_change_cases.cjs" },
             { "changes.cjs", "ci_change_policy_cases.cjs" },
             { "changes.cjs", "ci_changed_format_cases.cjs" },
             { "evidence.cjs", "ci_evidence_cases.cjs" },
             { "evidence.cjs", "ci_publication_cases.cjs" } }) {
        const auto* script = find_tracked_surface_file(
            files, ".github/actions/select-manifesto-ci/" + adapter
        );
        require_true(script != nullptr, "CI adapter must be generated");
        const auto destination = root.path() / adapter;
        write_text(destination, script->contents);
        const auto result = ecosystem::capture_command_result(
            { "node",
              (fs::path(ECOS_TEST_SOURCE_DIR) / "core/tests/suite" / fixture)
                  .string(),
              destination.string(), engels_binary_path().string() },
            root.path()
        );
        require_true(
            result.exit_code == 0,
            "GitHub transport regressions require Node and must pass:\n"
                + result.output
        );
    }
}

void test_ci_event_selection_and_full_verification() {
    temp_dir root;
    const auto files = ecosystem::generate_tracked_surface_files(
        sample_manifest(), root.path()
    );
    const auto* action = find_tracked_surface_file(
        files, ".github/actions/select-manifesto-ci/action.yml"
    );
    const auto* checks
        = find_tracked_surface_file(files, ".github/workflows/tests.yml");
    const auto* codeql
        = find_tracked_surface_file(files, ".github/workflows/codeql.yml");
    require_true(
        action && checks && codeql, "all CI selection consumers exist"
    );
    for (const auto* workflow : { checks, codeql }) {
        require_contains(
            workflow->contents, "fetch-depth: 0\n      - id: selection",
            "change resolution requires history before selection"
        );
        require_contains(
            workflow->contents,
            "changes: ${{ steps.selection.outputs.changes }}",
            "later jobs must receive the same structured event delta"
        );
        require_contains(
            workflow->contents,
            "path: ${{ steps.selection.outputs.changes-file }}",
            "change resolution must remain inspectable as an artifact"
        );
    }
    require_contains(
        action->contents,
        "require(process.env.CHANGES_MODULE).collect({context, core});",
        "selection must collect event changes without duplicating local policy"
    );
    const auto selection = root.path() / "select.sh";
    const auto outputs = root.path() / "outputs";
    write_text(
        selection,
        github_shell_program(action->contents, "Select verification scope", 6)
    );
    for (const auto& [event, ref, branch, base, expected] :
         std::vector<std::tuple<
             std::string, std::string, std::string, std::string, std::string>> {
             { "push", "refs/heads/main", "main", "", "full" },
             { "push", "refs/heads/trunk", "trunk", "", "full" },
             { "push", "refs/heads/feature", "main", "", "cheap" },
             { "push", "refs/tags/main", "main", "", "skip" },
             { "pull_request", "refs/pull/42/merge", "main", "main", "full" },
             { "pull_request", "refs/pull/42/merge", "main", "release",
               "cheap" },
             { "workflow_dispatch", "refs/heads/feature", "main", "", "full" },
             { "schedule", "refs/heads/main", "main", "", "security" },
             { "release", "refs/tags/v1", "main", "", "skip" },
             { "push", "refs/heads/main", "", "", "error" },
             { "pull_request", "refs/pull/42/merge", "main", "", "error" } }) {
        write_text(outputs, "");
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", selection.string() }, root.path(),
            { { "EVENT_NAME", event },
              { "CURRENT_REF", ref },
              { "DEFAULT_BRANCH", branch },
              { "PR_BASE_REF", base },
              { "GITHUB_OUTPUT", outputs.string() },
              { "GITHUB_STEP_SUMMARY", (root.path() / "summary").string() } }
        );
        require_true(
            (result.exit_code == 0) == (expected != "error"),
            "event selection must fail when required context is missing"
        );
        require_true(
            read_text(outputs)
                == (expected == "error" ? "" : "scope=" + expected + "\n"),
            "event selection mismatch: " + event + " " + ref
        );
    }
    require_contains(
        codeql->contents, "  schedule:\n    - cron:",
        "CodeQL must maintain an independent scheduled default-branch baseline"
    );
    require_contains(
        codeql->contents,
        "group: codeql-${{ github.workflow }}-${{ github.event_name }}-",
        "scheduled CodeQL must not cancel or be cancelled by push verification"
    );
    require_contains(
        codeql->contents,
        "if: ${{ needs.select.outputs.scope == 'full' || "
        "needs.select.outputs.scope == 'security' || "
        "needs.select.outputs.scope == 'reuse' }}",
        "cheap branches and tags must not run CodeQL setup or analysis"
    );
    require_contains(
        checks->contents,
        "if: ${{ needs.select.outputs.scope == 'cheap' || "
        "needs.select.outputs.scope == 'full' || needs.select.outputs.scope == "
        "'reuse' }}",
        "required checks must skip package/release events"
    );
    require_contains(
        checks->contents,
        "if: ${{ needs.select.outputs.scope == 'full' && "
        "steps.required.outputs.status == 'passed' }}",
        "coverage must require a successful full verification stage"
    );
    require_contains(
        checks->contents, "permissions:\n  contents: read\n\n",
        "project checks must have read-only repository credentials"
    );

    require_contains(
        checks->contents,
        "install-project-deps: ${{ needs.select.outputs.scope == 'full' }}",
        "cheap verification must not install project build/test dependencies"
    );
    const auto* setup = find_tracked_surface_file(
        files, ".github/actions/setup-manifesto/action.yml"
    );
    require_true(setup != nullptr, "local dependency setup must exist");
    require_contains(
        setup->contents,
        "- name: Install Qt 6\n      if: ${{ inputs.install-project-deps == "
        "'true' }}",
        "cheap verification must skip Qt installation"
    );
    const auto native_setup = github_shell_program(
        setup->contents, "Install shared native dependencies", 6
    );
    const auto bin = root.path() / "bin";
    write_executable_script(bin / "sudo", "#!/bin/sh\nprintf '%s\\n' \"$*\"\n");
    const auto dependencies = root.path() / "dependencies.sh";
    for (const auto& [project_deps, docs] :
         std::vector<std::pair<std::string, std::string>> {
             { "false", "false" }, { "true", "false" }, { "false", "true" } }) {
        auto script = github_substitute(
            native_setup, "inputs.install-project-deps", project_deps
        );
        script = github_substitute(script, "inputs.install-docs", docs);
        write_text(dependencies, script);
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", dependencies.string() },
            root.path(), { { "PATH", bin.string() + ":" + current_path_env() } }
        );
        require_true(
            result.exit_code == 0, "dependency selection must execute"
        );
        for (const auto* package :
             { "clang-format", "libclang-dev", "llvm-dev", "libcxxopts-dev" })
            require_contains(
                result.output, package,
                "tool bootstrap prerequisites remain required"
            );
        for (const auto* package :
             { "clang-tidy", "default-jdk", "libopencv-dev", "libgtest-dev",
               "libbenchmark-dev" })
            require_true(
                (result.output.find(package) != std::string::npos)
                    == (project_deps == "true"),
                "project dependencies must follow the selected scope"
            );
        require_true(
            (result.output.find("doxygen graphviz") != std::string::npos)
                == (project_deps == "true" || docs == "true"),
            "native docs dependencies support full tests or explicit "
            "documentation"
        );
        require_true(
            (result.output.find("python3-venv") != std::string::npos)
                == (docs == "true"),
            "Sphinx dependencies remain explicit"
        );
    }

    // Execute the exact generated local-operation selection with recording
    // actors.
    for (const auto* actor : { "engels", "marx" })
        write_executable_script(
            root.path() / actor,
            "#!/bin/sh\nprintf '" + std::string(actor)
                + " %s\\n' \"$*\" >> \"$CI_CALLS\"\n"
                  "if [ \"$CI_FAIL_COMMAND\" = \""
                + actor + " $*\" ]; then exit 5; fi\n"
        );
    auto content = checks->contents;
    const auto command_key = content.find("shell-command: |");
    require_true(command_key != std::string::npos, "required operation exists");
    content.replace(command_key, std::string("shell-command").size(), "run");
    auto program
        = github_shell_program(content, "Run required local checks", 10);
    for (const auto* actor : { "engels", "marx" })
        program = github_substitute(
            program,
            "steps.manifesto.outputs." + std::string(actor) + "-binary",
            (root.path() / actor).string()
        );
    const auto command = root.path() / "checks.sh";
    const auto calls = root.path() / "calls";
    write_text(command, program);
    for (const auto& [scope, failure, expected] :
         std::vector<std::tuple<std::string, std::string, std::string>> {
             { "cheap", "", "engels check repo\nengels check format\n" },
             { "full", "", "marx build debug\nengels check ci\n" },
             { "cheap", "engels check repo", "engels check repo\n" },
             { "cheap", "engels check format",
               "engels check repo\nengels check format\n" },
             { "full", "marx build debug", "marx build debug\n" },
             { "full", "engels check ci",
               "marx build debug\nengels check ci\n" },
             { "missing", "invalid scope", "" } }) {
        write_text(calls, "");
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", command.string() }, root.path(),
            { { "CHECK_SCOPE", scope },
              { "CI_FAIL_COMMAND", failure },
              { "CI_CALLS", calls.string() } }
        );
        require_true(
            (result.exit_code == 0) == failure.empty()
                && read_text(calls) == expected,
            "selected operations must preserve order and failure propagation:\n"
                + result.output
        );
    }

    const auto gate = root.path() / "gate.sh";
    write_text(
        gate,
        github_shell_program(checks->contents, "Enforce required result", 8)
    );
    const auto reports = root.path() / ".ecosystem/github/reports";
    for (const auto* stage : { "01-required-checks", "02-coverage-check" }) {
        write_text(reports / stage / "summary.md", "current result");
        write_text(reports / stage / "output.log", "");
    }
    const auto report = root.path() / ".ecosystem/reports/coverage.json";
    write_text(report, "current coverage");
    const auto run_gate = [&](const std::string& outcome,
                              const std::string& status,
                              const std::string& code) {
        return ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", gate.string() }, root.path(),
            { { "CHECK_SCOPE", "full" },
              { "CHECK_OUTCOME", "success" },
              { "CHECK_STATUS", "passed" },
              { "CHECK_EXIT_CODE", "0" },
              { "COVERAGE_OUTCOME", outcome },
              { "COVERAGE_STATUS", status },
              { "COVERAGE_EXIT_CODE", code } }
        );
    };
    require_true(
        run_gate("success", "passed", "0").exit_code == 0,
        "full coverage may pass"
    );
    for (const auto& [outcome, status, code] :
         std::vector<std::tuple<std::string, std::string, std::string>> {
             { "", "", "" },
             { "failure", "passed", "0" },
             { "skipped", "skipped", "3" },
             { "success", "failed", "5" },
             { "success", "skipped", "4" },
             { "success", "passed", "3" },
             { "success", "failed", "0" } })
        require_true(
            run_gate(outcome, status, code).exit_code != 0,
            "full verification cannot hide coverage failure"
        );
    fs::remove(report);
    require_true(
        run_gate("success", "passed", "0").exit_code != 0,
        "successful coverage must have its report"
    );
    require_true(
        run_gate("success", "skipped", "3").exit_code == 0,
        "unsupported coverage must be explicit"
    );
    fs::remove(reports / "02-coverage-check/output.log");
    require_true(
        run_gate("success", "skipped", "3").exit_code != 0,
        "even unsupported coverage needs a completed stage record"
    );
}

void test_ci_stage_reports_intermediate_and_pipeline_failures() {
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

void test_ci_required_result_enforces_failures_and_missing_evidence() {
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
        gate,
        github_shell_program(workflow->contents, "Enforce required result", 8)
    );
    const auto* action = find_tracked_surface_file(
        files, ".github/actions/run-manifesto-stage/action.yml"
    );
    require_true(action != nullptr, "stage runner must exist");
    const auto program
        = github_shell_program(action->contents, "Run stage command", 6);
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
            script = github_substitute(script, "inputs." + name, value);
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
    write_text(
        gate, github_shell_program(codeql->contents, "Enforce CodeQL result", 8)
    );
    const std::vector<std::pair<std::string, std::string>> clean {
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
        write_text(gate, github_shell_program(consumer->contents, name, 8));
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

void test_ci_pages_requires_complete_current_results() {
    temp_dir root;
    const auto files = ecosystem::generate_tracked_surface_files(
        sample_manifest(), root.path()
    );
    const auto* workflow
        = find_tracked_surface_file(files, ".github/workflows/html.yml");
    require_true(workflow != nullptr, "Pages workflow must be generated");
    const auto& content = workflow->contents;
    require_contains(
        content, "permissions:\n  contents: read\n\n",
        "Pages build and report jobs must have read-only credentials"
    );
    require_contains(
        content,
        "  deploy:\n    permissions:\n      pages: write\n      id-token: "
        "write\n",
        "only deployment may request publishing credentials"
    );
    require_contains(
        content,
        "${{ github.ref == format('refs/heads/{0}', "
        "github.event.repository.default_branch) &&",
        "manual branch or tag runs cannot build a publishable artifact"
    );
    require_contains(
        content,
        "if: ${{ needs.build.result == 'success' && "
        "needs.build.outputs.publishable == 'true' && github.ref == "
        "format('refs/heads/{0}', github.event.repository.default_branch) }}",
        "deployment must require a successful default-branch build"
    );
    require_contains(
        content,
        "- name: Upload Pages artifact\n        if: ${{ success() && "
        "steps.publication.outcome == 'success' }}",
        "Pages upload must require the current enforcement step"
    );
    require_contains(
        content,
        "- name: Enforce Pages prerequisites\n        if: ${{ always() &&",
        "setup failures and partial stage results must reach enforcement"
    );
    require_contains(
        content,
        "MANIFESTO_SPHINX_EVIDENCE: ${{ steps.automatic.outputs.directory || "
        "steps.presentation.outputs.directory }}",
        "Sphinx must consume the selected current prerequisite snapshot"
    );
    require_contains(
        content,
        "((steps.presentation.outcome == 'success' && "
        "steps.presentation.outputs.directory != '') || "
        "steps.automatic.outputs.ready == 'true')",
        "snapshot failure cannot fall through to evidence-free documentation"
    );
    require_true(
        content.find("- name: Select current presentation reports")
            < content.find("- name: Generate docs site"),
        "evidence selection must precede documentation rendering"
    );
    require_contains(
        content,
        "workflow_run:\n    workflows: [Checks, CodeQL]\n    types: "
        "[completed]",
        "either verification workflow can complete the publication "
        "prerequisites"
    );
    require_contains(
        content, "github.event.workflow_run.head_sha == github.sha",
        "completion must describe the trusted default-branch checkout"
    );
    require_contains(
        content, "ref: ${{ github.sha }}\n          persist-credentials: false",
        "build must pin trusted workflow context and avoid persisting "
        "credentials"
    );
    require_contains(
        content, "ref: ${{ needs.build.outputs.commit }}",
        "deployment must revalidate the exact site revision"
    );
    require_contains(
        content,
        "group: pages-deploy-${{ github.workflow }}\n      cancel-in-progress: "
        "false",
        "only ready deployment jobs serialize; unrelated completions cannot "
        "cancel them"
    );
    require_true(
        content.find("Revalidate deployment source")
            < content.find("uses: actions/configure-pages"),
        "source validation must precede the deployment action"
    );
    require_true(
        content.find("Confirm current publication revision")
            > content.find("Upload Pages artifact"),
        "a branch advance during upload must block deployment scheduling"
    );
    require_contains(
        content,
        "- name: Run coverage when supported\n        if: ${{ "
        "github.event_name == "
        "'workflow_dispatch' && steps.repository.outputs.status == 'passed' }}",
        "automatic publication must use selected coverage without repeating "
        "its test run"
    );
    require_contains(
        content,
        "- name: Upload Pages reports\n        if: ${{ always() }}\n        "
        "continue-on-error: true",
        "failed publication must retain diagnostics without masking its result"
    );
    require_contains(
        content,
        "include-hidden-files: true\n          if-no-files-found: error",
        "a successful coverage artifact cannot silently omit its hidden source"
    );
    require_true(
        content.find("- name: Enforce Pages prerequisites")
            < content.find("- name: Upload coverage artifact"),
        "publication artifacts must follow enforcement"
    );
    for (const auto* forbidden :
         { " sync\n", "git diff", "mkdir -p .ecosystem/sphinx/html",
           "issues: write", "pull-requests: write" })
        require_not_contains(
            content, forbidden,
            "manual Pages must verify authored surfaces without repair or "
            "placeholders"
        );

    const auto gate = root.path() / "gate.sh";
    write_text(
        gate, github_shell_program(content, "Enforce Pages prerequisites", 8)
    );
    const std::vector<std::pair<std::string, std::string>> clean {
        { "REPOSITORY_OUTCOME", "success" },
        { "REPOSITORY_STATUS", "passed" },
        { "REPOSITORY_EXIT_CODE", "0" },
        { "COVERAGE_OUTCOME", "success" },
        { "COVERAGE_STATUS", "passed" },
        { "COVERAGE_EXIT_CODE", "0" },
        { "DOCS_OUTCOME", "success" },
        { "DOCS_STATUS", "passed" },
        { "DOCS_EXIT_CODE", "0" },
        { "PRESENTATION_OUTCOME", "success" }
    };
    const auto run_gate = [&](const auto& environment,
                              const std::string& event = "workflow_dispatch") {
        auto values = environment;
        values.emplace_back("EVENT_NAME", event);
        return ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", gate.string() }, root.path(),
            values
        );
    };
    const auto reports = root.path() / ".ecosystem/github/reports";
    std::vector<fs::path> required;
    for (const auto* stage :
         { "01-tracked-surface", "02-coverage-check", "03-docs-build" }) {
        required.push_back(reports / stage / "summary.md");
        required.push_back(reports / stage / "output.log");
    }
    const auto coverage
        = root.path() / ".ecosystem/github/presentation/reports/coverage.json";
    const auto site = root.path() / ".ecosystem/sphinx/html/index.html";
    required.push_back(
        root.path() / ".ecosystem/sphinx/html/_manifesto/index.html"
    );
    required.push_back(
        root.path() / ".ecosystem/github/presentation/receipt.json"
    );
    required.push_back(coverage);
    required.push_back(site);
    for (const auto& file : required)
        write_text(file, "current evidence");
    require_true(run_gate(clean).exit_code == 0, "complete Pages inputs pass");
    for (std::size_t i = 0; i < clean.size(); ++i) {
        const auto values = i % 3 == 0
            ? std::vector<std::string> { "", "failure", "skipped", "cancelled" }
            : i % 3 == 1
            ? std::vector<std::string> { "", "failed", "skipped" }
            : std::vector<std::string> { "", "1", "3", "5", "127" };
        for (const auto& value : values) {
            auto environment = clean;
            environment[i].second = value;
            require_true(
                run_gate(environment).exit_code != 0,
                "old HTML cannot bypass incomplete results: " + clean[i].first
                    + "=" + value
            );
        }
    }
    for (const auto& file : required) {
        fs::remove(file);
        require_true(
            run_gate(clean).exit_code != 0,
            "missing evidence must fail: " + file.string()
        );
        write_text(file, "");
        require_true(
            (run_gate(clean).exit_code == 0)
                == (file.filename() == "output.log"),
            "only a tool's diagnostic log may be empty"
        );
        write_text(file, "current evidence");
    }
    auto unsupported = clean;
    unsupported[4].second = "skipped";
    unsupported[5].second = "3";
    fs::remove(coverage);
    require_true(
        run_gate(unsupported).exit_code == 0,
        "unsupported coverage may skip with a complete report and exit 3"
    );
    unsupported[5].second = "4";
    require_true(
        run_gate(unsupported).exit_code != 0,
        "missing coverage tools cannot masquerade as unsupported coverage"
    );
    write_text(coverage, "current evidence");
    for (const auto* name : { "html.pending", "html.previous" }) {
        const auto recovery = root.path() / ".ecosystem/sphinx" / name;
        fs::create_directory(recovery);
        require_true(
            run_gate(clean).exit_code != 0,
            "publication recovery state must not be uploaded"
        );
        fs::remove(recovery);
    }
    auto automatic = clean;
    automatic[3].second = "";
    automatic[5].second = "";
    automatic[9].second = "";
    automatic.emplace_back("AUTOMATIC_READY", "true");
    automatic.emplace_back("REVALIDATION_OUTCOME", "success");
    fs::remove_all(reports / "02-coverage-check");
    require_true(
        run_gate(automatic, "workflow_run").exit_code == 0,
        "automatic publication uses validated remote evidence without local "
        "coverage"
    );
    for (const auto index : { 10U, 11U }) {
        for (const auto* value :
             { "", "false", "failure", "skipped", "cancelled" }) {
            auto invalid = automatic;
            invalid[index].second = value;
            require_true(
                run_gate(invalid, "workflow_run").exit_code != 0,
                "missing automatic evidence or revalidation must block "
                "publication"
            );
        }
    }
    fs::remove(coverage);
    require_true(
        run_gate(automatic, "workflow_run").exit_code != 0,
        "successful remote coverage still requires its selected report"
    );
    automatic[4].second = "skipped";
    require_true(
        run_gate(automatic, "workflow_run").exit_code == 0,
        "explicit unsupported remote coverage may publish"
    );
    require_true(
        run_gate(automatic, "push").exit_code != 0,
        "ordinary push events cannot bypass workflow completion selection"
    );
}

void test_github_bootstrap_vars_validate_explicit_selection() {
    temp_dir root;
    const auto generate = [&](const json& config,
                              ecosystem::string_list* errors) {
        write_text(root.path() / "manifesto.github.vars.json", config.dump());
        return ecosystem::generate_tracked_surface_files(
            sample_manifest(), root.path(), errors
        );
    };
    for (const auto& config : std::vector<json> {
             { { "manifesto_bootstrap", "unknown" } },
             { { "manifesto_repository", "example/tools" } },
             { { "manifesto_ref", "reviewed-ref" } },
             { { "manifesto_bootstrap", "checkout" },
               { "manifesto_repository", "example/tools" },
               { "manifesto_ref", "old" } },
             { { "manifesto_source_path", "../tools" } },
             { { "manifesto_source_path", "/tools" } },
             { { "manifesto_source_path", "" } },
             { { "manifesto_source_path", "tools\nnext" } },
             { { "manifesto_source_path", "${{ github.ref }}" } },
             { { "manifesto_build_parallelism", "0" } },
             { { "manifesto_build_parallelism", "2\nnext" } } }) {
        ecosystem::string_list errors;
        generate(config, &errors);
        require_true(
            !errors.empty(),
            "invalid bootstrap inputs must fail generation: " + config.dump()
        );
    }
    for (const auto& config : std::vector<json> {
             json::object(),
             { { "manifesto_bootstrap", "checkout" } },
             { { "manifesto_repository", "example/tools" },
               { "manifesto_ref", "release/preview" },
               { "manifesto_source_path", "tools/ecosystem" } },
             { { "manifesto_repository", "example/tools" },
               { "manifesto_ref", "0123456789abcdef" },
               { "manifesto_source_path", "tool source/#quoted" } } }) {
        ecosystem::string_list errors;
        const auto files = generate(config, &errors);
        require_true(
            errors.empty(), "explicit compatible bootstrap inputs must render"
        );
        for (const auto* name : { "tests.yml", "codeql.yml", "html.yml" }) {
            const auto* workflow = find_tracked_surface_file(
                files, std::string(".github/workflows/") + name
            );
            require_true(
                workflow != nullptr, "all workflow consumers must exist"
            );
            require_not_contains(
                workflow->contents, "ninjaro/cppr",
                "no obsolete repository fallback"
            );
            require_not_contains(
                workflow->contents, "manifesto-ref: master",
                "no mutable implicit ref"
            );
            require_contains(
                workflow->contents,
                "bootstrap: "
                    + config.value("manifesto_bootstrap", json("repository"))
                          .dump(),
                "workflow must forward explicit bootstrap mode as a YAML scalar"
            );
            require_contains(
                workflow->contents,
                "source-path: "
                    + config.value("manifesto_source_path", json(".")).dump(),
                "workflow must preserve the selected source layout"
            );
        }
    }
}

void test_ci_bootstrap_builds_reviewed_checkout_and_repository_layouts() {
    const auto action = ecosystem::render_required_text_template(
        "tracked/.github/actions/setup-manifesto/action.yml.tpl",
        { { "checkout_action", "actions/checkout@v4" },
          { "install_qt_action", "vendor/qt@pin" } }
    );
    require_contains(
        action, "if: ${{ inputs.bootstrap == 'repository' }}",
        "self-bootstrap must not fetch a different tooling revision"
    );
    require_not_contains(
        action, "sparse-checkout",
        "consumer layout cannot assume a parent monorepo"
    );
    require_contains(
        action, "repository: nlohmann/json\n        ref: v3.12.0",
        "clean Ubuntu runners must obtain the declared JSON version"
    );
    require_contains(
        action, "CMAKE_PREFIX_PATH=$prefix",
        "bootstrapped prerequisites must reach local child builds"
    );
    const auto prepare = github_shell_program(action, "Set ecosystem paths", 6);
    const auto build = github_shell_program(action, "Build Marx and Engels", 6);
    temp_dir fixture;
    const auto repository = fixture.path() / "provider";
    const auto source = repository / "nested/tool source";
    const auto git = [&](const ecosystem::string_list& args) {
        auto command = ecosystem::string_list { "git" };
        command.insert(command.end(), args.begin(), args.end());
        const auto result
            = ecosystem::capture_command_result(command, repository);
        require_true(
            result.exit_code == 0,
            "isolated revision fixture failed: " + result.output
        );
    };
    write_text(
        source / "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.20)\nproject(tooling_fixture "
        "LANGUAGES C)\n"
        "add_executable(marx actor.c)\nadd_executable(engels actor.c)\n"
    );
    write_text(source / "manifest.json", "{}\n");
    write_text(source / "templates/version.txt", "runtime data\n");
    write_text(
        source / "actor.c",
        "#include <stdio.h>\nint main(void) { puts(\"old-revision\"); return "
        "0; }\n"
    );
    git({ "init", "-q" });
    git({ "add", "." });
    git({ "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
          "commit", "-qm", "old" });
    git({ "tag", "old" });
    write_text(
        source / "actor.c",
        "#include <stdio.h>\nint main(void) { puts(\"reviewed-revision\"); "
        "return 0; }\n"
    );
    git({ "add", "." });
    git({ "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
          "commit", "-qm", "reviewed" });
    git({ "tag", "reviewed" });

    for (const auto* mode : { "checkout", "repository" }) {
        temp_dir workspace;
        const auto clone_root = std::string(mode) == "checkout"
            ? workspace.path() / "review"
            : workspace.path() / ".ecosystem/tooling/source";
        const auto clone = ecosystem::capture_command_result(
            { "git", "clone", "-q", repository.string(), clone_root.string() }
        );
        require_true(
            clone.exit_code == 0, "fixture must clone without remote access"
        );
        const auto selected_ref
            = std::string(mode) == "checkout" ? "reviewed" : "old";
        require_true(
            ecosystem::capture_command_result(
                { "git", "checkout", "-q", selected_ref }, clone_root
            )
                    .exit_code
                == 0,
            "fixture checkout must select the requested revision"
        );
        const auto project
            = std::string(mode) == "checkout" ? clone_root : workspace.path();
        const auto outputs = project / "outputs";
        const auto paths_script = project / "paths.sh";
        write_text(paths_script, prepare);
        auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", paths_script.string() }, project,
            { { "GITHUB_WORKSPACE", project.string() },
              { "GITHUB_OUTPUT", outputs.string() },
              { "BOOTSTRAP", mode },
              { "SOURCE_PATH", "nested/tool source" },
              { "TOOL_REPOSITORY",
                std::string(mode) == "checkout" ? "" : "fixture/tooling" },
              { "TOOL_REF",
                std::string(mode) == "checkout" ? "" : selected_ref },
              { "BUILD_PARALLELISM", "2" } }
        );
        require_true(
            result.exit_code == 0,
            "explicit bootstrap must prepare paths: " + result.output
        );
        std::map<std::string, std::string> values;
        std::istringstream lines(read_text(outputs));
        std::string line;
        while (std::getline(lines, line)) {
            const auto split = line.find('=');
            values[line.substr(0, split)] = line.substr(split + 1);
        }
        const auto build_script = project / "build.sh";
        write_text(build_script, build);
        const auto build_env
            = std::vector<std::pair<std::string, std::string>> {
                  { "CHECKOUT_ROOT", values.at("manifesto-checkout-root") },
                  { "SOURCE_ROOT", values.at("manifesto-source-root") },
                  { "BUILD_ROOT", values.at("manifesto-build-root") },
                  { "BUILD_PARALLELISM", "2" }
              };
        result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", build_script.string() }, project,
            build_env
        );
        require_true(
            result.exit_code == 0,
            "rendered bootstrap must build both actors: " + result.output
        );
        for (const auto* actor : { "marx-binary", "engels-binary" }) {
            result = ecosystem::capture_command_result(
                { values.at(actor) }, project
            );
            require_true(
                result.exit_code == 0, "advertised actor must execute"
            );
            require_contains(
                result.output,
                std::string(mode) == "checkout" ? "reviewed-revision"
                                                : "old-revision",
                "bootstrap must build exactly the reviewed or explicitly "
                "selected revision"
            );
        }
        fs::remove_all(clone_root / "nested/tool source/templates");
        result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", build_script.string() }, project,
            build_env
        );
        require_true(
            result.exit_code != 0,
            "incompatible source revisions without runtime templates must fail"
        );
        write_text(
            clone_root / "nested/tool source/templates/version.txt",
            "runtime data\n"
        );
        write_text(
            clone_root / "nested/tool source/CMakeLists.txt",
            "cmake_minimum_required(VERSION 3.20)\nproject(incompatible "
            "LANGUAGES NONE)\n"
        );
        result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", build_script.string() }, project,
            build_env
        );
        require_true(
            result.exit_code != 0,
            "stale actors from a previous build cannot satisfy an incompatible "
            "revision"
        );
        require_contains(
            result.output, "must build both marx and engels",
            "incompatible revisions must identify the required binary layout"
        );
    }
}

void test_ci_bootstrap_rejects_missing_inputs_and_escaping_layouts() {
    temp_dir root;
    const auto action = ecosystem::render_required_text_template(
        "tracked/.github/actions/setup-manifesto/action.yml.tpl",
        { { "checkout_action", "actions/checkout@v4" },
          { "install_qt_action", "vendor/qt@pin" } }
    );
    const auto paths = root.path() / "paths.sh";
    write_text(paths, github_shell_program(action, "Set ecosystem paths", 6));
    const auto clean = std::vector<std::pair<std::string, std::string>> {
        { "GITHUB_WORKSPACE", root.path().string() },
        { "GITHUB_OUTPUT", (root.path() / "outputs").string() },
        { "BOOTSTRAP", "repository" },
        { "SOURCE_PATH", "." },
        { "TOOL_REPOSITORY", "fixture/tooling" },
        { "TOOL_REF", "selected-ref" },
        { "BUILD_PARALLELISM", "2" }
    };
    for (const auto& [name, value] :
         std::vector<std::pair<std::string, std::string>> {
             { "BOOTSTRAP", "unknown" },
             { "BOOTSTRAP", "checkout" },
             { "SOURCE_PATH", "../outside" },
             { "SOURCE_PATH", "/absolute" },
             { "SOURCE_PATH", "" },
             { "SOURCE_PATH", "line\nbreak" },
             { "TOOL_REPOSITORY", "" },
             { "TOOL_REF", "" },
             { "BUILD_PARALLELISM", "0" },
             { "BUILD_PARALLELISM", "2; false" } }) {
        auto env = clean;
        for (auto& item : env)
            if (item.first == name)
                item.second = value;
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", paths.string() }, root.path(), env
        );
        require_true(
            result.exit_code != 0,
            "invalid or incomplete bootstrap must fail before checkout/build: "
                + name
        );
    }
    const auto checkout = root.path() / "checkout";
    const auto outside = root.path() / "outside";
    fs::create_directories(checkout);
    fs::create_directories(outside);
    fs::create_directory_symlink(outside, checkout / "tooling");
    const auto build = root.path() / "build.sh";
    write_text(build, github_shell_program(action, "Build Marx and Engels", 6));
    const auto result = ecosystem::capture_command_result(
        { "bash", "-e", "-o", "pipefail", build.string() }, root.path(),
        { { "CHECKOUT_ROOT", checkout.string() },
          { "SOURCE_ROOT", (checkout / "tooling").string() },
          { "BUILD_ROOT", (root.path() / "build").string() },
          { "BUILD_PARALLELISM", "2" } }
    );
    require_true(
        result.exit_code != 0 && !fs::exists(root.path() / "build"),
        "symlinked source layouts cannot escape the selected revision"
    );
}

void test_generated_builds_reject_compiler_warnings_in_c_and_cxx() {
    temp_dir root;
    write_text(
        root.path() / "manifest.json",
        json(
            { { "id", "warning_sample" },
              { "description", "Required compiler warnings" },
              { "facade", "sample:app" },
              { "artifacts",
                json::array(
                    { { { "id", "sample:app" },
                        { "kind", "exe" },
                        { "owns", json::array({ "helper" }) },
                        { "entry", "src/main.cpp" } } }
                ) } }
        ).dump(2)
    );
    write_text(
        root.path() / "src/main.cpp",
        "int main() { int unused_value = 42; return 0; }\n"
    );
    write_text(
        root.path() / "src/helper.c", "int helper(void) { return 0; }\n"
    );
    for (const auto* profile : { "debug", "release" }) {
        const auto result
            = run_marx_cli(root.path(), std::string("build ") + profile);
        require_true(
            result.exit_code == 5,
            "compiler warnings must fail local builds:\n" + result.output
        );
        require_contains(
            result.output, "unused_value",
            "build failure must retain the compiler diagnostic"
        );
    }
    require_true(
        run_marx_cli(root.path(), "sync").exit_code == 0,
        "warning fixture must generate its visitor surface"
    );
    const auto visitor = root.path() / "visitor";
    require_true(
        ecosystem::run_command(
            { "cmake", "-S", root.path().string(), "-B", visitor.string(),
              "-DCMAKE_C_COMPILER=gcc", "-DCMAKE_CXX_COMPILER=g++" },
            root.path()
        ) == 0,
        "GNU visitor fixture must configure"
    );
    const auto build_visitor = [&]() {
        return ecosystem::capture_command_result(
            { "cmake", "--build", visitor.string(), "--parallel", "2" },
            root.path()
        );
    };
    auto visitor_result = build_visitor();
    require_true(
        visitor_result.exit_code != 0,
        "visitor builds must enforce the same compiler contract"
    );
    require_contains(
        visitor_result.output, "unused_value",
        "GNU must report its selected warning"
    );
    write_text(root.path() / "src/main.cpp", "int main() { return 0; }\n");
    require_true(
        run_marx_cli(root.path(), "build debug").exit_code == 0
            && build_visitor().exit_code == 0,
        "clean C and C++ sources must build through both compilers"
    );
    write_text(
        root.path() / "src/helper.c",
        "int helper(void) { int unused_c_value = 42; return 0; }\n"
    );
    const auto native = run_marx_cli(root.path(), "build debug");
    visitor_result = build_visitor();
    require_true(
        native.exit_code == 5 && visitor_result.exit_code != 0,
        "C warnings must be hard errors without applying C++-only options"
    );
    require_contains(
        native.output, "unused_c_value",
        "Clang must retain C diagnostic attribution"
    );
    require_contains(
        visitor_result.output, "unused_c_value",
        "GNU must retain C diagnostic attribution"
    );
}

void test_cli_required_checks_propagate_failures_and_reject_empty_ctest_runs() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_sample_leak_check_project(project);
    require_true(
        run_marx_cli(project, "sync --then format").exit_code == 0,
        "aggregate check fixture must prepare canonical policy and formatting"
    );
    auto result = run_engels_cli(project, "check ci");
    require_true(
        result.exit_code == 0,
        "all required external checks must pass on clean inputs:\n"
            + result.output
    );
    require_contains(
        result.output, "ci checks passed",
        "aggregate success must follow all required stages"
    );
    const auto tests = project / "tests/main_tests.cpp";
    const auto clean_tests = read_text(tests);
    write_text(tests, "int main() { return 1; }\n");
    result = run_engels_cli(project, "check ci");
    require_true(
        result.exit_code == 5,
        "failing required tests must fail aggregate checks"
    );
    require_contains(
        result.output, "tests failed",
        "aggregate test failure must retain stage attribution"
    );
    require_not_contains(
        result.output,
        "tidy report:", "aggregate checks must stop at failed required tests"
    );
    write_text(tests, "int main() { int unused_test_value = 0; return 0; }\n");
    result = run_engels_cli(project, "check tests");
    require_true(
        result.exit_code == 5,
        "test compilation must also enforce compiler warnings"
    );
    require_contains(
        result.output, "unused_test_value",
        "test compiler errors must remain visible"
    );
    write_text(tests, clean_tests);

    const auto bin = root.path() / "bin";
    fs::create_directory(bin);
    write_fake_clang_tidy_tool(bin / "clang-tidy");
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    {
        scoped_env tidy_failure("FAKE_CLANG_TIDY_EXIT_CODE", "1");
        scoped_env tidy_output(
            "FAKE_CLANG_TIDY_OUTPUT", "required tidy fixture failure"
        );
        result = run_engels_cli(project, "check ci");
        require_true(
            result.exit_code == 5,
            "aggregate checks must propagate analyzer failure"
        );
        require_contains(
            result.output, "required tidy fixture failure",
            "aggregate failures must preserve tool output"
        );
        require_not_contains(
            result.output, "format check passed",
            "aggregate checks must stop at tidy failure"
        );
    }
    write_text(tests, "int main(){return 0;}\n");
    result = run_engels_cli(project, "check ci");
    require_true(
        result.exit_code == 5, "format drift must fail aggregate checks"
    );
    require_contains(
        result.output, "clang-format reported formatting drift",
        "format stage must remain a hard contract"
    );
    require_true(
        run_marx_cli(project, "format").exit_code == 0,
        "canonical formatting must repair predictable drift"
    );
    require_true(
        run_engels_cli(project, "check ci").exit_code == 0,
        "local repair must restore required-check success"
    );

    const auto ctest = ecosystem::find_command_path("ctest");
    scoped_env real_ctest("MANIFESTO_TEST_REAL_CTEST", ctest);
    write_executable_script(
        bin / "ctest",
        "#!/bin/sh\n"
        "if [ \"$1\" != '--version' ]; then\n"
        "  printf '# deliberately missing test registration\\n' > "
        "CTestTestfile.cmake\n"
        "fi\n"
        "exec \"$MANIFESTO_TEST_REAL_CTEST\" \"$@\"\n"
    );
    for (const auto* request :
         { "check tests", "check coverage", "check ci" }) {
        result = run_engels_cli(project, request);
        require_true(
            result.exit_code == 5,
            "no registered tests must fail required CTest execution:\n"
                + result.output
        );
        require_contains(
            result.output, "No tests were found",
            "real CTest must reject the empty selected test run"
        );
        require_not_contains(
            result.output, "ci checks passed",
            "an empty test run cannot satisfy aggregate acceptance"
        );
    }
}

void test_cli_check_ci_fails_fast_on_repository_policy_drift() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    write_text(
        root.path() / "scripts/build.sh", "#!/usr/bin/env bash\nexit 0\n"
    );

    const cli_result result = run_cli(root.path(), "check ci");
    require_true(
        result.exit_code != 0,
        "ecos check ci must fail when repository policy drift is present"
    );
    require_contains(
        result.output, "forbidden repository entries:",
        "ecos check ci must run repository policy checks before "
        "external tooling checks"
    );
    require_not_contains(
        result.output, "clang-format is not available",
        "ecos check ci must fail on repository drift before "
        "probing format tooling"
    );
    require_not_contains(
        result.output, "clang-tidy is not available",
        "ecos check ci must fail on repository drift before "
        "probing tidy tooling"
    );
}

void test_cli_check_ci_keeps_optional_features_out_of_required_checks() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_sample_leak_check_project(project);
    write_text(project / "docs/index.md", "# Authored documentation\n");
    write_text(project / "docs/conf.py", "# Authored configuration\n");
    require_true(
        run_marx_cli(project, "sync --then format").exit_code == 0,
        "required-check fixture must start with canonical tracked surfaces"
    );
    const std::vector<fs::path> preserved {
        "manifest.json",
        "docs/index.md",
        "docs/conf.py",
        ".ecosystem/sphinx/html/index.html",
        ".ecosystem/doxygen/html/index.html",
        ".ecosystem/reports/coverage.json",
        ".ecosystem/reports/naming.json",
        ".ecosystem/reports/style.json",
        ".ecosystem/reports/benchmark/core/core/result.json"
    };
    std::vector<std::string> contents;
    for (const auto& relative : preserved) {
        if (!fs::exists(project / relative))
            write_text(project / relative, "previous optional output\n");
        contents.push_back(read_text(project / relative));
    }
    const auto bin = root.path() / "bin";
    const auto invocations = root.path() / "optional-invocations";
    for (const auto* name :
         { "sphinx-build", "doxygen", "dot", "llvm-cov", "llvm-profdata",
           "java", "javac", "gradle", "adb", "androiddeployqt", "makepkg",
           "repo-add", "dpkg-deb", "gpg" })
        write_executable_script(
            bin / name,
            "#!/bin/sh\n"
            "printf '%s\\n' \"$0 $*\" >> \"$MANIFESTO_TEST_OPTIONAL_LOG\"\n"
            "echo 'optional tool deliberately unavailable' >&2\n"
            "exit 97\n"
        );
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    scoped_env log("MANIFESTO_TEST_OPTIONAL_LOG", invocations.string());
    auto result = run_engels_cli(project, "check ci");
    require_true(
        result.exit_code == 0,
        "required native checks must pass without optional tools:\n"
            + result.output
    );
    require_contains(
        result.output, "ci checks passed", "required checks must finish"
    );
    require_true(
        !fs::exists(invocations), "CI must not even probe unrelated tools"
    );
    require_true(
        !fs::exists(ecosystem::local_build_dir(project, "coverage"))
            && !fs::exists(ecosystem::local_build_dir(project, "leaks")),
        "ordinary tests must not enable coverage or sanitizer profiles"
    );
    for (std::size_t i = 0; i < preserved.size(); ++i)
        require_true(
            read_text(project / preserved[i]) == contents[i],
            "required checks must preserve authored docs and optional results"
        );

    result = run_engels_cli(project, "check sphinx");
    require_true(
        result.exit_code == 4,
        "an explicitly requested dormant operation must propagate tool failure"
    );
    require_contains(
        result.output, "sphinx-build is unavailable",
        "explicit failure must name Sphinx"
    );
    require_contains(
        result.output, "optional tool deliberately unavailable",
        "explicit failure must retain the tool's explanation"
    );
    require_not_contains(
        result.output, "sphinx generated", "failed output is not success"
    );
    for (std::size_t i = 0; i < preserved.size(); ++i)
        require_true(
            read_text(project / preserved[i]) == contents[i],
            "failed dormant execution must preserve unrelated state"
        );
}

} // namespace ecosystem_test_support
