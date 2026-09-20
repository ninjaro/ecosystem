#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

void test_cli_report_matrix() {
    const cli_result result
        = run_cli(fs::path(ECOS_TEST_SOURCE_DIR), "report matrix");
    require_true(result.exit_code == 0, "manifesto report matrix must succeed");
    const json report = json::parse(result.output);
    require_true(
        report.at("project").get<std::string>() == "manifesto",
        "matrix report project id must match"
    );
    require_true(
        report.at("artifacts")
            .at(0)
            .at("build_profiles")
            .at("debug")
            .get<bool>(),
        "debug profile must be supported by the self-hosted project"
    );
}

void test_cli_workspace_report_matrix_records_group_selection() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    write_sample_workspace_project(root.path(), "gamma");
    write_sample_workspace_config(root.path());

    const cli_result result
        = run_cli(root.path(), "report matrix --group core");
    require_true(
        result.exit_code == 0,
        "workspace ecos report matrix must accept named workspace groups"
    );

    const json report = json::parse(result.output);
    require_true(
        report.contains("selection"),
        "workspace matrix report must describe a named group selection"
    );
    require_true(
        report.at("selection").at("group").get<std::string>() == "core",
        "workspace matrix selection must retain the requested group name"
    );
    require_true(
        report.at("projects").size() == 2U,
        "workspace matrix group selection must keep only the grouped projects"
    );
}

void test_cli_workspace_report_matrix() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    const cli_result result = run_cli(root.path(), "report matrix");
    require_true(result.exit_code == 0, "workspace matrix must succeed");
    const json report = json::parse(result.output);
    require_true(
        report.at("projects").size() == 2U,
        "workspace matrix must include exactly the fixture projects"
    );
    std::set<std::string> identities;
    for (const auto& project : report.at("projects"))
        identities.insert(project.at("project").get<std::string>());
    require_true(
        identities == std::set<std::string> { "alpha", "beta" },
        "workspace matrix must preserve fixture identities"
    );
}

void test_cli_workspace_report_matrix_supports_filters() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");

    const cli_result result
        = run_cli(root.path(), "report matrix alpha/alpha:app");
    require_true(
        result.exit_code == 0,
        "workspace ecos report matrix must support qualified artifact filters"
    );

    const json report = json::parse(result.output);
    require_true(
        report.contains("selection"),
        "filtered workspace matrix report must describe its selection"
    );
    require_true(
        report.at("projects").size() == 1U,
        "filtered workspace matrix report must contain one project"
    );
    require_true(
        report.at("selection").at("project").get<std::string>() == "alpha",
        "workspace matrix selection must resolve the target project"
    );
    require_true(
        report.at("selection").at("artifact").get<std::string>() == "alpha:app",
        "workspace matrix selection must retain the requested artifact"
    );
    require_true(
        report.at("projects").at(0).at("project").get<std::string>() == "alpha",
        "workspace matrix filter must keep the selected project"
    );
    require_true(
        report.at("projects").at(0).at("artifacts").size() == 1U,
        "workspace matrix artifact filter must keep one artifact"
    );
    require_true(
        report.at("projects")
                .at(0)
                .at("artifacts")
                .at(0)
                .at("ref")
                .get<std::string>()
            == "alpha:app",
        "workspace matrix artifact filter must keep the requested artifact"
    );
}

void test_cli_workspace_report_matrix_supports_multiple_artifact_filters() {
    temp_dir root;
    write_sample_dual_run_workspace_project(root.path(), "alpha");
    write_sample_dual_run_workspace_project(root.path(), "beta");

    const cli_result result
        = run_cli(root.path(), "report matrix alpha/app:app beta/tool:cli");
    require_true(
        result.exit_code == 0,
        "workspace ecos report matrix must support multiple artifact filters"
    );

    const json report = json::parse(result.output);
    require_true(
        report.contains("selection"),
        "multi-artifact workspace matrix report must describe its selection"
    );
    require_true(
        report.at("selection").contains("artifacts"),
        "multi-artifact workspace matrix selection must list artifacts"
    );
    require_true(
        report.at("selection").at("artifacts").size() == 2U,
        "multi-artifact workspace matrix selection must keep two artifacts"
    );
    require_true(
        report.at("selection")
                .at("artifacts")
                .at(0)
                .at("project")
                .get<std::string>()
            == "alpha",
        "multi-artifact workspace matrix selection must keep the alpha project"
    );
    require_true(
        report.at("selection")
                .at("artifacts")
                .at(1)
                .at("artifact")
                .get<std::string>()
            == "tool:cli",
        "multi-artifact workspace matrix selection must keep the beta tool "
        "artifact"
    );
    require_true(
        report.at("projects").size() == 2U,
        "multi-artifact workspace matrix report must contain both projects"
    );
    require_true(
        report.at("projects").at(0).at("artifacts").size() == 1U,
        "multi-artifact workspace matrix report must filter alpha to one "
        "artifact"
    );
    require_true(
        report.at("projects").at(1).at("artifacts").size() == 1U,
        "multi-artifact workspace matrix report must filter beta to one "
        "artifact"
    );
}

