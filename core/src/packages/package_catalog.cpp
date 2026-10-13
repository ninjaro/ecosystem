#include "packages/package_catalog.hpp"

#include "packages/package_registry.hpp"
#include "packages/package_status.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ecosystem {
namespace package_catalog_support {

    std::vector<package_line>* select_package_group(
        std::vector<package_line>* required_packages,
        std::vector<package_line>* kde_profile_packages,
        std::vector<package_line>* optional_packages, const package_group group
    ) {
        switch (group) {
        case package_group::required:
            return required_packages;
        case package_group::profile_kde:
            return kde_profile_packages;
        case package_group::optional:
            return optional_packages;
        }

        return required_packages;
    }

    std::vector<package_status_line>* select_package_status_group(
        std::vector<package_status_line>* required_packages,
        std::vector<package_status_line>* kde_profile_packages,
        std::vector<package_status_line>* optional_packages,
        const package_group group
    ) {
        switch (group) {
        case package_group::required:
            return required_packages;
        case package_group::profile_kde:
            return kde_profile_packages;
        case package_group::optional:
            return optional_packages;
        }

        return required_packages;
    }

    void append_package_line(
        std::vector<package_line>* packages, std::string_view label,
        const dependency_entry& entry, const bool include_values = false
    ) {
        if (!entry.enabled) {
            return;
        }
        packages->push_back(package_line { std::string(label), entry,
                                           include_values });
    }

    void append_package_status_line(
        std::vector<package_status_line>* packages, std::string_view label,
        const std::string& status
    ) {
        packages->push_back(package_status_line { std::string(label), status });
    }

    void append_section(
        std::vector<package_section>* sections, const package_group group,
        std::vector<package_line>* packages
    ) {
        if (packages->empty()) {
            return;
        }
        sections->push_back(package_section { group, *packages });
    }

    void append_status_section(
        std::vector<package_status_section>* sections,
        const package_group group, std::vector<package_status_line>* packages
    ) {
        if (packages->empty()) {
            return;
        }
        sections->push_back(package_status_section { group, *packages });
    }

} // namespace package_catalog_support

using namespace package_catalog_support;

std::vector<package_section> declared_package_sections(
    const dependency_summary& dependencies, const bool kde_enabled_for_profile
) {
    std::vector<package_line> required_packages;
    std::vector<package_line> kde_profile_packages;
    std::vector<package_line> optional_packages;

    for (const package_descriptor* descriptor :
         enabled_packages(dependencies)) {
        if (descriptor == nullptr) {
            continue;
        }

        const dependency_entry& entry
            = package_dependency_entry(dependencies, *descriptor);
        const package_group group
            = effective_package_group(*descriptor, kde_enabled_for_profile);
        std::vector<package_line>* packages = select_package_group(
            &required_packages, &kde_profile_packages, &optional_packages, group
        );
        append_package_line(
            packages, descriptor->label, entry, descriptor->include_values
        );
    }

    std::vector<package_section> sections;
    append_section(&sections, package_group::required, &required_packages);
    append_section(
        &sections, package_group::profile_kde, &kde_profile_packages
    );
    append_section(&sections, package_group::optional, &optional_packages);
    return sections;
}

std::vector<package_status_section> configured_package_sections(
    const dependency_summary& dependencies, const cmake_cache_snapshot& cache
) {
    std::vector<package_status_line> required_packages;
    std::vector<package_status_line> kde_profile_packages;
    std::vector<package_status_line> optional_packages;

    for (const package_descriptor* descriptor :
         enabled_packages(dependencies)) {
        if (descriptor == nullptr) {
            continue;
        }

        std::vector<package_status_line>* packages
            = select_package_status_group(
                &required_packages, &kde_profile_packages, &optional_packages,
                descriptor->group
            );
        append_package_status_line(
            packages, descriptor->label,
            configured_status_for_package(descriptor->kind, dependencies, cache)
        );
    }

    std::vector<package_status_section> sections;
    append_status_section(
        &sections, package_group::required, &required_packages
    );
    append_status_section(
        &sections, package_group::profile_kde, &kde_profile_packages
    );
    append_status_section(
        &sections, package_group::optional, &optional_packages
    );
    return sections;
}

} // namespace ecosystem
