#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

void test_package_surface_includes_qttest_component() {
    const ecosystem::manifest manifest_value = sample_qttest_manifest();
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(manifest_value, std::nullopt);
    require_true(
        dependencies.qt.values.contains("Test"),
        "shared package surface summary must derive Qt Test from tests.qttest"
    );

    temp_dir root;
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());
    require_contains(
        generated_cmake, "find_package(Qt6 6.0 CONFIG REQUIRED COMPONENTS",
        "generated facade must include the shared Qt package block"
    );
    require_contains(
        generated_cmake, "        Test\n",
        "generated facade must include the derived Qt Test component"
    );
}

void test_component_package_link_targets_include_qttest_target() {
    const ecosystem::manifest manifest_value = sample_qttest_manifest();
    const ecosystem::component& component_value
        = manifest_value.components.front();
    const std::vector<std::string> targets
        = ecosystem::component_package_link_targets(component_value);

    require_true(
        std::find(targets.begin(), targets.end(), "Qt6::Core") != targets.end(),
        "package link targets must include declared Qt component targets"
    );
    require_true(
        std::find(targets.begin(), targets.end(), "Qt6::Widgets")
            != targets.end(),
        "package link targets must include all declared Qt component targets"
    );
    require_true(
        std::find(targets.begin(), targets.end(), "Qt6::Test") != targets.end(),
        "package link targets must derive Qt Test from tests.qttest"
    );
}

void test_component_package_link_targets_include_cxxopts_target() {
    const ecosystem::manifest manifest_value = sample_cxxopts_manifest();
    const ecosystem::component& component_value
        = manifest_value.components.front();
    const std::vector<std::string> targets
        = ecosystem::component_package_link_targets(component_value);

    require_true(
        std::find(targets.begin(), targets.end(), "cxxopts::cxxopts")
            != targets.end(),
        "package link targets must include the cxxopts package target"
    );
}

void test_component_package_link_targets_follow_descriptor_rules() {
    const ecosystem::manifest manifest_value = sample_dependency_manifest();

    const std::vector<std::string> core_targets
        = ecosystem::component_package_link_targets(
            manifest_value.components[0]
        );
    require_true(
        std::find(
            core_targets.begin(), core_targets.end(),
            "nlohmann_json::nlohmann_json"
        ) != core_targets.end(),
        "descriptor-driven link targets must include JSON package targets"
    );
    require_true(
        std::find(core_targets.begin(), core_targets.end(), "Qt6::Core")
            != core_targets.end(),
        "descriptor-driven link targets must include Qt component targets"
    );
    require_true(
        std::find(core_targets.begin(), core_targets.end(), "Qt6::Widgets")
            != core_targets.end(),
        "descriptor-driven link targets must include all declared Qt "
        "component targets"
    );
    require_true(
        std::find(
            core_targets.begin(), core_targets.end(),
            "ecosystem_optional_opencv"
        ) != core_targets.end(),
        "descriptor-driven link targets must include optional OpenCV "
        "support targets"
    );

    const std::vector<std::string> desktop_targets
        = ecosystem::component_package_link_targets(
            manifest_value.components[2]
        );
    require_true(
        std::find(
            desktop_targets.begin(), desktop_targets.end(),
            "ecosystem_kde_support"
        ) != desktop_targets.end(),
        "descriptor-driven link targets must include shared KDE support targets"
    );
    require_true(
        std::find(
            desktop_targets.begin(), desktop_targets.end(),
            "ecosystem_jni_support"
        ) != desktop_targets.end(),
        "descriptor-driven link targets must include JNI support targets"
    );
    require_true(
        std::find(
            desktop_targets.begin(), desktop_targets.end(),
            "ecosystem_llvm_clang_support"
        ) != desktop_targets.end(),
        "descriptor-driven link targets must include LLVM/Clang support targets"
    );
    require_true(
        std::find(desktop_targets.begin(), desktop_targets.end(), "KDEGames6")
            == desktop_targets.end(),
        "descriptor-driven link targets must keep KDEGames inside the "
        "shared KDE support target"
    );

    const std::vector<std::string> test_targets
        = ecosystem::component_package_link_targets(
            manifest_value.components[3]
        );
    require_true(
        std::find(test_targets.begin(), test_targets.end(), "GTest::gtest_main")
            != test_targets.end(),
        "descriptor-driven link targets must derive GTest support from "
        "tests.gtest"
    );

    const std::vector<std::string> benchmark_targets
        = ecosystem::component_package_link_targets(
            manifest_value.components[4]
        );
    require_true(
        std::find(
            benchmark_targets.begin(), benchmark_targets.end(),
            "ecosystem_optional_eigen"
        ) != benchmark_targets.end(),
        "descriptor-driven link targets must include optional Eigen "
        "support targets"
    );
    require_true(
        std::find(
            benchmark_targets.begin(), benchmark_targets.end(),
            "benchmark::benchmark_main"
        ) != benchmark_targets.end(),
        "descriptor-driven link targets must derive benchmark support "
        "from benchmark flags"
    );
}