void test_cli_workspace_report_matrix_supports_multi_project_filters() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    write_sample_workspace_project(root.path(), "gamma");

    const cli_result result
        = run_cli(root.path(), "report matrix --project alpha --project beta");
    require_true(
        result.exit_code == 0,
        "workspace ecos report matrix must support multiple project filters"
    );

    const json report = json::parse(result.output);
    require_true(
        report.contains("selection"),
        "multi-project workspace matrix report must describe its selection"
    );
    require_true(
        report.at("selection").contains("projects"),
        "multi-project selection must list projects"
    );
    require_true(
        report.at("selection").at("projects").size() == 2U,
        "multi-project selection must keep two projects"
    );
    require_true(
        report.at("projects").size() == 2U,
        "multi-project workspace matrix report must contain two projects"
    );

    std::set<std::string> projects;
    for (const json& project : report.at("projects")) {
        projects.insert(project.at("project").get<std::string>());
    }
    require_true(
        projects.contains("alpha"),
        "multi-project workspace matrix report must include alpha"
    );
    require_true(
        projects.contains("beta"),
        "multi-project workspace matrix report must include beta"
    );
    require_true(
        !projects.contains("gamma"),
        "multi-project workspace matrix report must exclude gamma"
    );
}

void test_render_benchmark_svg_uses_template_backed_surface() {
    ecosystem::benchmark_summary summary;
    summary.series = {
        {
            "native<fast>",
            {
                { 32, 1.5 },
                { 64, 3.0 },
            },
        },
        {
            "transpose & swap",
            {
                { 32, 1.2 },
                { 64, 2.4 },
            },
        },
    };

    std::string error_message;
    const std::string svg = ecosystem::render_benchmark_svg(
        summary, "sample <plot> & run", &error_message
    );
    require_true(
        error_message.empty(),
        "benchmark SVG rendering must succeed with shared templates"
    );
    require_contains(
        svg, "sample &lt;plot&gt; &amp; run",
        "benchmark SVG rendering must escape the plot title"
    );
    require_contains(
        svg, "native&lt;fast&gt;",
        "benchmark SVG rendering must escape legend labels"
    );
    require_contains(
        svg, "transpose &amp; swap",
        "benchmark SVG rendering must preserve all series labels"
    );
    require_contains(
        svg, "<polyline",
        "benchmark SVG rendering must keep the plotted series line"
    );
    require_contains(
        svg, "<circle", "benchmark SVG rendering must keep the point markers"
    );
}

void test_cli_doctor_reports_declared_dependency_guidance() {
    temp_dir root;
    write_sample_dependency_project(root.path());
    require_sync_success(
        root.path(), "sample dependency project must sync before doctor"
    );
    write_sample_cmake_cache(
        root.path(), "debug", "ECOSYSTEM_PROFILE_KDE:BOOL=OFF\n"
    );

    const cli_result result = run_cli(root.path(), "doctor");
    require_true(
        result.exit_code == 0,
        "ecos doctor must succeed on a complete sample dependency project"
    );
    require_contains(
        result.output, "declared dependencies:",
        "doctor must describe declared dependency guidance"
    );
    require_contains(
        result.output, "nlohmann_json 3.12 CONFIG package",
        "doctor must report manifest-declared JSON package requirements"
    );
    require_contains(
        result.output, "LLVM CONFIG + Clang CONFIG + libclang target",
        "doctor must report manifest-declared LLVM/Clang requirements"
    );
    require_contains(
        result.output, "Qt6 6.0 CONFIG components: Core Widgets",
        "doctor must report manifest-declared Qt requirements"
    );
    require_contains(
        result.output, "KF6 CONFIG components: CoreAddons",
        "doctor must report manifest-declared KDE requirements"
    );
    require_contains(
        result.output, "KDEGames6 package",
        "doctor must report manifest-declared KDEGames6 requirements"
    );
    require_contains(
        result.output, "JNI package",
        "doctor must report manifest-declared JNI requirements"
    );
    require_contains(
        result.output, "OpenCV package",
        "doctor must report manifest-declared optional OpenCV guidance"
    );
    require_contains(
        result.output, "Eigen3 3.3 package",
        "doctor must report manifest-declared optional Eigen guidance"
    );
    require_contains(
        result.output, "GTest package",
        "doctor must report manifest-declared GTest guidance"
    );
    require_contains(
        result.output, "benchmark package",
        "doctor must report manifest-declared benchmark guidance"
    );
}

