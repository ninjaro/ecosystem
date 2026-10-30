#pragma once

#include "../suite/test_support.hpp"
#include <filesystem>

namespace ecosystem_test_support {
json prepared_input(const fs::path& project, const cli_result& result);
fs::path prepared_payload(const json& input);
fs::path prepare_fixture(
    const fs::path& project, const ecosystem::manifest& value,
    const ecosystem::resolved_artifact& primary, const fs::path& binary
);

void test_release_runtime_closure();
void test_release_provider_runtime();
void test_release_runtime_aliases();
void test_release_runtime_recovery();
void test_release_tooling_bundle();
void test_release_tooling_errors();
void test_release_asset_intent();
void test_release_cache_inventory();
void test_prerelease_prepared_full_product();
void test_prerelease_prepared_failures();
void test_release_prepared_planner(const std::filesystem::path& planner);
void test_engels_prepared_full_product();
void test_engels_prepared_rejects_inconsistent_inputs();
void test_engels_prepared_gates_and_no_build();
void test_engels_prepared_planner(const std::filesystem::path& planner);
void test_engels_prepared_evidence(const std::filesystem::path& directory);

} // namespace ecosystem_test_support