void test_summarize_dependencies_follows_descriptor_sources() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );

    require_true(
        ecosystem::dependency_summary_has_entries(dependencies),
        "descriptor-driven summaries must report enabled packages"
    );
    require_true(
        dependencies.json.enabled
            && dependencies.json.values.contains("nlohmann_json"),
        "descriptor-driven summaries must collect JSON stack values"
    );
    require_true(
        dependencies.llvm_clang.enabled
            && dependencies.llvm_clang.values.contains("libclang"),
        "descriptor-driven summaries must collect LLVM/Clang stack values"
    );
    require_true(
        dependencies.kde.enabled
            && dependencies.kde.values.contains("CoreAddons"),
        "descriptor-driven summaries must keep non-KDEGames KDE values"
    );
    require_true(
        !dependencies.kde.values.contains("KDEGames6"),
        "descriptor-driven summaries must exclude KDEGames6 from KF6 "
        "component values"
    );
    require_true(
        dependencies.kdegames.enabled
            && dependencies.kdegames.owners.contains("desktop"),
        "descriptor-driven summaries must promote KDEGames6 into its "
        "own package slot"
    );
    require_true(
        dependencies.gtest.enabled
            && dependencies.gtest.owners.contains("tests"),
        "descriptor-driven summaries must derive GTest from tests.gtest"
    );
    require_true(
        dependencies.benchmark.enabled
            && dependencies.benchmark.owners.contains("benchmarks"),
        "descriptor-driven summaries must derive benchmark support from "
        "benchmarks.google_benchmark"
    );
}

void test_summarize_dependencies_collects_cxxopts_stack_values() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_cxxopts_manifest(), std::nullopt
        );

    require_true(
        dependencies.cxxopts.enabled
            && dependencies.cxxopts.values.contains("cxxopts"),
        "descriptor-driven summaries must collect cxxopts stack values"
    );
}

void test_declared_package_sections_group_kde_by_profile() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );

    const std::vector<ecosystem::package_section> debug_sections
        = ecosystem::declared_package_sections(dependencies, false);
    const ecosystem::package_section* debug_required = find_package_section(
        debug_sections, ecosystem::package_group::required
    );
    const ecosystem::package_section* debug_kde = find_package_section(
        debug_sections, ecosystem::package_group::profile_kde
    );
    require_true(
        debug_required != nullptr,
        "declared package catalog must include a required section"
    );
    require_true(
        debug_kde != nullptr,
        "declared package catalog must include a kde profile section"
    );
    require_true(
        find_package_line(*debug_required, "KF6 CONFIG components") == nullptr,
        "non-kde declared package sections must keep KDE requirements "
        "out of the required section"
    );
    require_true(
        find_package_line(*debug_kde, "KF6 CONFIG components") != nullptr,
        "non-kde declared package sections must expose KDE requirements "
        "in the kde profile section"
    );

    const std::vector<ecosystem::package_section> kde_sections
        = ecosystem::declared_package_sections(dependencies, true);
    const ecosystem::package_section* kde_required = find_package_section(
        kde_sections, ecosystem::package_group::required
    );
    require_true(
        kde_required != nullptr,
        "kde declared package sections must include a required section"
    );
    require_true(
        find_package_line(*kde_required, "KF6 CONFIG components") != nullptr,
        "kde declared package sections must move KDE requirements into "
        "the required section"
    );
    require_true(
        find_package_section(
            kde_sections, ecosystem::package_group::profile_kde
        ) == nullptr,
        "kde declared package sections must not emit an empty kde "
        "profile section"
    );
}

