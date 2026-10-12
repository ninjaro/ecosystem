#pragma once

#include <filesystem>

namespace ecosystem_test_support {

void test_release_runtime_closure();
void test_release_dependency_queries();
// Opt-in integration acceptance: uses native cached distribution packages and
// user namespaces. It is intentionally outside the ordinary portable suite.
void test_release_isolated_install(const std::filesystem::path& cache);
void test_release_provider_runtime();
void test_release_runtime_aliases();
void test_release_runtime_recovery();
void test_release_tooling_bundle();
void test_release_tooling_errors();
void test_release_asset_intent();
void test_release_cache_inventory();

} // namespace ecosystem_test_support
