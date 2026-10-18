#include "packages/package_registry.hpp"

#include "packages/package_dependency.hpp"

#include <algorithm>
#include <vector>

namespace ecosystem {

const std::vector<package_descriptor>& package_descriptors() {
    static const std::vector<package_descriptor> descriptors {
        {
            package_kind::nlohmann_json,
            dependency_id::nlohmann_json,
            {
                {
                    package_summary_source::component_stack,
                    "json",
                    package_summary_effect::stack_values,
                    {},
                    false,
                },
            },
            {
                {
                    package_link_target_source::dependency_values_prefix,
                    "nlohmann_json::",
                },
            },
            {
                package_status_source::all_found_keys,
                { "nlohmann_json_DIR" },
                {},
                {},
                {},
                {},
            },
            package_group::required,
            package_surface_block::nlohmann_json,
            "nlohmann_json 3.12 CONFIG package",
        },
        {
            package_kind::llvm_clang,
            dependency_id::llvm_clang,
            {
                {
                    package_summary_source::component_stack,
                    "llvm_clang",
                    package_summary_effect::stack_values,
                    {},
                    false,
                },
            },
            {
                {
                    package_link_target_source::fixed,
                    "ecosystem_llvm_clang_support",
                },
            },
            {
                package_status_source::all_found_keys,
                { "LLVM_DIR", "Clang_DIR" },
                {},
                {},
                {},
                {},
            },
            package_group::required,
            package_surface_block::llvm_clang,
            "LLVM CONFIG + Clang CONFIG + libclang target",
        },
        {
            package_kind::qt,
            dependency_id::qt,
            {
                {
                    package_summary_source::component_stack,
                    "qt",
                    package_summary_effect::stack_values,
                    {},
                    false,
                },
                {
                    package_summary_source::component_tests_flag,
                    "qttest",
                    package_summary_effect::fixed_value,
                    "Test",
                    false,
                },
            },
            {
                {
                    package_link_target_source::dependency_values_prefix,
                    "Qt6::",
                },
            },
            {
                package_status_source::dependency_values_prefix_suffix,
                {},
                "Qt6",
                "_DIR",
                {},
                {},
            },
            package_group::required,
            package_surface_block::qt,
            "Qt6 6.0 CONFIG components",
            true,
        },
        {
            package_kind::kde,
            dependency_id::kde,
            {
                {
                    package_summary_source::component_stack,
                    "kde",
                    package_summary_effect::stack_values,
                    "KDEGames6",
                    true,
                },
            },
            {
                {
                    package_link_target_source::fixed,
                    "ecosystem_kde_support",
                },
            },
            {
                package_status_source::all_found_keys,
                { "KF6_DIR" },
                {},
                {},
                "ECOSYSTEM_PROFILE_KDE",
                "profile disabled in this configure data",
            },
            package_group::profile_kde,
            package_surface_block::kde,
            "KF6 CONFIG components",
            true,
            true,
        },
        {
            package_kind::kdegames,
            dependency_id::kdegames,
            {
                {
                    package_summary_source::component_stack,
                    "kde",
                    package_summary_effect::owner_only,
                    "KDEGames6",
                },
            },
            {},
            {
                package_status_source::all_found_keys,
                { "KDEGames6_DIR" },
                {},
                {},
                "ECOSYSTEM_PROFILE_KDE",
                "profile disabled in this configure data",
            },
            package_group::profile_kde,
            package_surface_block::kde,
            "KDEGames6 package",
            false,
            true,
        },
        {
            package_kind::opencv,
            dependency_id::opencv,
            {
                {
                    package_summary_source::component_stack,
                    "opencv",
                    package_summary_effect::stack_values,
                    {},
                    false,
                },
            },
            {
                {
                    package_link_target_source::fixed,
                    "ecosystem_optional_opencv",
                },
            },
            {
                package_status_source::all_found_keys,
                { "OpenCV_DIR" },
                {},
                {},
                {},
                {},
            },
            package_group::optional,
            package_surface_block::opencv,
            "OpenCV package",
        },
        {
            package_kind::eigen,
            dependency_id::eigen,
            {
                {
                    package_summary_source::component_stack,
                    "eigen",
                    package_summary_effect::stack_values,
                    {},
                    false,
                },
            },
            {
                {
                    package_link_target_source::fixed,
                    "ecosystem_optional_eigen",
                },
            },
            {
                package_status_source::all_found_keys,
                { "Eigen3_DIR" },
                {},
                {},
                {},
                {},
            },
            package_group::optional,
            package_surface_block::eigen,
            "Eigen3 3.3 package",
        },
        {
            package_kind::jni,
            dependency_id::jni,
            {
                {
                    package_summary_source::component_stack,
                    "jni",
                    package_summary_effect::stack_values,
                    {},
                    false,
                },
            },
            {
                {
                    package_link_target_source::fixed,
                    "ecosystem_jni_support",
                },
            },
            {
                package_status_source::all_found_keys,
                { "JAVA_INCLUDE_PATH", "JAVA_JVM_LIBRARY" },
                {},
                {},
                {},
                {},
            },
            package_group::required,
            package_surface_block::jni,
            "JNI package",
        },
        {
            package_kind::cxxopts,
            dependency_id::cxxopts,
            {
                {
                    package_summary_source::component_stack,
                    "cxxopts",
                    package_summary_effect::stack_values,
                    {},
                    false,
                },
            },
            {
                {
                    package_link_target_source::dependency_values_prefix,
                    "cxxopts::",
                },
            },
            {
                package_status_source::all_found_keys,
                { "cxxopts_DIR" },
                {},
                {},
                {},
                {},
            },
            package_group::required,
            package_surface_block::cxxopts,
            "cxxopts CONFIG package",
        },
        {
            package_kind::zlib,
            dependency_id::zlib,
            {
                {
                    package_summary_source::component_stack,
                    "zlib",
                    package_summary_effect::stack_values,
                    {},
                    false,
                },
            },
            {
                {
                    package_link_target_source::fixed,
                    "ZLIB::ZLIB",
                },
            },
            {
                package_status_source::all_found_keys,
                { "ZLIB_INCLUDE_DIR", "ZLIB_LIBRARY_RELEASE" },
                {},
                {},
                {},
                {},
            },
            package_group::required,
            package_surface_block::zlib,
            "ZLIB package",
        },
        {
            package_kind::gtest,
            dependency_id::gtest,
            {
                {
                    package_summary_source::component_tests_flag,
                    "gtest",
                    package_summary_effect::owner_only,
                    {},
                    false,
                },
            },
            {
                {
                    package_link_target_source::fixed,
                    "GTest::gtest",
                },
            },
            {
                package_status_source::all_found_keys,
                { "GTest_DIR" },
                {},
                {},
                {},
                {},
            },
            package_group::optional,
            package_surface_block::gtest,
            "GTest package",
        },
        {
            package_kind::benchmark,
            dependency_id::benchmark,
            {
                {
                    package_summary_source::component_benchmarks_flag,
                    "google_benchmark",
                    package_summary_effect::owner_only,
                    {},
                    false,
                },
            },
            {
                {
                    package_link_target_source::fixed,
                    "benchmark::benchmark_main",
                },
            },
            {
                package_status_source::all_found_keys,
                { "benchmark_DIR" },
                {},
                {},
                {},
                {},
            },
            package_group::optional,
            package_surface_block::benchmark,
            "benchmark package",
        },
    };
    return descriptors;
}

std::span<const package_descriptor> registered_packages() {
    return package_descriptors();
}

std::vector<const package_descriptor*>
enabled_packages(const dependency_summary& dependencies) {
    std::vector<const package_descriptor*> descriptors;
    descriptors.reserve(package_descriptors().size());

    for (const package_descriptor& descriptor : registered_packages()) {
        if (package_dependency_entry(dependencies, descriptor).enabled) {
            descriptors.push_back(&descriptor);
        }
    }

    return descriptors;
}

bool has_enabled_packages(const dependency_summary& dependencies) {
    return std::ranges::any_of(
        enabled_packages(dependencies),
        [](const package_descriptor* descriptor) {
            return descriptor != nullptr;
        }
    );
}

const package_descriptor* find_package_descriptor(const package_kind kind) {
    for (const package_descriptor& descriptor : registered_packages()) {
        if (descriptor.kind == kind) {
            return &descriptor;
        }
    }

    return nullptr;
}

} // namespace ecosystem