void test_configured_package_sections_report_component_and_profile_state() {
    ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );
    dependencies.qt.values.insert("Test");

    ecosystem::cmake_cache_snapshot cache;
    cache.path = "CMakeCache.txt";
    cache.entries["nlohmann_json_DIR"] = "/deps/json";
    cache.entries["LLVM_DIR"] = "/deps/llvm";
    cache.entries["Clang_DIR"] = "/deps/clang";
    cache.entries["Qt6Core_DIR"] = "/deps/qtcore";
    cache.entries["Qt6Widgets_DIR"] = "/deps/qtwidgets";
    cache.entries["JAVA_INCLUDE_PATH"] = "/deps/jni/include";
    cache.entries["JAVA_JVM_LIBRARY"] = "/deps/jni/libjvm.so";
    cache.entries["cxxopts_DIR"] = "/deps/cxxopts";
    cache.entries["Eigen3_DIR"] = "/deps/eigen";
    cache.entries["GTest_DIR"] = "/deps/gtest";
    cache.entries["ECOSYSTEM_PROFILE_KDE"] = "OFF";

    const std::vector<ecosystem::package_status_section> sections
        = ecosystem::configured_package_sections(dependencies, cache);

    const ecosystem::package_status_section* required
        = find_package_status_section(
            sections, ecosystem::package_group::required
        );
    const ecosystem::package_status_section* kde = find_package_status_section(
        sections, ecosystem::package_group::profile_kde
    );
    require_true(
        required != nullptr,
        "configured package catalog must include a required section"
    );
    require_true(
        kde != nullptr,
        "configured package catalog must include a kde profile section"
    );

    const ecosystem::package_status_line* qt
        = find_package_status_line(*required, "Qt6 6.0 CONFIG components");
    require_true(
        qt != nullptr,
        "configured package catalog must include a Qt package line"
    );
    require_true(
        qt->status == "detected Core Widgets; missing Test",
        "configured package catalog must report detected and missing Qt "
        "components together"
    );

    const ecosystem::package_status_line* kde_components
        = find_package_status_line(*kde, "KF6 CONFIG components");
    require_true(
        kde_components != nullptr,
        "configured package catalog must include a KDE package line"
    );
    require_true(
        kde_components->status == "profile disabled in this configure data",
        "configured package catalog must preserve disabled-profile "
        "state for KDE packages"
    );
}

void test_package_descriptors_bind_dependency_ids() {
    ecosystem::dependency_summary dependencies;
    dependencies.qt.enabled = true;
    dependencies.qt.values.insert("Core");
    dependencies.kdegames.enabled = true;

    const ecosystem::package_descriptor* qt_descriptor
        = ecosystem::find_package_descriptor(ecosystem::package_kind::qt);
    const ecosystem::package_descriptor* kdegames_descriptor
        = ecosystem::find_package_descriptor(ecosystem::package_kind::kdegames);
    require_true(qt_descriptor != nullptr, "descriptor lookup must resolve Qt");
    require_true(
        kdegames_descriptor != nullptr,
        "descriptor lookup must resolve KDEGames"
    );

    const ecosystem::dependency_entry& qt_entry
        = ecosystem::package_dependency_entry(dependencies, *qt_descriptor);
    const ecosystem::dependency_entry& kdegames_entry
        = ecosystem::package_dependency_entry(
            dependencies, *kdegames_descriptor
        );
    require_true(
        qt_entry.enabled && qt_entry.values.contains("Core"),
        "descriptor dependency-id lookup must return the matching Qt "
        "dependency entry"
    );
    require_true(
        kdegames_entry.enabled,
        "descriptor dependency ids must keep "
        "distinct package entries for KDEGames"
    );
}