void test_cli_doctor_artifact_scope_filters_declared_dependencies() {
    temp_dir root;
    write_sample_dependency_project(root.path());
    require_sync_success(
        root.path(),
        "sample dependency project must sync before artifact-scoped doctor"
    );
    write_sample_cmake_cache(
        root.path(), "debug", "ECOSYSTEM_PROFILE_KDE:BOOL=OFF\n"
    );

    const cli_result result = run_cli(root.path(), "doctor app:app");
    require_true(
        result.exit_code == 0,
        "artifact-scoped ecos doctor must succeed on a complete sample project"
    );
    require_contains(
        result.output, "artifact: app:app",
        "artifact-scoped doctor must report the selected artifact"
    );
    require_contains(
        result.output, "Qt6 6.0 CONFIG components: Core Widgets",
        "artifact-scoped doctor must include dependency closure requirements"
    );
    require_contains(
        result.output, "nlohmann_json 3.12 CONFIG package",
        "artifact-scoped doctor must include linked JSON requirements"
    );
    require_contains(
        result.output, "OpenCV package",
        "artifact-scoped doctor must include linked optional OpenCV guidance"
    );
    require_not_contains(
        result.output, "LLVM CONFIG + Clang CONFIG + libclang target",
        "artifact-scoped doctor must exclude dependencies "
        "outside the selected artifact closure"
    );
    require_not_contains(
        result.output, "KF6 CONFIG components: CoreAddons",
        "artifact-scoped doctor must exclude unrelated KDE guidance"
    );
    require_not_contains(
        result.output, "JNI package",
        "artifact-scoped doctor must exclude unrelated JNI guidance"
    );
    require_not_contains(
        result.output, "GTest package",
        "artifact-scoped doctor must exclude unrelated test-only guidance"
    );
    require_not_contains(
        result.output, "benchmark package",
        "artifact-scoped doctor must exclude unrelated benchmark guidance"
    );
    require_not_contains(
        result.output, "Eigen3 3.3 package",
        "artifact-scoped doctor must exclude unrelated "
        "benchmark dependency guidance"
    );
}

void test_cli_doctor_reports_configured_package_state_from_cache() {
    temp_dir root;
    write_sample_dependency_project(root.path());
    require_sync_success(
        root.path(),
        "sample dependency project must sync before cache-driven doctor"
    );
    write_sample_cmake_cache(
        root.path(), "debug",
        "ECOSYSTEM_PROFILE_KDE:BOOL=OFF\n"
        "Clang_DIR:PATH=/deps/clang\n"
        "LLVM_DIR:PATH=/deps/llvm\n"
        "nlohmann_json_DIR:PATH=/deps/json\n"
        "Qt6Core_DIR:PATH=/deps/qtcore\n"
        "Qt6Widgets_DIR:PATH=/deps/qtwidgets\n"
        "OpenCV_DIR:PATH=OpenCV_DIR-NOTFOUND\n"
        "Eigen3_DIR:PATH=/deps/eigen\n"
        "GTest_DIR:PATH=/deps/gtest\n"
        "benchmark_DIR:PATH=benchmark_DIR-NOTFOUND\n"
        "JAVA_INCLUDE_PATH:PATH=/deps/jni/include\n"
        "JAVA_JVM_LIBRARY:FILEPATH=/deps/jni/libjvm.so\n"
    );

    const cli_result result = run_cli(root.path(), "doctor");
    require_true(
        result.exit_code == 0,
        "ecos doctor must read configure data from the debug cache"
    );
    require_contains(
        result.output,
        "configured package state: "
            + relative_build_cache_path(root.path(), "debug"),
        "doctor must identify the configure cache it inspected"
    );
    require_contains(
        result.output, "nlohmann_json 3.12 CONFIG package: detected",
        "doctor must report detected JSON package state from cache data"
    );
    require_contains(
        result.output, "LLVM CONFIG + Clang CONFIG + libclang target: detected",
        "doctor must report detected LLVM/Clang package state from cache data"
    );
    require_contains(
        result.output, "Qt6 6.0 CONFIG components: detected Core Widgets",
        "doctor must report detected Qt component state from cache data"
    );
    require_contains(
        result.output, "JNI package: detected",
        "doctor must report detected JNI state from cache data"
    );
    require_contains(
        result.output,
        "KF6 CONFIG components: profile disabled in this configure data",
        "doctor must distinguish KDE profile packages from a non-kde cache"
    );
    require_contains(
        result.output,
        "KDEGames6 package: profile disabled in this configure data",
        "doctor must distinguish KDEGames6 from a non-kde cache"
    );
    require_contains(
        result.output, "OpenCV package: missing",
        "doctor must report missing optional OpenCV state from cache data"
    );
    require_contains(
        result.output, "Eigen3 3.3 package: detected",
        "doctor must report detected Eigen state from cache data"
    );
    require_contains(
        result.output, "GTest package: detected",
        "doctor must report detected GTest state from cache data"
    );
    require_contains(
        result.output, "benchmark package: missing",
        "doctor must report missing benchmark state from cache data"
    );
}

