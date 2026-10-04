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
    for (const auto& [adapter, fixture] :
         std::vector<std::pair<std::string, std::string>> {
             { "changes.cjs", "ci_change_cases.cjs" },
             { "changes.cjs", "ci_change_policy_cases.cjs" },
             { "changes.cjs", "ci_changed_format_cases.cjs" },
             { "changes.cjs", "ci_cheap_format_cases.cjs" },
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
              destination.string(), engels_binary_path().string(),
              workflow_path.string() },
            root.path()
        );
        require_true(
            result.exit_code == 0,
            "GitHub transport regressions require Node and must pass:\n"
                + result.output
        );
    }
}

} // namespace ecosystem_test_support
