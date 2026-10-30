#pragma once

#include <string_view>
#include <vector>

namespace ecosystem {

enum class package_summary_source {
    component_stack,
    component_tests_flag,
    component_benchmarks_flag,
};

enum class package_summary_effect {
    owner_only,
    stack_values,
    fixed_value,
};

enum class package_link_target_source {
    none,
    fixed,
    dependency_values_prefix,
};

enum class package_status_source {
    all_found_keys,
    dependency_values_prefix_suffix,
};

struct package_summary_rule {
    package_summary_source source = package_summary_source::component_stack;
    std::string_view key;
    package_summary_effect effect = package_summary_effect::owner_only;
    std::string_view value;
    bool exclude_matching_values = false;
    // Empty means native component names resolved by the package finder.
    std::vector<std::string_view> supported_values {};
    bool allow_enabled_flag = false;
};

struct package_link_rule {
    package_link_target_source source = package_link_target_source::none;
    std::string_view value;
};

struct package_status_rule {
    package_status_source source = package_status_source::all_found_keys;
    std::vector<std::string_view> keys;
    std::string_view key_prefix;
    std::string_view key_suffix;
    std::string_view profile_flag;
    std::string_view profile_disabled_status;
    bool uppercase_values = false;
};

} // namespace ecosystem
