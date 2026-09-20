#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

void test_mutate_add_module_updates_manifest_and_files() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    const ecosystem::mutation_report report = ecosystem::add_module(
        root.path(), &manifest_value, "sample", "sample/core"
    );
    require_true(report.errors.empty(), "add_module must succeed");
    require_true(
        report.changed_manifest, "add_module must update the manifest"
    );
    require_true(
        fs::exists(root.path() / "include/sample/core.hpp"),
        "header must be scaffolded"
    );
    require_true(
        fs::exists(root.path() / "src/sample/core.cpp"),
        "source must be scaffolded"
    );
}

void test_mutate_add_component_scaffolds_templates() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    ecosystem::mutation_report report
        = ecosystem::add_component(root.path(), &manifest_value, "core");
    require_true(
        report.errors.empty(), "default component template must succeed"
    );
    const ecosystem::component* core
        = find_component_value(manifest_value, "core");
    require_true(
        core != nullptr, "default component template must add a component"
    );
    require_true(
        core->artifacts.size() == 1U,
        "default component template must add one artifact"
    );
    require_true(
        core->artifacts.front().kind == "static_lib",
        "default component template must produce a static library artifact"
    );
    require_true(
        core->artifacts.front().id == "core",
        "default component template must keep the component id as the "
        "library artifact id"
    );
    require_true(
        core->modules == std::vector<std::string> { "core" },
        "default component template must scaffold a default module"
    );
    require_true(
        fs::exists(root.path() / "core/include/core.hpp"),
        "default component header must exist"
    );
    require_true(
        fs::exists(root.path() / "core/src/core.cpp"),
        "default component source must exist"
    );
    require_contains(
        read_text(root.path() / "core/src/core.cpp"), "#include \"core.hpp\"",
        "default component source must include the scaffolded header"
    );

    report = ecosystem::add_component(
        root.path(), &manifest_value, "headers", "interface_lib"
    );
    require_true(
        report.errors.empty(), "interface component template must succeed"
    );
    const ecosystem::component* headers
        = find_component_value(manifest_value, "headers");
    require_true(
        headers != nullptr, "interface component template must add a component"
    );
    require_true(
        headers->artifacts.front().kind == "interface_lib",
        "interface component template must produce an interface artifact"
    );
    require_true(
        headers->artifacts.front().id == "headers",
        "interface component template must keep the component id as the "
        "interface artifact id"
    );
    require_true(
        headers->file_units.size() == 1U
            && headers->file_units.front().id == "headers"
            && headers->file_units.front().kind == "header_only",
        "interface component template must declare a header-only file unit"
    );
    require_true(
        fs::exists(root.path() / "headers/include/headers.hpp"),
        "interface component header must exist"
    );

    report = ecosystem::add_component(
        root.path(), &manifest_value, "widget_shell", "qt_app"
    );
    require_true(
        report.errors.empty(), "Qt app component template must succeed"
    );
    const ecosystem::component* widget_shell
        = find_component_value(manifest_value, "widget_shell");
    require_true(
        widget_shell != nullptr,
        "Qt app component template must add a component"
    );
    require_true(
        widget_shell->artifacts.front().kind == "qt_app",
        "Qt app component template must produce a Qt app artifact"
    );
    require_true(
        widget_shell->artifacts.front().id == "app",
        "Qt app component template must default to an app artifact id"
    );
    require_true(
        widget_shell->stack.at("qt")
            == ecosystem::json::array({ "Core", "Gui", "Widgets" }),
        "Qt app component template must declare the default Qt stack"
    );
    require_true(
        widget_shell->file_units.size() == 1U
            && widget_shell->file_units.front().id == "main"
            && widget_shell->file_units.front().kind == "source_only",
        "Qt app component template must declare a main source file"
    );
    require_contains(
        read_text(root.path() / "widget_shell/src/main.cpp"),
        "#include <QApplication>",
        "Qt app component template must scaffold a QApplication entrypoint"
    );

    report = ecosystem::add_component(
        root.path(), &manifest_value, "suite", "tests"
    );
    require_true(report.errors.empty(), "test component template must succeed");
    const ecosystem::component* suite
        = find_component_value(manifest_value, "suite");
    require_true(
        suite != nullptr, "test component template must add a component"
    );
    require_true(
        suite->tests.at("gtest").get<bool>(),
        "test component template must enable gtest support"
    );
    require_true(
        suite->artifacts.front().id == "tests",
        "test component template must default to a tests artifact id"
    );
    require_true(
        suite->modules.empty() && suite->file_units.size() == 1U
            && suite->file_units.front().id == "tests/test_main",
        "test component template must stay file-unit based"
    );
    require_contains(
        read_text(root.path() / "suite/tests/test_main.cpp"), "RUN_ALL_TESTS()",
        "test component template must scaffold a gtest entrypoint"
    );

    report = ecosystem::add_component(
        root.path(), &manifest_value, "perf", "benchmarks"
    );
    require_true(
        report.errors.empty(), "benchmark component template must succeed"
    );
    const ecosystem::component* perf
        = find_component_value(manifest_value, "perf");
    require_true(
        perf != nullptr, "benchmark component template must add a component"
    );
    require_true(
        perf->benchmarks.at("google_benchmark").get<bool>(),
        "benchmark component template must enable benchmark support"
    );
    require_true(
        perf->artifacts.front().id == "bench",
        "benchmark component template must default to a bench artifact id"
    );
    require_true(
        perf->modules.empty() && perf->file_units.size() == 1U
            && perf->file_units.front().id == "benchmarks/bench_main",
        "benchmark component template must stay file-unit based"
    );
    require_contains(
        read_text(root.path() / "perf/benchmarks/bench_main.cpp"),
        "BENCHMARK_MAIN()",
        "benchmark component template must scaffold a benchmark entrypoint"
    );
}

