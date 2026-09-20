#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

void test_sync_matches_tracked_surfaces() {
    const ecosystem::manifest_report report = ecosystem::load_manifest(
        fs::path(ECOS_TEST_SOURCE_DIR) / "manifest.json"
    );
    require_true(
        report.errors.empty(), "self manifest must load before sync comparison"
    );
    require_true(report.value.has_value(), "self manifest must be available");

    ecosystem::string_list errors;
    const std::vector<ecosystem::tracked_surface_file> generated_files
        = ecosystem::generate_tracked_surface_files(
            *report.value, fs::path(ECOS_TEST_SOURCE_DIR), &errors
        );
    require_true(
        errors.empty(),
        "tracked surface generation must succeed for self-hosting"
    );

    for (const ecosystem::tracked_surface_file& file_value : generated_files) {
        require_true(
            read_text(fs::path(ECOS_TEST_SOURCE_DIR) / file_value.relative_path)
                == file_value.contents,
            "generated tracked surface must match tracked file: "
                + file_value.relative_path.generic_string()
        );
    }
}

void test_tracked_facade_limits_surface_to_entry_artifact_closure() {
    temp_dir root;
    const ecosystem::manifest manifest_value = sample_dependency_manifest();
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake, "add_library(core__lib STATIC",
        "tracked facade must include the linked library closure"
    );
    require_contains(
        generated_cmake, "add_executable(app__app",
        "tracked facade must include the facade entry artifact"
    );
    require_not_contains(
        generated_cmake, "desktop__support",
        "tracked facade must exclude unrelated non-entry components"
    );
    require_not_contains(
        generated_cmake, "tests__tests",
        "tracked facade must exclude test-only components"
    );
    require_not_contains(
        generated_cmake, "benchmarks__bench",
        "tracked facade must exclude benchmark-only components"
    );
    require_not_contains(
        generated_cmake, "ECOSYSTEM_BUILD_TESTS",
        "tracked facade must not expose developer-only test toggles"
    );
}

void test_developer_surface_materializes_full_build_graph() {
    temp_dir root;
    const ecosystem::manifest manifest_value = sample_dependency_manifest();
    const std::string generated_cmake
        = ecosystem::generate_developer_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake,
        "get_filename_component(ECOSYSTEM_PROJECT_ROOT "
        "\"${CMAKE_CURRENT_SOURCE_DIR}/../..\" ABSOLUTE)",
        "developer surface must anchor generated sources back to "
        "the project root"
    );
    require_contains(
        generated_cmake, "desktop__support",
        "developer surface must include non-facade project components"
    );
    require_contains(
        generated_cmake, "if (ECOSYSTEM_BUILD_TESTS)",
        "developer surface must retain test toggles for ecosystem-owned builds"
    );
    require_contains(
        generated_cmake, "if (ECOSYSTEM_BUILD_BENCHMARKS)",
        "developer surface must retain benchmark toggles for "
        "ecosystem-owned builds"
    );
    require_contains(
        generated_cmake, "${ECOSYSTEM_PROJECT_ROOT}/src/main.cpp",
        "developer surface must reference project-owned sources "
        "through the local root anchor"
    );
}

void test_forbidden_repository_entries_report_legacy_scaffolding() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    write_text(
        root.path() / "scripts/build.sh", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_text(root.path() / "Makefile", "all:\n\t@true\n");
    write_text(root.path() / "Doxyfile", "PROJECT_NAME = legacy\n");
    write_text(root.path() / "cli/run.sh", "#!/usr/bin/env bash\nexit 0\n");
    write_text(root.path() / "cov/index.html", "<html></html>\n");
    write_text(root.path() / "bench/bench_log.txt", "bench\n");
    write_text(root.path() / "java-report/index.html", "<html></html>\n");
    write_text(root.path() / "cmake/legacy.cmake", "# legacy\n");
    write_text(
        root.path() / "frontend/CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.20)\nproject(legacy)\n"
    );

    const ecosystem::string_list issues
        = ecosystem::forbidden_repository_entries(root.path());
    std::ostringstream rendered;
    for (const std::string& issue : issues) {
        rendered << issue << "\n";
    }

    require_contains(
        rendered.str(), "scripts/: script directories are forbidden",
        "repository scan must flag committed script directories"
    );
    require_contains(
        rendered.str(), "Makefile: Makefiles are forbidden",
        "repository scan must flag committed Makefiles"
    );
    require_contains(
        rendered.str(), "Doxyfile: committed Doxyfile is forbidden",
        "repository scan must flag committed Doxyfiles"
    );
    require_contains(
        rendered.str(), "cli/run.sh: committed shell wrappers are forbidden",
        "repository scan must flag committed shell wrappers outside "
        "scripts directories"
    );
    require_contains(
        rendered.str(), "cov/: generated coverage reports are forbidden",
        "repository scan must flag committed coverage reports"
    );
    require_contains(
        rendered.str(), "bench/: generated benchmark reports are forbidden",
        "repository scan must flag committed benchmark reports"
    );
    require_contains(
        rendered.str(), "java-report/: generated Java reports are forbidden",
        "repository scan must flag committed Java report output"
    );
    require_contains(
        rendered.str(), "cmake/legacy.cmake: extra CMake modules are forbidden",
        "repository scan must flag committed auxiliary CMake modules"
    );
    require_contains(
        rendered.str(),
        "frontend/CMakeLists.txt: nested CMakeLists.txt files are forbidden",
        "repository scan must flag nested CMake entry files"
    );
}