void test_package_descriptors_bind_rule_types() {
    const ecosystem::package_descriptor* qt_descriptor
        = ecosystem::find_package_descriptor(ecosystem::package_kind::qt);
    const ecosystem::package_descriptor* benchmark_descriptor
        = ecosystem::find_package_descriptor(
            ecosystem::package_kind::benchmark
        );
    require_true(
        qt_descriptor != nullptr,
        "descriptor lookup must resolve Qt for rule checks"
    );
    require_true(
        benchmark_descriptor != nullptr,
        "descriptor lookup must resolve benchmark for rule checks"
    );

    require_true(
        qt_descriptor->summary_rules.size() == 2,
        "Qt descriptor must keep both stack and test-derived summary rules"
    );
    require_true(
        qt_descriptor->summary_rules[0].source
            == ecosystem::package_summary_source::component_stack,
        "Qt descriptor must keep stack-driven summary sourcing"
    );
    require_true(
        qt_descriptor->summary_rules[1].source
            == ecosystem::package_summary_source::component_tests_flag,
        "Qt descriptor must keep test-driven summary sourcing for QtTest"
    );
    require_true(
        qt_descriptor->summary_rules[1].effect
                == ecosystem::package_summary_effect::fixed_value
            && qt_descriptor->summary_rules[1].value == "Test",
        "Qt descriptor must keep the fixed QtTest summary value"
    );
    require_true(
        qt_descriptor->link_rules.size() == 1
            && qt_descriptor->link_rules[0].source
                == ecosystem::package_link_target_source::
                    dependency_values_prefix
            && qt_descriptor->link_rules[0].value == "Qt6::",
        "Qt descriptor must keep prefix-based link-target rules"
    );
    require_true(
        qt_descriptor->status_rule.source
            == ecosystem::package_status_source::
                dependency_values_prefix_suffix,
        "Qt descriptor must keep value-derived configured-status rules"
    );
    require_true(
        benchmark_descriptor->summary_rules.size() == 1
            && benchmark_descriptor->summary_rules[0].source
                == ecosystem::package_summary_source::component_benchmarks_flag
            && benchmark_descriptor->summary_rules[0].effect
                == ecosystem::package_summary_effect::owner_only,
        "benchmark descriptor must keep benchmark-flag summary rules"
    );
}

void test_registered_packages_keep_stable_order() {
    const std::vector<ecosystem::package_kind> expected {
        ecosystem::package_kind::nlohmann_json,
        ecosystem::package_kind::llvm_clang,
        ecosystem::package_kind::qt,
        ecosystem::package_kind::kde,
        ecosystem::package_kind::kdegames,
        ecosystem::package_kind::opencv,
        ecosystem::package_kind::eigen,
        ecosystem::package_kind::jni,
        ecosystem::package_kind::cxxopts,
        ecosystem::package_kind::gtest,
        ecosystem::package_kind::benchmark,
    };

    std::vector<ecosystem::package_kind> actual;
    for (const ecosystem::package_descriptor& descriptor :
         ecosystem::registered_packages()) {
        actual.push_back(descriptor.kind);
    }

    require_true(
        actual == expected,
        "shared package registry must keep a stable package order"
    );
}

void test_enabled_surface_blocks_deduplicate_kde_block() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );

    const std::vector<ecosystem::package_surface_block> blocks
        = ecosystem::enabled_surface_blocks(dependencies);
    const std::vector<ecosystem::package_surface_block> expected {
        ecosystem::package_surface_block::nlohmann_json,
        ecosystem::package_surface_block::llvm_clang,
        ecosystem::package_surface_block::qt,
        ecosystem::package_surface_block::kde,
        ecosystem::package_surface_block::opencv,
        ecosystem::package_surface_block::eigen,
        ecosystem::package_surface_block::jni,
        ecosystem::package_surface_block::gtest,
        ecosystem::package_surface_block::benchmark,
    };

    require_true(
        blocks == expected,
        "surface block selection must follow descriptor order and "
        "collapse KDE packages into one block"
    );
}