void test_cli_doctor_artifact_scope_filters_configured_package_state() {
    temp_dir root;
    write_sample_dependency_project(root.path());
    require_sync_success(
        root.path(),
        "sample dependency project must sync "
        "before artifact-scoped cache doctor"
    );
    write_sample_cmake_cache(
        root.path(), "debug",
        "ECOSYSTEM_PROFILE_KDE:BOOL=OFF\n"
        "nlohmann_json_DIR:PATH=/deps/json\n"
        "Qt6Core_DIR:PATH=/deps/qtcore\n"
        "Qt6Widgets_DIR:PATH=/deps/qtwidgets\n"
        "OpenCV_DIR:PATH=OpenCV_DIR-NOTFOUND\n"
        "Clang_DIR:PATH=/deps/clang\n"
        "LLVM_DIR:PATH=/deps/llvm\n"
        "GTest_DIR:PATH=/deps/gtest\n"
        "JAVA_INCLUDE_PATH:PATH=/deps/jni/include\n"
        "JAVA_JVM_LIBRARY:FILEPATH=/deps/jni/libjvm.so\n"
        "benchmark_DIR:PATH=/deps/benchmark\n"
        "Eigen3_DIR:PATH=/deps/eigen\n"
    );

    const cli_result result = run_cli(root.path(), "doctor app:app");
    require_true(
        result.exit_code == 0, "artifact-scoped doctor must read configure data"
    );
    require_contains(
        result.output,
        "configured package state: "
            + relative_build_cache_path(root.path(), "debug"),
        "artifact-scoped doctor must report the inspected cache"
    );
    require_contains(
        result.output, "nlohmann_json 3.12 CONFIG package: detected",
        "artifact-scoped doctor must include linked JSON package state"
    );
    require_contains(
        result.output, "Qt6 6.0 CONFIG components: detected Core Widgets",
        "artifact-scoped doctor must include linked Qt package state"
    );
    require_contains(
        result.output, "OpenCV package: missing",
        "artifact-scoped doctor must include linked optional OpenCV state"
    );
    require_not_contains(
        result.output, "LLVM CONFIG + Clang CONFIG + libclang target: detected",
        "artifact-scoped doctor must exclude unrelated LLVM/Clang package state"
    );
    require_not_contains(
        result.output, "JNI package: detected",
        "artifact-scoped doctor must exclude unrelated JNI package state"
    );
    require_not_contains(
        result.output, "GTest package: detected",
        "artifact-scoped doctor must exclude unrelated test package state"
    );
    require_not_contains(
        result.output, "benchmark package:",
        "artifact-scoped doctor must exclude unrelated benchmark package state"
    );
    require_not_contains(
        result.output, "Eigen3 3.3 package:",
        "artifact-scoped doctor must exclude unrelated Eigen package state"
    );
}

void test_cli_doctor_uses_requested_kde_cache_for_package_state() {
    temp_dir root;
    write_sample_dependency_project(root.path());
    require_sync_success(
        root.path(),
        "sample dependency project must sync before kde cache doctor"
    );
    write_sample_cmake_cache(
        root.path(), "kde",
        "ECOSYSTEM_PROFILE_KDE:BOOL=ON\n"
        "KF6_DIR:PATH=/deps/kf6\n"
        "KDEGames6_DIR:PATH=KDEGames6_DIR-NOTFOUND\n"
        "Qt6Core_DIR:PATH=/deps/qtcore\n"
        "Qt6Widgets_DIR:PATH=/deps/qtwidgets\n"
    );

    const cli_result result = run_cli(root.path(), "doctor kde");
    require_true(
        result.exit_code == 0,
        "profile-scoped doctor must use the requested profile cache"
    );
    require_contains(
        result.output,
        "configured package state: "
            + relative_build_cache_path(root.path(), "kde"),
        "doctor must use the kde cache for the kde profile"
    );
    require_contains(
        result.output, "KF6 CONFIG components: detected",
        "doctor must report detected KF6 state from the kde cache"
    );
    require_contains(
        result.output, "KDEGames6 package: missing",
        "doctor must report missing KDEGames6 state from the kde cache"
    );
}