void test_forbidden_repository_entries_fall_back_for_gitlink_roots() {
    if (!ecosystem::command_exists("git")) {
        return;
    }

    temp_dir root;
    const fs::path workspace_root = root.path() / "workspace";
    const fs::path project_root = workspace_root / "sample";
    write_sample_build_project(project_root, "sample");
    write_text(
        project_root / "scripts/build.sh", "#!/usr/bin/env bash\nexit 0\n"
    );

    require_true(
        ecosystem::run_command({ "git", "init", "-q" }, workspace_root) == 0,
        "gitlink repository regression test must initialize a temporary "
        "git repo"
    );
    require_true(
        ecosystem::run_command(
            {
                "git",
                "update-index",
                "--add",
                "--cacheinfo",
                "160000,1111111111111111111111111111111111111111,sample",
            },
            workspace_root
        ) == 0,
        "gitlink repository regression test must add a synthetic gitlink entry"
    );

    const ecosystem::string_list issues
        = ecosystem::forbidden_repository_entries(project_root);
    std::ostringstream rendered;
    for (const std::string& issue : issues) {
        rendered << issue << "\n";
    }

    require_contains(
        rendered.str(), "scripts/: script directories are forbidden",
        "gitlink-backed repository scans must fall back to the "
        "filesystem when git lists only the project entry"
    );
}