void test_dependency_entry_for_id_returns_matching_entry() {
    ecosystem::dependency_summary dependencies;
    dependencies.cxxopts.enabled = true;
    dependencies.cxxopts.values.insert("cxxopts");

    const ecosystem::dependency_entry& entry
        = ecosystem::dependency_entry_for_id(
            dependencies, ecosystem::dependency_id::cxxopts
        );
    require_true(
        entry.enabled, "dependency id access must resolve the requested entry"
    );
    require_true(
        entry.values.contains("cxxopts"),
        "dependency id access must preserve the requested dependency values"
    );
}

void test_package_surface_block_order_is_stable() {
    const std::vector<ecosystem::package_surface_block> expected {
        ecosystem::package_surface_block::nlohmann_json,
        ecosystem::package_surface_block::llvm_clang,
        ecosystem::package_surface_block::qt,
        ecosystem::package_surface_block::kde,
        ecosystem::package_surface_block::opencv,
        ecosystem::package_surface_block::eigen,
        ecosystem::package_surface_block::jni,
        ecosystem::package_surface_block::cxxopts,
        ecosystem::package_surface_block::gtest,
        ecosystem::package_surface_block::benchmark,
    };
    require_true(
        ecosystem::package_surface_block_order() == expected,
        "shared surface block order must stay stable through the "
        "dedicated block module"
    );
}

void test_package_group_heading_is_stable() {
    require_true(
        ecosystem::package_group_heading(ecosystem::package_group::required)
            == "required",
        "shared package groups must keep the required heading stable"
    );
    require_true(
        ecosystem::package_group_heading(ecosystem::package_group::profile_kde)
            == "profile kde",
        "shared package groups must keep the profile-kde heading stable"
    );
    require_true(
        ecosystem::package_group_heading(ecosystem::package_group::optional)
            == "optional",
        "shared package groups must keep the optional heading stable"
    );
}

void test_configured_status_for_package_reports_profile_disabled_kde() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );

    ecosystem::cmake_cache_snapshot cache;
    cache.path = "CMakeCache.txt";
    cache.entries["ECOSYSTEM_PROFILE_KDE"] = "OFF";

    const std::string status = ecosystem::configured_status_for_package(
        ecosystem::package_kind::kde, dependencies, cache
    );
    require_true(
        status == "profile disabled in this configure data",
        "descriptor helper must preserve disabled-profile KDE status"
    );
}

void test_configured_status_for_package_requires_all_jni_cache_keys() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );

    ecosystem::cmake_cache_snapshot cache;
    cache.path = "CMakeCache.txt";
    cache.entries["JAVA_INCLUDE_PATH"] = "/deps/jni/include";

    const std::string status = ecosystem::configured_status_for_package(
        ecosystem::package_kind::jni, dependencies, cache
    );
    require_true(
        status == "missing",
        "descriptor-driven configured status must require every JNI cache key"
    );
}

void test_configured_status_for_package_detects_cxxopts_dir() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_cxxopts_manifest(), std::nullopt
        );

    ecosystem::cmake_cache_snapshot cache;
    cache.path = "CMakeCache.txt";
    cache.entries["cxxopts_DIR"] = "/deps/cxxopts";

    const std::string status = ecosystem::configured_status_for_package(
        ecosystem::package_kind::cxxopts, dependencies, cache
    );
    require_true(
        status == "detected",
        "descriptor-driven configured status must "
        "detect cxxopts from cxxopts_DIR"
    );
}