void test_cli_doctor_reports_tracked_surface_drift() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );
    write_text(root.path() / "src/main.cpp", "int main() { return 0; }\n");

    const cli_result result = run_cli(root.path(), "doctor");
    require_true(
        result.exit_code != 0, "doctor must reject missing tracked surfaces"
    );
    require_contains(
        result.output, "tracked surfaces out of sync:",
        "doctor must report tracked surface drift"
    );
    require_contains(
        result.output,
        "CMakeLists.txt is missing or unreadable; run `marx sync`",
        "doctor must report a missing tracked CMake facade"
    );
    require_contains(
        result.output, ".gitignore is missing or unreadable; run `marx sync`",
        "doctor must report a missing tracked gitignore"
    );
}

void test_cli_doctor_materializes_missing_configure_cache() {
    temp_dir root;
    write_sample_json_project(root.path());
    require_sync_success(
        root.path(), "json sample project must sync before doctor refresh"
    );

    const fs::path cache_path
        = ecosystem::local_build_cache_path(root.path(), "debug");
    require_true(
        !fs::exists(cache_path),
        "configure cache must start missing for materialization test"
    );

    const cli_result result = run_cli(root.path(), "doctor");
    require_true(
        result.exit_code == 0, "doctor must materialize missing configure cache"
    );
    require_true(
        fs::exists(cache_path),
        "doctor must generate the missing configure cache"
    );
    require_contains(
        result.output,
        "configured package state: "
            + relative_build_cache_path(root.path(), "debug") + " (refreshed)",
        "doctor must report that it refreshed missing configure data"
    );
    require_contains(
        result.output, "nlohmann_json 3.12 CONFIG package: detected",
        "doctor must report configured package state after "
        "materializing the cache"
    );
}

void test_cli_doctor_refreshes_stale_configure_cache() {
    temp_dir root;
    write_sample_json_project(root.path());
    require_sync_success(
        root.path(), "json sample project must sync before stale cache test"
    );

    const fs::path cache_path
        = ecosystem::local_build_cache_path(root.path(), "debug");
    write_sample_cmake_cache(
        root.path(), "debug",
        "nlohmann_json_DIR:PATH=nlohmann_json_DIR-NOTFOUND\n"
    );
    age_file_by_seconds(cache_path, std::chrono::seconds(5));

    ecosystem::manifest manifest_value = sample_json_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must rewrite manifest to make the cache stale"
    );

    const cli_result result = run_cli(root.path(), "doctor");
    require_true(
        result.exit_code == 0, "doctor must refresh stale configure cache"
    );
    require_contains(
        result.output,
        "configured package state: "
            + relative_build_cache_path(root.path(), "debug") + " (refreshed)",
        "doctor must report that stale configure data was refreshed"
    );
    require_contains(
        result.output, "nlohmann_json 3.12 CONFIG package: detected",
        "doctor must replace stale configured package state with refreshed data"
    );
    require_not_contains(
        result.output, "nlohmann_json 3.12 CONFIG package: missing",
        "doctor must not keep stale package-state data after refresh"
    );
}

void test_cli_doctor_artifact_scope_refresh_uses_scoped_probe_cache() {
    temp_dir root;
    write_sample_scoped_probe_project(root.path());
    require_sync_success(
        root.path(),
        "scoped probe sample project must sync before doctor refresh"
    );

    const cli_result result
        = run_cli(root.path(), "doctor sample:app --profile kde");
    require_true(
        result.exit_code == 0,
        "artifact-scoped doctor must use a scoped probe cache when the "
        "project kde cache is unavailable"
    );
    require_contains(
        result.output,
        "configured package state: "
            + relative_doctor_probe_cache_path(
                root.path(), ecosystem::artifact_ref { "sample", "app" }, "kde"
            )
            + " (refreshed)",
        "artifact-scoped doctor must report the scoped probe cache path"
    );
    require_contains(
        result.output, "nlohmann_json 3.12 CONFIG package: detected",
        "artifact-scoped doctor must refresh package state for the "
        "selected artifact closure"
    );
    require_not_contains(
        result.output, "KF6 CONFIG components",
        "artifact-scoped doctor must not pull unrelated KDE "
        "requirements into the selected closure"
    );
}