void test_tracked_surface_generation_uses_github_vars_file() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    write_text(
        root.path() / "manifesto.github.vars.json",
        "{\n"
        "  \"manifesto_repository\": \"example/ecosystem\",\n"
        "  \"manifesto_ref\": \"release/preview\",\n"
        "  \"manifesto_build_parallelism\": \"3\",\n"
        "  \"sphinx_theme\": \"furo\",\n"
        "  \"sphinx_theme_package\": \"furo\",\n"
        "  \"checkout_action\": \"actions/checkout@checkout-pin\",\n"
        "  \"install_qt_action\": \"vendor/qt@qt-pin\",\n"
        "  \"github_script_action\": \"actions/github-script@script-pin\",\n"
        "  \"codeql_action_ref\": \"codeql-pin\",\n"
        "  \"upload_artifact_action\": "
        "\"actions/upload-artifact@upload-pin\",\n"
        "  \"download_artifact_action\": "
        "\"actions/download-artifact@download-pin\",\n"
        "  \"configure_pages_action\": "
        "\"actions/configure-pages@configure-pin\",\n"
        "  \"upload_pages_artifact_action\": "
        "\"actions/upload-pages-artifact@pages-upload-pin\",\n"
        "  \"deploy_pages_action\": \"actions/deploy-pages@pages-deploy-pin\"\n"
        "}\n"
    );

    const ecosystem::manifest_report report
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        report.errors.empty(),
        "sample manifest must load before tracked-surface generation"
    );
    require_true(
        report.value.has_value(),
        "sample manifest must parse before tracked-surface generation"
    );

    ecosystem::string_list errors;
    const std::vector<ecosystem::tracked_surface_file> generated_files
        = ecosystem::generate_tracked_surface_files(
            *report.value, root.path(), &errors
        );
    require_true(
        errors.empty(), "tracked surface generation must accept a vars file"
    );

    const ecosystem::tracked_surface_file* tests_workflow
        = find_tracked_surface_file(
            generated_files, ".github/workflows/tests.yml"
        );
    require_true(
        tests_workflow != nullptr,
        "tracked surface generation must include the tests workflow"
    );
    require_contains(
        tests_workflow->contents, "uses: ./.github/actions/setup-manifesto",
        "tests workflow must use the generated local setup action"
    );
    require_contains(
        tests_workflow->contents, "uses: actions/checkout@checkout-pin",
        "tests workflow must use the configured immutable checkout action"
    );
    require_contains(
        tests_workflow->contents, "uses: actions/upload-artifact@upload-pin",
        "tests workflow must use the configured immutable upload action"
    );
    require_contains(
        tests_workflow->contents, "manifesto-repository: \"example/ecosystem\"",
        "tests workflow must pass the configured ecosystem repository"
    );
    require_contains(
        tests_workflow->contents, "manifesto-ref: \"release/preview\"",
        "tests workflow must pass the configured ecosystem ref"
    );
    require_contains(
        tests_workflow->contents, "workflow_dispatch:",
        "tests workflow must stay branch-friendly through manual dispatch"
    );
    require_contains(
        tests_workflow->contents, "cancel-in-progress: true",
        "tests workflow must cancel superseded branch runs"
    );
    require_contains(
        tests_workflow->contents, "uses: ./.github/actions/run-manifesto-stage",
        "tests workflow must reuse the shared stage runner"
    );
    require_contains(
        tests_workflow->contents,
        "uses: ./.github/actions/publish-manifesto-report",
        "tests workflow must publish the shared CI report surface"
    );
    require_contains(
        tests_workflow->contents,
        "\"${{ steps.manifesto.outputs.engels-binary }}\" check ci",
        "CI must invoke the canonical local aggregate"
    );
    for (const auto* operation :
         { "check naming", "report cxx", "check tidy", "check leaks",
           "check sphinx", "build release", "package", "sync", "git diff" }) {
        require_not_contains(
            tests_workflow->contents, operation,
            "default CI must delegate policy without repairing or expanding it"
        );
    }

    const ecosystem::tracked_surface_file* html_workflow
        = find_tracked_surface_file(
            generated_files, ".github/workflows/html.yml"
        );
    require_true(
        html_workflow != nullptr,
        "tracked surface generation must include the deploy workflow"
    );
    require_contains(
        html_workflow->contents, "check sphinx --theme furo",
        "deploy workflow must forward the configured sphinx theme"
    );
    require_contains(
        html_workflow->contents, "uses: actions/configure-pages@configure-pin",
        "deploy workflow must use the configured Pages setup action"
    );
    require_contains(
        html_workflow->contents,
        "uses: actions/upload-pages-artifact@pages-upload-pin",
        "deploy workflow must use the configured Pages upload action"
    );
    require_contains(
        html_workflow->contents, "uses: actions/deploy-pages@pages-deploy-pin",
        "deploy workflow must use the configured Pages deployment action"
    );
    require_not_contains(
        html_workflow->contents, "Checkout manifesto tool",
        "deploy workflow must stay thin and rely on the shared setup action"
    );
    require_contains(
        html_workflow->contents, "sphinx-theme-package: furo",
        "deploy workflow must pass the configured sphinx theme package"
    );
    require_contains(
        html_workflow->contents,
        "uses: ./.github/actions/publish-manifesto-report",
        "deploy workflow must publish the shared Pages report surface"
    );
    require_contains(
        html_workflow->contents,
        "\"${{ steps.manifesto.outputs.engels-binary }}\" check coverage",
        "deploy workflow must stage manifesto-owned coverage checks"
    );

    const ecosystem::tracked_surface_file* setup_action
        = find_tracked_surface_file(
            generated_files, ".github/actions/setup-manifesto/action.yml"
        );
    require_true(
        setup_action != nullptr,
        "tracked surface generation must include the local setup action"
    );
    const auto native_dependencies = setup_action->contents.substr(
        setup_action->contents.find("packages=("),
        setup_action->contents.find("if [ \"${{ inputs.install-docs }}\"")
            - setup_action->contents.find("packages=(")
    );
    require_contains(
        native_dependencies, "doxygen",
        "ordinary native documentation regressions require Doxygen without "
        "enabling optional presentation"
    );
    require_contains(
        native_dependencies, "graphviz",
        "ordinary native documentation regressions require the shared graph "
        "renderer"
    );
    require_contains(
        setup_action->contents,
        "repository: ${{ inputs.manifesto-repository }}",
        "generated setup action must stay reusable through workflow inputs"
    );
    require_contains(
        setup_action->contents, "uses: vendor/qt@qt-pin",
        "generated setup action must use the configured immutable Qt action"
    );
    require_contains(
        setup_action->contents,
        "python3 -m pip install --user sphinx myst-parser "
        "\"${{ inputs.sphinx-theme-package }}\"",
        "generated setup action must install the workflow-selected "
        "Sphinx theme package"
    );
    require_contains(
        setup_action->contents, "--parallel \"$BUILD_PARALLELISM\"",
        "generated setup action must cap build parallelism through inputs"
    );
    require_contains(
        setup_action->contents, "marx-binary=$build_root/marx",
        "generated setup action must expose the bootstrap Marx binary"
    );
    require_contains(
        setup_action->contents, "engels-binary=$build_root/engels",
        "bootstrap must also expose Engels"
    );
    require_not_contains(
        setup_action->contents, "manifesto-binary",
        "bootstrap must not expose a third actor"
    );
    require_not_contains(
        tests_workflow->contents, "manifesto-binary",
        "workflows must use explicit owners"
    );
    require_not_contains(
        setup_action->contents, "build debug engels:engels",
        "generated setup action must not require a second frontend bootstrap "
        "step"
    );

    const ecosystem::tracked_surface_file* stage_action
        = find_tracked_surface_file(
            generated_files, ".github/actions/run-manifesto-stage/action.yml"
        );
    require_true(
        stage_action != nullptr,
        "tracked surface generation must include the shared stage runner"
    );
    require_contains(
        stage_action->contents, "status as passed, failed, or skipped",
        "shared stage runner must expose non-fatal stage status reporting"
    );

    const ecosystem::tracked_surface_file* report_action
        = find_tracked_surface_file(
            generated_files,
            ".github/actions/publish-manifesto-report/action.yml"
        );
    require_true(
        report_action != nullptr,
        "tracked surface generation must include the shared report publisher"
    );
    require_contains(
        report_action->contents, "issues.updateComment",
        "shared report publisher must update sticky pull request comments"
    );
    require_contains(
        report_action->contents, "issues.create({",
        "shared report publisher must create default-branch issues when "
        "needed"
    );
    require_contains(
        report_action->contents, "uses: actions/github-script@script-pin",
        "report publisher must use the configured immutable script action"
    );

    const ecosystem::tracked_surface_file* codeql_workflow
        = find_tracked_surface_file(
            generated_files, ".github/workflows/codeql.yml"
        );
    require_true(
        codeql_workflow != nullptr,
        "tracked surface generation must include the CodeQL workflow"
    );
    require_contains(
        codeql_workflow->contents, "uses: github/codeql-action/init@codeql-pin",
        "CodeQL workflow must use the configured immutable action ref"
    );

    const ecosystem::tracked_surface_file* clang_format
        = find_tracked_surface_file(generated_files, ".clang-format");
    require_true(
        clang_format != nullptr,
        "tracked surface generation must include the shared format file"
    );
    require_contains(
        clang_format->contents, "ColumnLimit:     80",
        "tracked format file must retain the shipped style defaults"
    );
}