void test_find_package_surface_rule_returns_qt_rule() {
    const ecosystem::package_surface_rule* rule
        = ecosystem::find_package_surface_rule(
            ecosystem::package_surface_block::qt
        );
    require_true(
        rule != nullptr, "surface-rule lookup must return the Qt rule"
    );
    require_true(
        rule->source == ecosystem::package_surface_source::component_rule,
        "surface-rule lookup must preserve the Qt rule source"
    );
    require_true(
        rule->component_rule.find_package_open_line
            == "find_package(Qt6 6.0 CONFIG REQUIRED COMPONENTS",
        "surface-rule lookup must preserve the Qt package rule data"
    );
}

void test_render_package_surface_block_keeps_header_only_json_cross_compilable() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );
    const ecosystem::package_surface_options options;

    const std::string block = ecosystem::render_package_surface_block(
        ecosystem::package_surface_block::nlohmann_json, dependencies, options
    );
    require_contains(
        block, "if (ANDROID)",
        "header-only JSON discovery must distinguish Android cross-compiles"
    );
    require_contains(
        block,
        "find_package(nlohmann_json 3.12 CONFIG REQUIRED "
        "NO_CMAKE_FIND_ROOT_PATH)",
        "Android must be allowed to use the architecture-independent host "
        "nlohmann_json package"
    );
    require_contains(
        block,
        "file(COPY "
        "\"${ECOSYSTEM_NLOHMANN_JSON_INCLUDE_DIR}/nlohmann\"",
        "Android must stage host header-only JSON files inside the "
        "cross-compile build tree"
    );
    require_contains(
        block,
        "target_include_directories(nlohmann_json::nlohmann_json INTERFACE "
        "\"${ECOSYSTEM_NLOHMANN_JSON_STAGE_DIR}\")",
        "the imported JSON target must expose only the staged Android-safe "
        "include directory"
    );
    require_contains(
        block, "find_package(nlohmann_json 3.12 CONFIG REQUIRED)",
        "desktop JSON discovery must keep its existing package requirement"
    );
}

void test_render_package_surface_block_renders_kde_support() {
    ecosystem::manifest manifest_value = sample_dependency_manifest();
    manifest_value.components.at(2).stack["kde"].push_back("KIOCore");
    manifest_value.components.at(2).stack["kde"].push_back("XmlGui");
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(manifest_value, std::nullopt);

    ecosystem::package_surface_options options;
    options.support_targets = true;
    options.emit_profile_option_lines = true;

    const std::string block = ecosystem::render_package_surface_block(
        ecosystem::package_surface_block::kde, dependencies, options
    );
    require_contains(
        block, "option(ECOSYSTEM_PROFILE_KDE",
        "descriptor surface helper must emit the KDE profile option "
        "when requested"
    );
    require_contains(
        block, "find_package(KF6CoreAddons CONFIG REQUIRED)",
        "descriptor surface helper must emit independently discoverable KF6 "
        "package requirements"
    );
    require_contains(
        block, "find_package(KF6XmlGui CONFIG REQUIRED)",
        "descriptor surface helper must preserve every requested KF6 package"
    );
    require_contains(
        block, "find_package(KF6KIO CONFIG REQUIRED)",
        "descriptor surface helper must map KIO targets to their shared KF6KIO "
        "package"
    );
    require_not_contains(
        block, "find_package(KF6KIOCore",
        "descriptor surface helper must not treat a KIO target as a package"
    );
    require_not_contains(
        block, "find_package(KF6 CONFIG REQUIRED COMPONENTS",
        "descriptor surface helper must not require a nonexistent KF6 "
        "umbrella package"
    );
    require_contains(
        block, "find_package(KDEGames6 REQUIRED)",
        "descriptor surface helper must emit KDEGames6 requirements"
    );
    require_contains(
        block, "add_library(ecosystem_kde_support INTERFACE)",
        "descriptor surface helper must emit the shared KDE support "
        "target when requested"
    );
    require_contains(
        block, "target_link_libraries(ecosystem_kde_support INTERFACE",
        "profile surface rules must emit the shared KDE support target body"
    );
    require_contains(
        block, "            KF6::CoreAddons",
        "profile surface rules must emit derived KF6 support target links"
    );
    require_contains(
        block, "            KDEGames6\n",
        "profile surface rules must preserve gated KDEGames6 support links"
    );
}