void test_cli_doctor_refreshes_stale_configure_state_without_cache_changes() {
    temp_dir root;
    write_sample_json_project(root.path());
    require_sync_success(
        root.path(),
        "json sample project must sync before configure-state refresh test"
    );

    const cli_result initial_result = run_cli(root.path(), "doctor");
    require_true(
        initial_result.exit_code == 0,
        "doctor must materialize configure state before the "
        "stale-configure-state regression test"
    );

    const fs::path stamp_path = ecosystem::local_build_dir(root.path(), "debug")
        / ".ecosystem_configured.stamp";
    require_true(
        fs::exists(stamp_path), "doctor refresh must create the configure stamp"
    );
    age_file_by_seconds(stamp_path, std::chrono::seconds(5));

    ecosystem::manifest manifest_value = sample_json_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must rewrite manifest to make the configure state stale"
    );

    const cli_result refreshed_result = run_cli(root.path(), "doctor");
    require_true(
        refreshed_result.exit_code == 0,
        "doctor must refresh stale configure state even when cache "
        "contents do not change"
    );
    require_contains(
        refreshed_result.output,
        "configured package state: "
            + relative_build_cache_path(root.path(), "debug") + " (refreshed)",
        "doctor must report refreshed configure state after "
        "updating the configure stamp"
    );
    require_not_contains(
        refreshed_result.output, "configured package state: unavailable",
        "doctor must not report stale configure state as "
        "unavailable after a successful refresh"
    );
}

void test_cli_report_cxx_preserves_artifact_scope_and_failure_status() {
    temp_dir root;
    scoped_env flags("CXXFLAGS", "-Wno-error=unused-variable");
    write_cxx_report_project(root.path(), "reports");
    const auto report_path = root.path() / ".ecosystem/reports/cxx.json";
    auto result = run_engels_cli(root.path(), "report cxx core:app --json");
    require_true(
        result.exit_code == 0,
        "scoped C++ report must succeed:\n" + result.output
    );
    auto report = json::parse(read_text(report_path));
    require_true(
        json::parse(result.output) == report,
        "fresh CMake output must not contaminate machine-readable stdout"
    );
    require_true(
        report.at("status") == "clean" && report.at("files_analyzed") == 1
            && report.at("artifact") == "core:app"
            && report.at("sources").at(0).at("artifact") == "core:app",
        "report and source must retain full artifact identity without sibling "
        "sources"
    );
    require_not_contains(
        result.output, "other.cpp", "terminal must not widen artifact scope"
    );
    const auto before = read_text(report_path);
    for (const std::string command :
         { "report cxx core:absent", "report matrix core:absent",
           "report toolchains core:app" }) {
        result = run_engels_cli(root.path(), command);
        require_true(
            result.exit_code == 2 && read_text(report_path) == before,
            "invalid report requests must fail before replacing results: "
                + command
        );
    }
    result = run_engels_cli(root.path(), "report cxx core:other");
    report = json::parse(read_text(report_path));
    require_true(
        result.exit_code == 5 && report.at("status") == "failed"
            && !report.at("errors").empty()
            && report.at("total_errors").get<int>() > 0,
        "source parse errors must persist an incomplete report and fail the "
        "operation"
    );
    require_true(
        !report.at("findings").empty(),
        "compiler diagnostics must use structured findings"
    );
    const auto& finding = report.at("findings").at(0);
    require_true(
        finding.at("authority") == "external"
            && finding.at("enforcement") == "required"
            && finding.at("severity") == "error"
            && finding.at("file") == "src/other.cpp" && finding.at("line") == 1
            && finding.at("artifact") == "core:other"
            && finding.at("entity_kind") == "translation_unit",
        "compiler findings must preserve authority, rule context, severity, "
        "location and owner"
    );
    require_contains(
        result.output, finding.at("rule").get<std::string>(),
        "terminal and JSON must identify the same compiler diagnostic"
    );
    require_contains(
        result.output, "unselected artifact",
        "failure must preserve Clang's reason"
    );

    write_text(
        root.path() / "src/main.cpp",
        "int main() { int unused = 0; return 0; }\n"
    );
    result = run_engels_cli(root.path(), "report cxx core:app --json");
    report = json::parse(read_text(report_path));
    require_true(
        result.exit_code == 0 && report.at("status") == "findings"
            && report.at("total_warnings").get<int>() > 0
            && report.at("errors").empty(),
        "successful analysis with warnings must remain distinct from failed "
        "analysis:\n"
            + result.output
    );
    temp_dir failed;
    write_cxx_report_project(failed.path(), "failed_configure");
    write_text(
        failed.path() / ".ecosystem/reports/cxx.json",
        "{\"status\":\"clean\"}\n"
    );
    scoped_env invalid_flags("CXXFLAGS", "-fmanifesto-invalid-compiler-option");
    result = run_engels_cli(failed.path(), "report cxx core:app --json");
    report
        = json::parse(read_text(failed.path() / ".ecosystem/reports/cxx.json"));
    require_true(
        result.exit_code != 0 && report.at("status") == "failed"
            && report.at("artifact") == "core:app"
            && report.at("files_analyzed") == 0,
        "configure failures must replace stale success with a failed scoped "
        "report"
    );
    require_true(
        json::parse(result.output) == report,
        "failed configuration must also emit one complete JSON object"
    );
    require_contains(
        report.at("errors").dump(), "-fmanifesto-invalid-compiler-option",
        "report must retain captured compiler configuration diagnostics"
    );
    require_contains(
        report.at("errors").dump(), "configure failed",
        "failed report must retain the configure failure reason"
    );
}