void test_tracked_surface_generation_supports_remote_setup_action() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    write_text(
        root.path() / "manifesto.github.vars.json",
        "{\n"
        "  \"manifesto_repository\": \"example/ecosystem\",\n"
        "  \"manifesto_ref\": \"release/preview\",\n"
        "  \"manifesto_setup_action\": "
        "\"example/ecosystem/.github/actions/setup-manifesto@release/"
        "preview\",\n"
        "  \"manifesto_build_parallelism\": \"3\",\n"
        "  \"sphinx_theme\": \"furo\",\n"
        "  \"sphinx_theme_package\": \"furo\"\n"
        "}\n"
    );

    const ecosystem::manifest_report report
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        report.errors.empty(),
        "sample manifest must load before remote-action tracked-surface "
        "generation"
    );
    require_true(
        report.value.has_value(),
        "sample manifest must parse before remote-action tracked-surface "
        "generation"
    );

    ecosystem::string_list errors;
    const std::vector<ecosystem::tracked_surface_file> generated_files
        = ecosystem::generate_tracked_surface_files(
            *report.value, root.path(), &errors
        );
    require_true(
        errors.empty(),
        "tracked surface generation must accept a remote setup action"
    );

    const ecosystem::tracked_surface_file* tests_workflow
        = find_tracked_surface_file(
            generated_files, ".github/workflows/tests.yml"
        );
    require_true(
        tests_workflow != nullptr,
        "remote-action tracked surface generation must include the tests "
        "workflow"
    );
    require_contains(
        tests_workflow->contents,
        "uses: "
        "example/ecosystem/.github/actions/setup-manifesto@release/preview",
        "tests workflow must use the configured remote setup action"
    );
    require_not_contains(
        tests_workflow->contents, "uses: ./.github/actions/setup-manifesto",
        "tests workflow must not keep the local setup action when a remote "
        "action is configured"
    );

    const ecosystem::tracked_surface_file* html_workflow
        = find_tracked_surface_file(
            generated_files, ".github/workflows/html.yml"
        );
    require_true(
        html_workflow != nullptr,
        "remote-action tracked surface generation must include the deploy "
        "workflow"
    );
    require_not_contains(
        html_workflow->contents, "Checkout manifesto tool",
        "deploy workflow must stay thin when a remote setup action is used"
    );
    require_contains(
        tests_workflow->contents, "build-parallelism: 3",
        "tests workflow must forward configured build parallelism"
    );
    require_true(
        find_tracked_surface_file(
            generated_files, ".github/actions/run-manifesto-stage/action.yml"
        ) != nullptr,
        "remote-action tracked surface generation must keep the shared stage "
        "runner"
    );
    require_true(
        find_tracked_surface_file(
            generated_files,
            ".github/actions/publish-manifesto-report/action.yml"
        ) != nullptr,
        "remote-action tracked surface generation must keep the shared "
        "report publisher"
    );

    require_true(
        find_tracked_surface_file(
            generated_files, ".github/actions/setup-manifesto/action.yml"
        ) == nullptr,
        "remote-action tracked surface generation must not materialize the "
        "local setup action"
    );
}

