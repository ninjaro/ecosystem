#include "packages/package_status.hpp"

#include "packages/package_cache.hpp"
#include "packages/package_dependency.hpp"
#include "packages/package_metadata.hpp"
#include "packages/package_registry.hpp"

#include <set>
#include <sstream>
#include <string>

namespace ecosystem {
namespace package_status_support {

    std::string join_set(
        const std::set<std::string>& values, const std::string& separator
    ) {
        std::ostringstream stream;
        bool first = true;
        for (const std::string& value : values) {
            if (!first) {
                stream << separator;
            }
            stream << value;
            first = false;
        }
        return stream.str();
    }

    std::string format_detected_values(
        const std::set<std::string>& detected_values,
        const std::set<std::string>& missing_values
    ) {
        std::ostringstream stream;
        if (!detected_values.empty()) {
            stream << "detected " << join_set(detected_values, " ");
        }
        if (!missing_values.empty()) {
            if (!stream.str().empty()) {
                stream << "; ";
            }
            stream << "missing " << join_set(missing_values, " ");
        }
        if (stream.str().empty()) {
            return "missing";
        }
        return stream.str();
    }

    bool status_rule_profile_enabled(
        const package_status_rule& rule, const cmake_cache_snapshot& cache
    ) {
        if (rule.profile_flag.empty()) {
            return true;
        }
        return cmake_cache_flag_enabled(cache, std::string(rule.profile_flag));
    }

    std::string configured_status_for_rule(
        const package_descriptor& descriptor,
        const dependency_summary& dependencies,
        const cmake_cache_snapshot& cache
    ) {
        if (!status_rule_profile_enabled(descriptor.status_rule, cache)) {
            return std::string(descriptor.status_rule.profile_disabled_status);
        }

        switch (descriptor.status_rule.source) {
        case package_status_source::all_found_keys:
            return cmake_cache_has_all_found_values(
                       cache, descriptor.status_rule.keys
                   )
                ? "detected"
                : "missing";
        case package_status_source::dependency_values_prefix_suffix: {
            std::set<std::string> detected_values;
            std::set<std::string> missing_values;
            const dependency_entry& entry
                = package_dependency_entry(dependencies, descriptor);
            for (const std::string& value : entry.values) {
                const std::string key
                    = std::string(descriptor.status_rule.key_prefix) + value
                    + std::string(descriptor.status_rule.key_suffix);
                if (cmake_cache_has_found_value(cache, key)) {
                    detected_values.insert(value);
                } else {
                    missing_values.insert(value);
                }
            }
            return format_detected_values(detected_values, missing_values);
        }
        }

        return "missing";
    }

} // namespace package_status_support

using namespace package_status_support;

std::string configured_status_for_package(
    const package_kind kind, const dependency_summary& dependencies,
    const cmake_cache_snapshot& cache
) {
    const package_descriptor* descriptor = find_package_descriptor(kind);
    if (descriptor == nullptr) {
        return "missing";
    }
    return configured_status_for_rule(*descriptor, dependencies, cache);
}

} // namespace ecosystem