void test_cli_workspace_report_cxx_preserves_each_selected_owner() {
    temp_dir root;
    write_cxx_report_project(root.path() / "alpha", "alpha");
    write_cxx_report_project(root.path() / "beta", "beta");
    write_text(root.path() / "broken/manifest.json", "invalid JSON\n");
    const auto report_path
        = root.path() / ".ecosystem/reports/workspace_cxx.json";
    auto result = run_engels_cli(root.path(), "report cxx");
    require_true(
        result.exit_code == 2 && !fs::exists(report_path),
        "analysis must preflight selected workspace validity before writing "
        "reports"
    );
    result = run_engels_cli(
        root.path(), "report cxx alpha/core:app beta/core:app --json"
    );
    require_true(
        result.exit_code == 0,
        "qualified workspace C++ reports must succeed:\n" + result.output
    );
    auto report = json::parse(read_text(report_path));
    require_true(
        json::parse(result.output) == report,
        "workspace stdout must equal the complete persisted JSON result"
    );
    require_true(
        report.at("status") == "clean" && report.at("projects").size() == 2,
        "workspace must retain each selected analysis"
    );
    for (const auto& project : report.at("projects"))
        require_true(
            project.at("artifact") == "core:app"
                && project.at("files_analyzed") == 1
                && project.at("sources").at(0).at("artifact") == "core:app",
            "each project result must select the full owner, including shared "
            "namespaces"
        );
    result = run_engels_cli(
        root.path(), "report cxx alpha/core:app beta/core:other"
    );
    report = json::parse(read_text(report_path));
    require_true(
        result.exit_code == 5 && report.at("status") == "failed"
            && report.at("projects").at(0).at("status") == "clean"
            && report.at("projects").at(1).at("status") == "failed",
        "workspace failures must retain successful peers and per-project status"
    );
}

void test_cli_personal_reports_share_findings_and_presentation() {
    temp_dir root;
    write_cxx_report_project(root.path(), "reports");
    auto manifest = json::parse(read_text(root.path() / "manifest.json"));
    manifest["artifacts"][0]["owns"] = json::array({ "include/model.hpp" });
    write_text(root.path() / "manifest.json", manifest.dump(2));
    write_text(
        root.path() / "include/model.hpp",
        "#pragma once\ninline int BadName(int value) { if (value) return 1; "
        "return 0; }\n"
    );
    write_text(
        root.path() / "src/main.cpp",
        "#include \"model.hpp\"\nint main() { return BadName(0); }\n"
    );
    for (const std::string profile : { "naming", "style" }) {
        auto result
            = run_engels_cli(root.path(), "check " + profile + " core:app");
        require_true(
            result.exit_code == 0,
            "personal checks must accept advisory findings"
        );
        const auto path
            = root.path() / (".ecosystem/reports/" + profile + ".json");
        const auto checked = json::parse(read_text(path));
        result = run_engels_cli(
            root.path(), "report " + profile + " core:app --json"
        );
        require_true(
            result.exit_code == 0 && json::parse(result.output) == checked
                && json::parse(read_text(path)) == checked
                && checked.at("status") == "findings",
            "check, report, persisted JSON and stdout must use the same result"
        );
        const auto terminal
            = run_engels_cli(root.path(), "report " + profile + " core:app");
        require_true(
            terminal.exit_code == 0,
            "terminal reporting must accept advisory findings"
        );
        for (const auto& finding : checked.at("findings")) {
            require_true(
                finding.at("authority") == "manifesto"
                    && finding.at("enforcement") == "advisory"
                    && finding.at("severity") == "hint"
                    && !finding.at("category").get<std::string>().empty(),
                "personal diagnostics must expose their policy and severity"
            );
            require_contains(
                terminal.output, finding.at("rule").get<std::string>(),
                "terminal must preserve the rule"
            );
            require_contains(
                terminal.output, finding.at("message").get<std::string>(),
                "terminal must preserve the explanation"
            );
            require_contains(
                terminal.output,
                "artifact=" + finding.at("artifact").get<std::string>(),
                "terminal must preserve full ownership"
            );
            const auto location = finding.at("file").get<std::string>() + ":"
                + std::to_string(finding.at("line").get<unsigned>()) + ":"
                + std::to_string(finding.at("column").get<unsigned>());
            require_contains(
                terminal.output, location,
                "terminal must preserve the source location"
            );
            if (!finding.at("measurements").empty())
                require_contains(
                    terminal.output, finding.at("measurements").dump(),
                    "terminal must preserve measurements"
                );
            if (!finding.at("thresholds").empty())
                require_contains(
                    terminal.output, finding.at("thresholds").dump(),
                    "terminal must preserve thresholds"
                );
            if (profile == "style") {
                require_true(
                    !finding.at("referent").is_null(),
                    "header diagnostics must retain their indexed referent"
                );
                require_contains(
                    terminal.output,
                    "referent=" + finding.at("referent").get<std::string>(),
                    "terminal must identify the referent"
                );
            }
        }
        require_not_contains(
            terminal.output, "other.cpp", "reports must not widen ownership"
        );
        const auto before = read_text(path);
        for (const std::string suffix :
             { "core:absent --json", "core:app --json --json",
               "core:app --unsupported" }) {
            result = run_engels_cli(
                root.path(), "report " + profile + " " + suffix
            );
            require_true(
                result.exit_code == 2 && read_text(path) == before,
                "invalid report requests must preserve the last result"
            );
        }
    }
    auto result
        = run_engels_cli(root.path(), "report naming core:other --json");
    const auto failed = json::parse(result.output);
    require_true(
        result.exit_code == 5 && failed.at("status") == "failed"
            && !failed.at("errors").empty(),
        "personal parser failures must retain a machine-readable result and "
        "fail"
    );
    const auto path = root.path() / ".ecosystem/reports/style.json";
    fs::remove(path);
    fs::create_directory(path);
    result = run_engels_cli(root.path(), "report style core:app --json");
    require_true(
        result.exit_code == 5,
        "unwritable report destinations must fail the operation"
    );
    result = run_engels_cli(root.path(), "report matrix core:app --json");
    require_true(
        result.exit_code == 0
            && json::parse(result.output).at("artifacts").size() == 1,
        "matrix must retain JSON inspection with explicit output selection"
    );
}