void test_required_template_failures_preserve_generated_state() {
    for (const std::string path :
         { "cmake/apply_defaults.tpl", "cmake/visitor_facade.tpl",
           "tracked/.gitignore.tpl", ".github/workflows/tests.yml",
           "cmake/package_surface/add_interface_library.tpl" }) {
        temp_dir root;
        temp_dir templates;
        auto value = sample_manifest();
        value.components.front().stack
            = json::object({ { "llvm_clang", true } });
        write_text(root.path() / "CMakeLists.txt", "previous facade\n");
        write_text(root.path() / ".gitignore", "previous ignores\n");
        write_text(templates.path() / path, "{{required_missing_binding}}\n");
        scoped_env env("MANIFESTO_TEMPLATE_ROOT", templates.path().string());

        const auto report = ecosystem::sync_project(root.path(), value);
        require_true(
            !report.errors.empty() && report.written_files.empty(),
            "required template failure must fail before generated writes: "
                + path
        );
        require_contains(
            join_lines(report.errors), path,
            "render failure must identify its template"
        );
        require_true(
            read_text(root.path() / "CMakeLists.txt") == "previous facade\n"
                && read_text(root.path() / ".gitignore")
                    == "previous ignores\n",
            "failed generation must preserve previous generated contents"
        );
        require_true(
            !fs::exists(root.path() / ".github"),
            "failed rendering must not create partial workflows"
        );
    }
}

void test_required_developer_template_failure_is_reported() {
    temp_dir root;
    temp_dir templates;
    const auto value = sample_manifest();
    const fs::path cmake
        = ecosystem::local_developer_cmakelists_path(root.path());
    write_text(cmake, "previous developer surface\n");
    write_text(
        templates.path() / "cmake/developer_options.tpl", "{{missing}}\n"
    );
    scoped_env env("MANIFESTO_TEMPLATE_ROOT", templates.path().string());
    std::string error;
    const auto status
        = ecosystem::ensure_local_developer_surface(root.path(), value, &error);
    require_true(
        status == ecosystem::command_error::task_failed,
        "developer generation must translate render failure to operation "
        "failure"
    );
    require_contains(
        error, "cmake/developer_options.tpl",
        "developer error must name its template"
    );
    require_true(
        read_text(cmake) == "previous developer surface\n",
        "developer render failure must preserve its previous surface"
    );
}

void test_required_missing_and_unreadable_templates_fail() {
    for (const std::string path :
         { "missing-required-test.tpl", "unreadable-required-test.tpl" }) {
        temp_dir templates;
        fs::create_directories(
            templates.path() / "unreadable-required-test.tpl"
        );
        scoped_env env("MANIFESTO_TEMPLATE_ROOT", templates.path().string());
        bool failed = false;
        try {
            ecosystem::render_required_text_template(path, {});
        } catch (const ecosystem::template_render_error& error) {
            failed = true;
            require_contains(
                error.what(), path,
                "required error must identify missing or unreadable template"
            );
        }
        require_true(
            failed, "required renderer must throw on unavailable template"
        );
    }
}

void test_sync_project_removes_obsolete_tracked_surface_files() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    const ecosystem::manifest_report initial_report
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        initial_report.errors.empty(),
        "sample manifest must load before initial sync"
    );
    require_true(
        initial_report.value.has_value(),
        "sample manifest must parse before initial sync"
    );

    const ecosystem::sync_report initial_sync
        = ecosystem::sync_project(root.path(), *initial_report.value);
    require_true(
        initial_sync.errors.empty(),
        "initial sync must succeed before obsolete-surface cleanup"
    );
    require_true(
        fs::exists(root.path() / ".github/actions/setup-manifesto/action.yml"),
        "initial sync must materialize the local setup action"
    );

    write_text(
        root.path() / "manifesto.github.vars.json",
        "{\n"
        "  \"manifesto_setup_action\": "
        "\"example/ecosystem/.github/actions/setup-manifesto@release/"
        "preview\"\n"
        "}\n"
    );

    const ecosystem::manifest_report updated_report
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        updated_report.errors.empty(),
        "sample manifest must reload before obsolete-surface cleanup"
    );
    require_true(
        updated_report.value.has_value(),
        "sample manifest must reparse before obsolete-surface cleanup"
    );

    const ecosystem::string_list drift
        = ecosystem::tracked_surface_drift(root.path(), *updated_report.value);
    require_contains(
        join_lines(drift),
        ".github/actions/setup-manifesto/action.yml is obsolete for current "
        "ecosystem defaults; run `marx sync`",
        "tracked surface drift must report obsolete sync-owned files"
    );

    const ecosystem::sync_report updated_sync
        = ecosystem::sync_project(root.path(), *updated_report.value);
    require_true(
        updated_sync.errors.empty(),
        "sync must remove obsolete sync-owned files"
    );

    bool removed_local_action = false;
    for (const fs::path& removed_file : updated_sync.removed_files) {
        if (removed_file
            == root.path() / ".github/actions/setup-manifesto/action.yml") {
            removed_local_action = true;
            break;
        }
    }
    require_true(
        removed_local_action, "sync must report the removed local setup action"
    );
    require_true(
        !fs::exists(root.path() / ".github/actions/setup-manifesto/action.yml"),
        "sync must remove obsolete local setup action files"
    );
}