void test_render_package_surface_block_renders_qt_support() {
    const ecosystem::manifest manifest_value = sample_qttest_manifest();
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(manifest_value, std::nullopt);

    ecosystem::package_surface_options options;
    options.qt_automation = true;

    const std::string block = ecosystem::render_package_surface_block(
        ecosystem::package_surface_block::qt, dependencies, options
    );
    require_contains(
        block, "set(QT_DEFAULT_MAJOR_VERSION 6)",
        "component surface rules must emit the shared Qt preamble"
    );
    require_contains(
        block, "find_package(Qt6 6.0 CONFIG REQUIRED COMPONENTS",
        "component surface rules must emit the Qt package lookup"
    );
    require_contains(
        block, "        Test\n",
        "component surface rules must include the derived Qt Test component"
    );
    require_contains(
        block, "set(CMAKE_AUTOMOC ON)",
        "component surface rules must emit Qt automation lines when requested"
    );
}

void test_render_package_surface_block_renders_llvm_support() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );

    ecosystem::package_surface_options options;
    options.support_targets = true;

    const std::string block = ecosystem::render_package_surface_block(
        ecosystem::package_surface_block::llvm_clang, dependencies, options
    );
    require_contains(
        block, "find_package(LLVM CONFIG REQUIRED)",
        "surface block dispatch must render LLVM package requirements"
    );
    require_contains(
        block, "find_package(Clang CONFIG REQUIRED)",
        "surface block dispatch must render Clang package requirements"
    );
    require_contains(
        block, "add_library(ecosystem_llvm_clang_support INTERFACE)",
        "surface block dispatch must render the LLVM/Clang support target"
    );
    require_contains(
        block,
        "target_include_directories(ecosystem_llvm_clang_support SYSTEM "
        "INTERFACE",
        "surface block dispatch must render LLVM/Clang include directories"
    );
}

void test_render_package_surface_block_renders_opencv_support() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );

    ecosystem::package_surface_options options;
    options.support_targets = true;

    const std::string block = ecosystem::render_package_surface_block(
        ecosystem::package_surface_block::opencv, dependencies, options
    );
    require_contains(
        block, "find_package(OpenCV QUIET)",
        "shared static surface rules must emit OpenCV package lookup"
    );
    require_contains(
        block, "add_library(ecosystem_optional_opencv INTERFACE)",
        "shared static surface rules must emit the OpenCV support target"
    );
    require_contains(
        block, "if (OpenCV_FOUND)",
        "shared static surface rules must preserve the OpenCV "
        "availability guard"
    );
    require_contains(
        block,
        "target_include_directories(ecosystem_optional_opencv INTERFACE "
        "${OpenCV_INCLUDE_DIRS})",
        "shared static surface rules must emit OpenCV include directories"
    );
}

void test_render_package_surface_block_renders_cxxopts_support() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_cxxopts_manifest(), std::nullopt
        );

    const std::string block = ecosystem::render_package_surface_block(
        ecosystem::package_surface_block::cxxopts, dependencies,
        ecosystem::package_surface_options {}
    );
    require_contains(
        block, "find_package(cxxopts CONFIG REQUIRED)",
        "shared static surface rules must emit the cxxopts package lookup"
    );
    require_contains(
        block, "find_package(cxxopts CONFIG REQUIRED NO_CMAKE_FIND_ROOT_PATH)",
        "Android cross-compiles must be allowed to use the "
        "architecture-independent host cxxopts package"
    );
    require_contains(
        block,
        "file(COPY \"${ECOSYSTEM_CXXOPTS_INCLUDE_DIR}/cxxopts.hpp\"",
        "Android must stage the host cxxopts header inside the cross-compile "
        "build tree"
    );
    require_contains(
        block,
        "target_include_directories(cxxopts::cxxopts INTERFACE "
        "\"${ECOSYSTEM_CXXOPTS_STAGE_DIR}\")",
        "the imported cxxopts target must expose the staged Android-safe "
        "include directory"
    );
}

} // namespace ecosystem_test_support
