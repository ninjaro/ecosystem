#include "../ci.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_ci_evidence() {
    temp_dir root;
    const auto files = ecosystem::generate_tracked_surface_files(
        sample_manifest(), root.path()
    );
    const auto* workflow
        = find_tracked_surface_file(files, ".github/workflows/tests.yml");
    require_true(workflow != nullptr, "Checks workflow must be generated");
    const auto workflow_path = root.path() / "tests.yml";
    write_text(workflow_path, workflow->contents);
    const auto* codeql
        = find_tracked_surface_file(files, ".github/workflows/codeql.yml");
    require_true(codeql != nullptr, "CodeQL workflow must be generated");
    const auto codeql_path = root.path() / "codeql.yml";
    write_text(codeql_path, codeql->contents);
    for (const auto* adapter : { "changes.cjs", "evidence.cjs" }) {
        const auto* script = find_tracked_surface_file(
            files, ".github/actions/select-manifesto-ci/" + std::string(adapter)
        );
        require_true(script != nullptr, "CI adapter must be generated");
        const auto destination = root.path() / adapter;
        write_text(destination, script->contents);
    }
    const auto* diagnostics = find_tracked_surface_file(
        files, ".github/actions/publish-manifesto-report/diagnostics.cjs"
    );
    require_true(diagnostics != nullptr, "CI diagnostics must be generated");
    write_text(root.path() / "diagnostics.cjs", diagnostics->contents);
    for (const auto& [adapter, fixture] :
         std::vector<std::pair<std::string, std::string>> {
             { "changes.cjs", "ci_change_cases.cjs" },
             { "changes.cjs", "ci_change_policy_cases.cjs" },
             { "changes.cjs", "ci_changed_format_cases.cjs" },
             { "changes.cjs", "ci_cheap_format_cases.cjs" },
             { "changes.cjs", "ci_classification_cases.cjs" },
             { "changes.cjs", "ci_codeql_cases.cjs" },
             { "evidence.cjs", "ci_evidence_cases.cjs" },
             { "evidence.cjs", "ci_codeql_delta_cases.cjs" },
             { "evidence.cjs", "ci_publication_cases.cjs" } }) {
        const auto destination = root.path() / adapter;
        const auto result = ecosystem::capture_command_result(
            { "node",
              (fs::path(ECOS_TEST_SOURCE_DIR) / "assets/dataset/test/suite"
               / fixture)
                  .string(),
              destination.string(), engels_binary_path().string(),
              workflow_path.string(), codeql_path.string() },
            root.path()
        );
        require_true(
            result.exit_code == 0,
            "GitHub transport regressions require Node and must pass:\n"
                + result.output
        );
    }
    const auto diagnostics_result = ecosystem::capture_command_result(
        { "node",
          (fs::path(ECOS_TEST_SOURCE_DIR)
           / "assets/dataset/test/suite/ci_diagnostics_cases.cjs")
              .string(),
          (root.path() / "diagnostics.cjs").string(),
          (root.path() / "evidence.cjs").string() },
        root.path()
    );
    require_true(
        diagnostics_result.exit_code == 0,
        "CI skip/report regressions must pass:\n" + diagnostics_result.output
    );
}

} // namespace ecosystem_test_support
