#include "packages/package_summary.hpp"

#include "packages/package_registry.hpp"
#include "workspace/project.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ecosystem {
namespace package_summary_support {

    bool json_flag_enabled(const json& object, const std::string& key) {
        return object.contains(key) && object.at(key).is_boolean()
            && object.at(key).get<bool>();
    }

    std::vector<const component*> scoped_components(
        const manifest& manifest_value,
        const std::optional<artifact_ref>& requested_artifact
    ) {
        if (requested_artifact.has_value()) {
            return component_closure_for_artifact(
                manifest_value, *requested_artifact
            );
        }

        std::vector<const component*> components;
        components.reserve(manifest_value.components.size());
        for (const component& component_value : manifest_value.components) {
            components.push_back(&component_value);
        }
        return components;
    }

    void record_dependency_owner(
        dependency_entry* entry, const component& component_value
    ) {
        entry->enabled = true;
        entry->owners.insert(component_value.id);
    }

    void record_dependency_values(
        dependency_entry* entry, const component& component_value,
        const std::vector<std::string>& values
    ) {
        entry->enabled = true;
        entry->owners.insert(component_value.id);
        entry->values.insert(values.begin(), values.end());
    }

    void push_unique_target(
        std::vector<std::string>* targets, std::set<std::string>* seen,
        const std::string& target
    ) {
        if (!target.empty() && seen->insert(target).second) {
            targets->push_back(target);
        }
    }

    bool stack_values_contain(
        const std::vector<std::string>& values,
        const std::string_view expected_value
    ) {
        return std::ranges::any_of(values, [&](const std::string& value) {
            return value == expected_value;
        });
    }

    std::vector<std::string> summarize_stack_values(
        const std::vector<std::string>& stack_values,
        const package_summary_rule& rule
    ) {
        if (rule.value.empty()) {
            return stack_values;
        }

        std::vector<std::string> values;
        for (const std::string& value : stack_values) {
            const bool value_matches_rule = value == rule.value;
            if (rule.exclude_matching_values == value_matches_rule) {
                continue;
            }
            values.push_back(value);
        }
        return values;
    }

    bool summary_rule_enabled(
        const component& component_value, const package_summary_rule& rule,
        std::vector<std::string>* resolved_values
    ) {
        resolved_values->clear();

        switch (rule.source) {
        case package_summary_source::component_stack: {
            if (!component_has_stack_key(
                    component_value, std::string(rule.key)
                )) {
                return false;
            }
            *resolved_values = component_stack_values(
                component_value, std::string(rule.key)
            );
            if (rule.effect == package_summary_effect::owner_only
                && !rule.value.empty()
                && !stack_values_contain(*resolved_values, rule.value)) {
                return false;
            }
            return true;
        }
        case package_summary_source::component_tests_flag:
            return json_flag_enabled(
                component_value.tests, std::string(rule.key)
            );
        case package_summary_source::component_benchmarks_flag:
            return json_flag_enabled(
                component_value.benchmarks, std::string(rule.key)
            );
        }

        return false;
    }

    void apply_summary_rule(
        dependency_entry* entry, const component& component_value,
        const package_summary_rule& rule
    ) {
        std::vector<std::string> resolved_values;
        if (!summary_rule_enabled(component_value, rule, &resolved_values)) {
            return;
        }

        switch (rule.effect) {
        case package_summary_effect::owner_only:
            record_dependency_owner(entry, component_value);
            return;
        case package_summary_effect::stack_values: {
            record_dependency_owner(entry, component_value);
            const std::vector<std::string> values
                = summarize_stack_values(resolved_values, rule);
            entry->values.insert(values.begin(), values.end());
            return;
        }
        case package_summary_effect::fixed_value:
            record_dependency_values(
                entry, component_value, { std::string(rule.value) }
            );
            return;
        }
    }

    void summarize_component_dependencies(
        dependency_summary* summary, const component& component_value,
        const package_dependency_scope scope
    ) {
        for (const package_descriptor& descriptor : registered_packages()) {
            dependency_entry& entry
                = dependency_entry_for_id(*summary, descriptor.dependency);
            for (const package_summary_rule& rule : descriptor.summary_rules) {
                if (rule.source == package_summary_source::component_tests_flag
                    && scope != package_dependency_scope::all
                    && scope != package_dependency_scope::tests)
                    continue;
                if (rule.source
                        == package_summary_source::component_benchmarks_flag
                    && scope != package_dependency_scope::all
                    && scope != package_dependency_scope::benchmarks)
                    continue;
                apply_summary_rule(&entry, component_value, rule);
            }
        }
    }

    void append_descriptor_link_targets(
        std::vector<std::string>* targets, std::set<std::string>* seen,
        const dependency_summary& dependencies,
        const package_descriptor& descriptor
    ) {
        const dependency_entry& entry
            = package_dependency_entry(dependencies, descriptor);
        if (!entry.enabled) {
            return;
        }

        for (const package_link_rule& rule : descriptor.link_rules) {
            switch (rule.source) {
            case package_link_target_source::none:
                continue;
            case package_link_target_source::fixed:
                push_unique_target(targets, seen, std::string(rule.value));
                continue;
            case package_link_target_source::dependency_values_prefix:
                for (const std::string& value : entry.values) {
                    push_unique_target(
                        targets, seen, std::string(rule.value) + value
                    );
                }
                continue;
            }
        }
    }

} // namespace package_summary_support

using namespace package_summary_support;

dependency_summary summarize_dependencies(
    const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    const package_dependency_scope scope
) {
    dependency_summary summary;

    for (const component* component_value :
         scoped_components(manifest_value, requested_artifact)) {
        if (component_value == nullptr) {
            continue;
        }

        summarize_component_dependencies(&summary, *component_value, scope);
    }

    return summary;
}

bool dependency_summary_has_entries(const dependency_summary& dependencies) {
    return has_enabled_packages(dependencies);
}

std::vector<std::string> component_package_link_targets(
    const component& component_value, const package_dependency_scope scope
) {
    dependency_summary dependencies;
    summarize_component_dependencies(&dependencies, component_value, scope);

    std::vector<std::string> targets;
    std::set<std::string> seen;

    for (const package_descriptor* descriptor :
         enabled_packages(dependencies)) {
        if (descriptor == nullptr) {
            continue;
        }
        append_descriptor_link_targets(
            &targets, &seen, dependencies, *descriptor
        );
    }

    // An authored main can opt out of the framework's default entry point.
    if (dependencies.gtest.enabled
        && component_value.tests.value("gtest_main", json(true)) != false)
        push_unique_target(&targets, &seen, "GTest::gtest_main");

    return targets;
}

} // namespace ecosystem
