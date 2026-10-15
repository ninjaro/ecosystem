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
        "ECOSYSTEM_SPHINX_EVIDENCE: ${{ steps.automatic.outputs.directory }}",
        "every publication consumes verified Checks coverage"
    );
    require_true(
        content.find("Select verified publication evidence")
            < content.find("Generate docs site"),
        "exact-commit evidence selection precedes rendering"
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
        "- name: Upload Pages reports\n        if: ${{ always() }}\n        "
        "continue-on-error: true",
        "failed publication retains internal diagnostics"
    );
    for (const auto* forbidden :
         { " sync\n", "git diff", "mkdir -p .ecosystem/sphinx/html",
           "issues: write", "pull-requests: write", "check coverage", "ctest",
           "llvm-profdata", "llvm-cov", "Upload coverage artifact" })
        require_not_contains(
            content, forbidden,
            "manual Pages must verify authored surfaces without repair or "
            "placeholders"
        );

    const auto gate = root.path() / "gate.sh";
    write_text(gate, ci_shell(content, "Enforce Pages prerequisites", 8));
    const std::vector<std::pair<std::string, std::string>> clean {
        { "REPOSITORY_OUTCOME", "success" },  { "REPOSITORY_STATUS", "passed" },
        { "REPOSITORY_EXIT_CODE", "0" },      { "DOCS_OUTCOME", "success" },
        { "DOCS_STATUS", "passed" },          { "DOCS_EXIT_CODE", "0" },
        { "COVERAGE_STATUS", "passed" },      { "AUTOMATIC_READY", "true" },
        { "REVALIDATION_OUTCOME", "success" }
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
    for (const auto* stage : { "01-tracked-surface", "03-docs-build" }) {
        required.push_back(reports / stage / "summary.md");
        required.push_back(reports / stage / "output.log");
    }
    for (const auto* file :
         { ".ecosystem/github/presentation/reports/coverage.json",
           ".ecosystem/github/presentation/reports/coverage.zip",
           ".ecosystem/github/presentation/receipt.json",
           ".ecosystem/sphinx/html/index.html",
           ".ecosystem/sphinx/html/_ecosystem/coverage/index.html" })
        required.emplace_back(root.path() / file);
    for (const auto& file : required)
        write_text(file, "current evidence");
    for (const auto* event : { "workflow_dispatch", "workflow_run" }) {
        require_true(
            run_gate(clean, event).exit_code == 0,
            "verified publication inputs pass"
        );
        for (std::size_t i = 0; i < clean.size(); ++i) {
            for (const auto* value :
                 { "", "failed", "failure", "cancelled", "3", "false" }) {
                auto environment = clean;
                environment[i].second = value;
                require_true(
                    run_gate(environment, event).exit_code != 0,
                    "old HTML cannot bypass incomplete results: "
                        + clean[i].first + "=" + value
                );
            }
        }
        for (const auto& file : required) {
            fs::remove(file);
            require_true(
                run_gate(clean, event).exit_code != 0,
                "missing evidence must fail: " + file.string()
            );
            write_text(file, "");
            require_true(
                (run_gate(clean, event).exit_code == 0)
                    == (file.filename() == "output.log"),
                "only diagnostic output may be empty"
            );
            write_text(file, "current evidence");
        }
    }
    auto unsupported = clean;
    unsupported[6].second = "skipped";
    for (const auto* name : { "coverage.json", "coverage.zip" })
        fs::remove(
            root.path() / ".ecosystem/github/presentation/reports" / name
        );
    require_true(
        run_gate(unsupported).exit_code == 0,
        "verified unsupported coverage may publish its unavailable page"
    );
    require_true(
        run_gate(clean).exit_code != 0,
        "supported coverage requires both measured data and HTML"
    );
    for (const auto* name : { "coverage.json", "coverage.zip" })
        write_text(
            root.path() / ".ecosystem/github/presentation/reports" / name,
            "current evidence"
        );
    for (const auto* name : { "html.pending", "html.previous" }) {
        const auto recovery = root.path() / ".ecosystem/sphinx" / name;
        fs::create_directory(recovery);
        require_true(
            run_gate(clean).exit_code != 0,
            "publication recovery state must not be uploaded"
        );
        fs::remove(recovery);
    }
    require_true(
        run_gate(clean, "push").exit_code != 0,
        "ordinary pushes cannot bypass verified publication selection"
    );
}

} // namespace ecosystem_test_support