void test_cli_workspace_personal_reports_keep_all_selected_results() {
    temp_dir root;
    write_cxx_report_project(root.path() / "alpha", "alpha");
    write_cxx_report_project(root.path() / "beta", "beta");
    write_text(root.path() / "broken/manifest.json", "invalid JSON\n");
    write_text(
        root.path() / "alpha/src/main.cpp",
        "int main() { int BadName = 0; if (BadName) ++BadName; return BadName; "
        "}\n"
    );
    write_text(
        root.path() / "alpha/src/other.cpp", "int main() { return 0; }\n"
    );
    write_text(
        root.path() / "manifesto.workspace.json",
        "{\"groups\":{\"apps\":[\"alpha\",\"beta\"]}}\n"
    );
    for (const std::string kind : { "naming", "style" }) {
        const auto path
            = root.path() / (".ecosystem/reports/workspace_" + kind + ".json");
        auto result = run_engels_cli(root.path(), "report " + kind + " --json");
        require_true(
            result.exit_code == 2 && !fs::exists(path),
            "invalid workspace selection must fail before reporting"
        );
        result = run_engels_cli(
            root.path(),
            "report " + kind
                + " alpha/core:app alpha/core:other beta/core:app --json"
        );
        const auto report = json::parse(result.output);
        require_true(
            result.exit_code == 0 && report == json::parse(read_text(path))
                && report.at("status") == "findings"
                && report.at("projects").size() == 3,
            "workspace reports must retain every artifact request, including "
            "two owners in one project"
        );
        require_true(
            report.at("projects").at(0).at("artifact") == "core:app"
                && report.at("projects").at(1).at("artifact") == "core:other"
                && report.at("projects").at(1).at("status") == "clean",
            "same-project requests must not overwrite each other"
        );
        result = run_engels_cli(
            root.path(), "report " + kind + " --group apps --json"
        );
        const auto failed = json::parse(result.output);
        require_true(
            result.exit_code == 5 && failed.at("status") == "failed"
                && failed.at("projects").at(0).at("status") == "findings"
                && failed.at("projects").at(1).at("status") == "failed",
            "workspace group failures must retain findings and failures "
            "separately"
        );
        const auto terminal
            = run_engels_cli(root.path(), "report " + kind + " alpha/core:app");
        require_contains(
            terminal.output, "alpha (alpha) [core:app]",
            "terminal must qualify project, root and artifact"
        );
        require_contains(
            terminal.output, "workspace_" + kind + ".json",
            "terminal must locate the complete workspace report"
        );
    }
}

} // namespace ecosystem_test_support