void test_mutate_add_component_supports_custom_artifact_ids_and_links() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_library_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    ecosystem::add_component_options tool_options;
    tool_options.template_kind = "exe";
    tool_options.artifact_id = "cli";
    tool_options.artifact_links = { ecosystem::artifact_ref { "core", "lib" } };

    ecosystem::mutation_report report = ecosystem::add_component(
        root.path(), &manifest_value, "tool", tool_options
    );
    require_true(
        report.errors.empty(),
        "custom artifact-id component mutation must succeed"
    );
    const ecosystem::component* tool
        = find_component_value(manifest_value, "tool");
    require_true(
        tool != nullptr,
        "custom artifact-id component mutation must add a component"
    );
    require_true(
        tool->artifacts.front().id == "cli",
        "custom artifact ids must be preserved in scaffolded components"
    );
    require_true(
        tool->artifacts.front().link == ecosystem::string_list { "core:lib" },
        "explicit artifact links must be preserved in scaffolded components"
    );
    require_true(
        fs::exists(root.path() / "tool/src/main.cpp"),
        "custom artifact-id component mutation must still scaffold files"
    );

    ecosystem::add_component_options tests_options;
    tests_options.template_kind = "tests";

    report = ecosystem::add_component(
        root.path(), &manifest_value, "suite", tests_options
    );
    require_true(
        report.errors.empty(),
        "test component mutation must infer a default link when unambiguous"
    );
    const ecosystem::component* suite
        = find_component_value(manifest_value, "suite");
    require_true(
        suite != nullptr, "test component mutation must add a test component"
    );
    require_true(
        suite->artifacts.front().id == "tests",
        "test component mutation must keep the default tests artifact id"
    );
    require_true(
        suite->artifacts.front().link == ecosystem::string_list { "core:lib" },
        "test component mutation must default-link to the single library facade"
    );

    ecosystem::add_component_options benchmark_options;
    benchmark_options.template_kind = "benchmarks";

    report = ecosystem::add_component(
        root.path(), &manifest_value, "perf", benchmark_options
    );
    require_true(
        report.errors.empty(),
        "benchmark component mutation must infer "
        "a default link when unambiguous"
    );
    const ecosystem::component* perf
        = find_component_value(manifest_value, "perf");
    require_true(
        perf != nullptr,
        "benchmark component mutation must add a benchmark component"
    );
    require_true(
        perf->artifacts.front().id == "bench",
        "benchmark component mutation must keep the default bench artifact id"
    );
    require_true(
        perf->artifacts.front().link == ecosystem::string_list { "core:lib" },
        "benchmark component mutation must default-link to the single "
        "library facade"
    );
}

void test_mutate_add_component_infers_facade_library_closure() {
    temp_dir root;
    ecosystem::manifest manifest_value
        = sample_facade_library_closure_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    ecosystem::add_component_options options;
    options.template_kind = "tests";
    const ecosystem::mutation_report report = ecosystem::add_component(
        root.path(), &manifest_value, "suite", options
    );
    require_true(
        report.errors.empty(),
        "test component mutation must infer the facade-library closure"
    );

    const ecosystem::component* suite
        = find_component_value(manifest_value, "suite");
    require_true(
        suite != nullptr,
        "facade-library closure inference must add the new component"
    );
    require_true(
        suite->artifacts.front().link
            == ecosystem::string_list { "api:api", "core:core" },
        "default links must include the facade library and its transitive "
        "library dependencies without unrelated spare libraries"
    );
}