void test_template_loader_renders_repo_owned_templates() {
    const fs::path mutation_template
        = ecosystem::locate_template_path({ "mutation/source.cpp.tpl" });
    require_true(
        !mutation_template.empty(),
        "template loader must resolve mutation templates from the repo"
    );

    std::string error_message;
    const std::string source_contents = ecosystem::render_text_template(
        "mutation/source.cpp.tpl", { { "module_path", "sample/core" } },
        &error_message
    );
    require_true(
        error_message.empty(), "template loader must render mutation templates"
    );
    require_contains(
        source_contents, "#include \"sample/core.hpp\"",
        "mutation source template must substitute the module path"
    );

    error_message.clear();
    const std::string sphinx_contents = ecosystem::render_text_template(
        "tooling/sphinx_conf.py.tpl",
        {
            { "project_name", "sample" },
            { "extensions", "    'myst_parser',\n" },
            { "source_suffix_markdown", "    '.md': 'markdown',\n" },
        },
        &error_message
    );
    require_true(
        error_message.empty(), "template loader must render tooling templates"
    );
    require_contains(
        sphinx_contents, "project = \"sample\"",
        "tooling templates must substitute the project name"
    );
    require_contains(
        sphinx_contents, "'myst_parser'",
        "tooling templates must preserve injected extension blocks"
    );
    require_contains(
        sphinx_contents, "sphinx_rtd_theme",
        "tooling templates must keep the default Read the Docs theme"
    );

    const fs::path shared_format_template = ecosystem::locate_template_path(
        { ".clang-format", "tooling/clang-format.tpl" }
    );
    require_true(
        !shared_format_template.empty(),
        "template loader must resolve the shared format template"
    );
    require_contains(
        read_text(shared_format_template), "ColumnLimit:     80",
        "template loader must retain the shipped format defaults "
        "when it is available"
    );

    const fs::path shared_workflow_template = ecosystem::locate_template_path(
        { ".github/workflows/tests.yml",
          "tracked/.github/workflows/tests.yml.tpl" }
    );
    require_true(
        !shared_workflow_template.empty(),
        "template loader must resolve the shared workflow template"
    );
    require_contains(
        shared_workflow_template.generic_string(),
        "/templates/tracked/.github/workflows/tests.yml.tpl",
        "template loader must prefer the executable-owned workflow "
        "template when it is available"
    );

    const fs::path shared_action_template = ecosystem::locate_template_path(
        { ".github/actions/setup-manifesto/action.yml",
          "tracked/.github/actions/setup-manifesto/action.yml.tpl" }
    );
    require_true(
        !shared_action_template.empty(),
        "template loader must resolve the shared GitHub action template"
    );
    require_contains(
        shared_action_template.generic_string(),
        "/templates/tracked/.github/actions/setup-manifesto/action.yml.tpl",
        "template loader must prefer the executable-owned GitHub action "
        "template when it is available"
    );

    const fs::path shared_doxygen_template = ecosystem::locate_template_path(
        { "Doxyfile", "tooling/Doxyfile.tpl" }
    );
    require_true(
        !shared_doxygen_template.empty(),
        "template loader must resolve the shared Doxygen template"
    );
    require_contains(
        shared_doxygen_template.generic_string(),
        "/templates/tooling/Doxyfile.tpl",
        "template loader must prefer the executable-owned Doxygen template"
    );

    error_message.clear();
    const std::string doxygen_contents
        = ecosystem::render_text_template_candidates(
            { "Doxyfile", "tooling/Doxyfile.tpl" },
            { { "project_name", "\"sample\"" },
              { "project_version", "\"2.3.4\"" },
              { "input_files", "\"src/main.cpp\"" },
              { "output_dir", "\".ecosystem/doxygen\"" },
              { "warning_log", "\".ecosystem/doxygen/warnings.log\"" },
              { "clang_database", "\"\"" } },
            &error_message
        );
    require_true(
        error_message.empty(),
        "template loader must render the shared Doxygen template"
    );
    require_contains(
        doxygen_contents, "PROJECT_NAME           = \"sample\"",
        "shared Doxygen template must substitute the project name"
    );
    require_contains(
        doxygen_contents, "OUTPUT_DIRECTORY       = \".ecosystem/doxygen\"",
        "shared Doxygen template must keep the ecosystem-local "
        "output directory"
    );

    const fs::path shared_cmake_template
        = ecosystem::locate_template_path({ "cmake/surface_prefix.tpl" });
    require_true(
        !shared_cmake_template.empty(),
        "template loader must resolve shared CMake surface templates"
    );
    require_true(
        shared_cmake_template.lexically_normal()
            == (fs::path(ECOS_TEST_SOURCE_DIR) / "templates/cmake"
                / "surface_prefix.tpl")
                   .lexically_normal(),
        "template loader must prefer the executable-owned CMake template "
        "when it is available"
    );

    error_message.clear();
    const std::string doctor_probe_contents = ecosystem::render_text_template(
        "cmake/doctor_probe.tpl",
        {
            { "cpp_standard", "20" },
            { "package_surface",
              "find_package(nlohmann_json CONFIG REQUIRED)\n" },
        },
        &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render the shared doctor probe template"
    );
    require_contains(
        doctor_probe_contents, "set(CMAKE_CXX_STANDARD 20)",
        "doctor probe template must substitute the project C++ standard"
    );
    require_contains(
        doctor_probe_contents, "find_package(nlohmann_json CONFIG REQUIRED)",
        "doctor probe template must preserve injected package surface text"
    );

    const fs::path shared_release_template
        = ecosystem::locate_template_path({ "release/debian_control.tpl" });
    require_true(
        !shared_release_template.empty(),
        "template loader must resolve shared release templates"
    );
    require_true(
        shared_release_template.lexically_normal()
            == (fs::path(ECOS_TEST_SOURCE_DIR) / "templates/release"
                / "debian_control.tpl")
                   .lexically_normal(),
        "template loader must prefer the executable-owned release "
        "template when it is available"
    );

    error_message.clear();
    const std::string debian_control = ecosystem::render_text_template(
        "release/debian_control.tpl",
        {
            { "package_name", "sample" },
            { "debian_version", "1.2.3~pre.1" },
            { "architecture", "amd64" },
            { "installed_size_kib", "42" },
            { "packager", "ecosystem prerelease <noreply@local>" },
            { "description", "Sample package" },
        },
        &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render the shared release template"
    );
    require_contains(
        debian_control, "Package: sample",
        "release templates must substitute the package name"
    );
    require_contains(
        debian_control, "Installed-Size: 42",
        "release templates must substitute computed size fields"
    );

    const fs::path shared_benchmark_template
        = ecosystem::locate_template_path({ "benchmark/plot.svg.tpl" });
    require_true(
        !shared_benchmark_template.empty(),
        "template loader must resolve shared benchmark templates"
    );
    require_true(
        shared_benchmark_template.lexically_normal()
            == (fs::path(ECOS_TEST_SOURCE_DIR) / "templates/benchmark"
                / "plot.svg.tpl")
                   .lexically_normal(),
        "template loader must prefer the executable-owned benchmark "
        "template when it is available"
    );

    error_message.clear();
    const std::string benchmark_svg = ecosystem::render_text_template(
        "benchmark/plot.svg.tpl",
        {
            { "svg_width", "960" },
            { "svg_height", "540" },
            { "margin_left", "80.00" },
            { "margin_top", "50.00" },
            { "plot_width", "660.00" },
            { "plot_height", "420.00" },
            { "title", "sample plot" },
            { "x_axis_label_y", "522.00" },
            { "y_axis_label_y", "260.00" },
            { "y_ticks", "  <line />\n" },
            { "x_ticks", "  <text>64</text>\n" },
            { "series_layers", "  <polyline />\n" },
        },
        &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render the shared benchmark template"
    );
    require_contains(
        benchmark_svg, "sample plot",
        "benchmark templates must substitute the plot title"
    );
    require_contains(
        benchmark_svg, "GFLOPs/s",
        "benchmark templates must preserve the throughput axis label"
    );

    const fs::path shared_package_surface_template
        = ecosystem::locate_template_path(
            { "cmake/package_surface/add_interface_library.tpl" }
        );
    require_true(
        !shared_package_surface_template.empty(),
        "template loader must resolve shared package-surface templates"
    );
    require_true(
        shared_package_surface_template.lexically_normal()
            == (fs::path(ECOS_TEST_SOURCE_DIR)
                / "templates/cmake/package_surface/add_interface_library.tpl")
                   .lexically_normal(),
        "template loader must prefer the executable-owned package-surface "
        "template when it is available"
    );

    error_message.clear();
    const std::string package_surface_line = ecosystem::render_text_template(
        "cmake/package_surface/target_link_libraries_single.tpl",
        {
            { "indent", {} },
            { "target_name", "sample_support" },
            { "library", "sample::core" },
        },
        &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render the shared package-surface template"
    );
    require_contains(
        package_surface_line,
        "target_link_libraries(sample_support INTERFACE sample::core)",
        "package-surface templates must substitute target link bindings"
    );

    const fs::path shared_sync_surface_template
        = ecosystem::locate_template_path(
            { "cmake/generated_tests_block.tpl" }
        );
    require_true(
        !shared_sync_surface_template.empty(),
        "template loader must resolve shared sync CMake templates"
    );
    require_true(
        shared_sync_surface_template.lexically_normal()
            == (fs::path(ECOS_TEST_SOURCE_DIR)
                / "templates/cmake/generated_tests_block.tpl")
                   .lexically_normal(),
        "template loader must prefer the executable-owned sync CMake "
        "template when it is available"
    );

    error_message.clear();
    const std::string path_set_block = ecosystem::render_text_template(
        "cmake/path_set_block.tpl",
        {
            { "variable_name", "SAMPLE_HEADERS" },
            { "paths_block",
              "        ${CMAKE_CURRENT_SOURCE_DIR}/include/sample/core.hpp\n" },
        },
        &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render shared path-set templates"
    );
    require_contains(
        path_set_block, "set(SAMPLE_HEADERS",
        "path-set templates must substitute the variable name"
    );
    require_contains(
        path_set_block, "include/sample/core.hpp",
        "path-set templates must preserve injected path entries"
    );

    error_message.clear();
    const std::string install_target_block = ecosystem::render_text_template(
        "cmake/install_target.tpl", { { "target_name", "sample__app" } },
        &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render shared install templates"
    );
    require_contains(
        install_target_block, "install(TARGETS sample__app",
        "install templates must substitute the target name"
    );

    const fs::path shared_artifact_template
        = ecosystem::locate_template_path({ "cmake/artifact/library.tpl" });
    require_true(
        !shared_artifact_template.empty(),
        "template loader must resolve shared artifact CMake templates"
    );
    require_true(
        shared_artifact_template.lexically_normal()
            == (fs::path(ECOS_TEST_SOURCE_DIR)
                / "templates/cmake/artifact/library.tpl")
                   .lexically_normal(),
        "template loader must prefer the executable-owned artifact CMake "
        "template when it is available"
    );

    error_message.clear();
    const std::string artifact_link_block = ecosystem::render_text_template(
        "cmake/artifact/link_libraries.tpl",
        {
            { "target_name", "sample__app" },
            { "link_scope", "PRIVATE" },
            { "link_targets_block", "    sample__core\n    sample::dep\n" },
        },
        &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render shared artifact templates"
    );
    require_contains(
        artifact_link_block, "target_link_libraries(sample__app PRIVATE",
        "artifact templates must substitute the target and link scope"
    );
    require_contains(
        artifact_link_block, "sample::dep",
        "artifact templates must preserve injected link targets"
    );

    const fs::path shared_component_template = ecosystem::locate_template_path(
        { "cmake/component/tests_guard_open.tpl" }
    );
    require_true(
        !shared_component_template.empty(),
        "template loader must resolve shared component CMake templates"
    );
    require_true(
        shared_component_template.lexically_normal()
            == (fs::path(ECOS_TEST_SOURCE_DIR)
                / "templates/cmake/component/tests_guard_open.tpl")
                   .lexically_normal(),
        "template loader must prefer the executable-owned component CMake "
        "template when it is available"
    );

    error_message.clear();
    const std::string gtest_warning_block = ecosystem::render_text_template(
        "cmake/component/gtest_guard_else.tpl",
        { { "component_id", "sample_tests" } }, &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render shared component templates"
    );
    require_contains(
        gtest_warning_block,
        "GTest not found; skipping test component sample_tests",
        "component templates must substitute the component id in warnings"
    );
}

void test_ensure_local_artifacts_keeps_tracked_style_surfaces_sync_owned() {
    temp_dir root;
    ecosystem::ensure_local_artifacts(root.path(), true, true);

    require_true(
        !fs::exists(root.path() / ".clang-format"),
        "local artifact materialization must not rewrite the tracked "
        "format surface"
    );
    require_true(
        !fs::exists(root.path() / ".clang-tidy"),
        "local artifact materialization must not rewrite the tracked "
        "tidy surface"
    );
    require_true(
        fs::exists(root.path() / ".ecosystem" / "source"),
        "local artifact materialization must still prepare the "
        "ecosystem state root"
    );
}

} // namespace ecosystem_test_support
