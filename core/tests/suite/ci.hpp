#pragma once

#include <cstddef>
#include <string>

namespace ecosystem_test_support {

void test_ci_evidence();
void test_ci_selection();
void test_ci_stage_failures();
void test_ci_result_gates();
void test_ci_pages_gates();
void test_ci_compiler_cache_configuration();
void test_ci_bootstrap_options();
void test_ci_bootstrap_layouts();
void test_ci_bootstrap_errors();
void test_build_warning_errors();
void test_check_failure_propagation();
void test_ci_repo_drift();
void test_ci_optional_checks();

/** Extract the shell block from a named workflow or action step. */
std::string ci_shell(
    const std::string& yaml, const std::string& step_name,
    std::size_t indentation
);

/** Replace one literal GitHub expression in a generated fixture. */
std::string ci_bind(
    std::string text, const std::string& expression, const std::string& value
);

} // namespace ecosystem_test_support