void test_mutate_add_component_infers_facade_component_library_closure() {
    temp_dir root;
    ecosystem::manifest manifest_value
        = sample_facade_component_library_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    ecosystem::add_component_options options;
    options.template_kind = "exe";
    const ecosystem::mutation_report report = ecosystem::add_component(
        root.path(), &manifest_value, "tool", options
    );
    require_true(
        report.errors.empty(),
        "runnable component mutation must infer the facade-component "
        "library closure"
    );

    const ecosystem::component* tool
        = find_component_value(manifest_value, "tool");
    require_true(
        tool != nullptr,
        "facade-component closure inference must add the new component"
    );
    require_true(
        tool->artifacts.front().link
            == ecosystem::string_list { "frontend:ui", "backend_core:core" },
        "default links must include facade-component libraries and their "
        "transitive library dependencies"
    );
}

void test_mutate_add_component_infers_direct_runnable_library_links() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_shared_runtime_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    ecosystem::add_component_options options;
    options.template_kind = "benchmarks";
    const ecosystem::mutation_report report = ecosystem::add_component(
        root.path(), &manifest_value, "perf", options
    );
    require_true(
        report.errors.empty(),
        "benchmark component mutation must infer direct runnable-library links"
    );

    const ecosystem::component* perf
        = find_component_value(manifest_value, "perf");
    require_true(
        perf != nullptr,
        "direct runnable-library inference must add the new component"
    );
    require_true(
        perf->artifacts.front().link == ecosystem::string_list { "core:lib" },
        "default links must follow direct runnable-library links and ignore "
        "unrelated spare libraries"
    );
}

void test_mutate_add_file_unit_supports_h_variants() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    ecosystem::mutation_report report = ecosystem::add_file_unit(
        root.path(), &manifest_value, "sample", "sample/detail", "header_only_h"
    );
    require_true(report.errors.empty(), "header_only_h file unit must succeed");
    require_true(
        fs::exists(root.path() / "include/sample/detail.h"),
        "C header must be scaffolded"
    );

    report = ecosystem::add_file_unit(
        root.path(), &manifest_value, "sample", "sample/native_bridge",
        "source_pair_h"
    );
    require_true(report.errors.empty(), "source_pair_h file unit must succeed");
    require_true(
        fs::exists(root.path() / "include/sample/native_bridge.h"),
        "paired .h header must exist"
    );
    require_true(
        fs::exists(root.path() / "src/sample/native_bridge.cpp"),
        "paired .cpp source must exist"
    );
}

void test_cli_mutate_add_component_supports_artifact_id_and_link_options() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_multi_library_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    const cli_result result = run_cli(
        root.path(),
        "mutate add component widget_shell --kind qt_app "
        "--artifact-id desktop_app --link support:helper"
    );
    require_true(
        result.exit_code == 0,
        "mutate add component must accept artifact-id and link options"
    );
    require_contains(
        result.output, "updated manifest.json",
        "mutate add component must save the manifest"
    );
    require_contains(
        result.output, "wrote widget_shell/src/main.cpp",
        "mutate add component must scaffold files"
    );

    const ecosystem::manifest_report report
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        report.errors.empty(),
        "mutate add component must keep the manifest valid"
    );
    require_true(
        report.value.has_value(),
        "mutate add component must persist the manifest"
    );
    const ecosystem::component* widget_shell
        = find_component_value(*report.value, "widget_shell");
    require_true(
        widget_shell != nullptr,
        "mutate add component must persist the new component"
    );
    require_true(
        widget_shell->artifacts.front().kind == "qt_app",
        "mutate add component --kind qt_app must persist the selected "
        "artifact kind"
    );
    require_true(
        widget_shell->artifacts.front().id == "desktop_app",
        "mutate add component must persist a custom artifact id"
    );
    require_true(
        widget_shell->artifacts.front().link
            == ecosystem::string_list { "support:helper" },
        "mutate add component must persist explicit artifact links "
        "instead of inferred defaults"
    );
    require_true(
        widget_shell->stack.at("qt")
            == ecosystem::json::array({ "Core", "Gui", "Widgets" }),
        "mutate add component --kind qt_app must persist the default Qt stack"
    );
    require_contains(
        read_text(root.path() / "widget_shell/src/main.cpp"),
        "QApplication application",
        "mutate add component --kind qt_app must scaffold the Qt entrypoint"
    );
}

} // namespace ecosystem_test_support
