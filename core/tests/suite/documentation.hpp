#pragma once

#include <filesystem>

namespace ecosystem_test_support {

void test_doxygen_ownership();
void test_doxygen_native_scope();
void test_doxygen_graphviz_errors();
void test_doxygen_fresh_output();
void test_doxygen_output_aliases();
void test_doxygen_workspace_scope();
void test_doxygen_config_errors();
void test_sphinx_presentation_contract();
void test_sphinx_default_config();
void test_sphinx_theme_override();
void test_sphinx_output_aliases();
void test_sphinx_failure_recovery();
void test_sphinx_native_pipeline();
void test_native_asset_pages(const std::filesystem::path& project);
void test_native_diagnostics(const std::filesystem::path& project);
void test_native_benchmarks(const std::filesystem::path& project);

void write_fake_sphinx(const std::filesystem::path& path);

} // namespace ecosystem_test_support
