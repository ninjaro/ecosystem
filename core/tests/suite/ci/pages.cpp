#include "../ci.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_ci_pages_gates() {
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
        "ECOSYSTEM_SPHINX_EVIDENCE: ${{ steps.automatic.outputs.directory || "
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
    write_text(gate, ci_shell(content, "Enforce Pages prerequisites", 8));
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
        root.path() / ".ecosystem/sphinx/html/_ecosystem/index.html"
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

} // namespace ecosystem_test_support
