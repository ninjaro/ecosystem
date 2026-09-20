#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

void test_ci_stage_reports_intermediate_and_pipeline_failures() {
    const std::string action = ecosystem::render_required_text_template(
        "tracked/.github/actions/run-manifesto-stage/action.yml.tpl", {}
    );
    const std::string marker = "      run: |\n";
    const auto start = action.find(marker);
    require_true(
        start != std::string::npos,
        "stage action must contain its shell program"
    );
    std::istringstream lines(action.substr(start + marker.size()));
    std::string program, line;
    while (std::getline(lines, line)) {
        program
            += (line.starts_with("        ") ? line.substr(8) : line) + "\n";
    }
    temp_dir project;
    const auto generated = ecosystem::generate_tracked_surface_files(
        sample_manifest(), project.path()
    );
    for (const auto& filename : { "tests.yml", "codeql.yml", "html.yml" }) {
        const auto* workflow = find_tracked_surface_file(
            generated, std::string(".github/workflows/") + filename
        );
        require_true(workflow != nullptr, "workflow must be generated");
        if (std::string(filename) == "html.yml") {
            require_contains(
                workflow->contents, "on:\n  workflow_dispatch:\n",
                "presentation workflow must retain manual dispatch"
            );
            require_not_contains(
                workflow->contents,
                "  push:", "later-stage presentation must not run on pushes"
            );
            require_not_contains(
                workflow->contents, "  pull_request:",
                "later-stage presentation must not run on pull requests"
            );
            require_contains(
                workflow->contents, "steps.coverage.outputs.status == 'passed'",
                "coverage uploads must use the status output provided by the "
                "stage action"
            );
            require_not_contains(
                workflow->contents, "steps.coverage.outputs.enabled",
                "coverage uploads must not depend on a nonexistent output"
            );
        } else if (std::string(filename) == "codeql.yml") {
            const auto trigger
                = workflow->contents.find("      - 'templates/**'");
            require_true(
                trigger != std::string::npos
                    && workflow->contents.find(
                           "      - 'templates/**'", trigger + 1
                       ) != std::string::npos,
                "template changes must trigger both push and pull-request "
                "checks"
            );
            require_not_contains(
                workflow->contents, "check sphinx",
                "ordinary CI must not depend on later-stage presentation"
            );
            require_not_contains(
                workflow->contents, "install-docs: 'true'",
                "ordinary CI must not install presentation dependencies"
            );
        }
    }

    struct stage_case {
        std::string command;
        std::string status;
        int exit_code;
    };

    for (const auto& item : std::vector<stage_case> {
             { "false\nprintf should-not-run", "failed", 1 },
             { "false | cat", "failed", 1 },
             { "printf success", "passed", 0 },
             { "exit 3", "skipped", 3 },
         }) {
        temp_dir root;
        std::string script = program;
        for (const auto& [name, value] :
             std::vector<std::pair<std::string, std::string>> {
                 { "stage-id", "audit" },
                 { "stage-label", "Audit" },
                 { "shell-command", item.command },
                 { "skipped-exit-codes", "3" },
             }) {
            const std::string token = "${{ inputs." + name + " }}";
            std::size_t offset = 0;
            while ((offset = script.find(token, offset)) != std::string::npos) {
                script.replace(offset, token.size(), value);
                offset += value.size();
            }
        }
        const fs::path command = root.path() / "stage.sh";
        const fs::path outputs = root.path() / "outputs";
        write_text(command, script);
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", command.string() }, root.path(),
            { { "GITHUB_OUTPUT", outputs.string() },
              { "GITHUB_STEP_SUMMARY", (root.path() / "summary").string() } }
        );
        require_true(
            result.exit_code == 0,
            "stage must keep the workflow alive to report: " + result.output
        );
        require_contains(
            read_text(outputs), "status=" + item.status + "\n",
            "stage status must reflect every failure"
        );
        require_contains(
            read_text(outputs),
            "exit-code=" + std::to_string(item.exit_code) + "\n",
            "stage must retain the child exit status"
        );
        require_not_contains(
            read_text(
                root.path() / ".ecosystem/github/reports/audit/output.log"
            ),
            "should-not-run", "stage must stop after an intermediate failure"
        );
    }
}

void test_ci_required_result_enforces_failures_and_missing_evidence() {
    temp_dir root;
    const auto files = ecosystem::generate_tracked_surface_files(
        sample_manifest(), root.path()
    );
    const auto* workflow
        = find_tracked_surface_file(files, ".github/workflows/tests.yml");
    require_true(workflow != nullptr, "required workflow must exist");
    require_not_contains(
        workflow->contents, "paths:",
        "required checks must cover arbitrary manifest-owned source directories"
    );
    for (const auto* name : { "Upload check reports", "Publish check report",
                              "Enforce required result" }) {
        require_contains(
            workflow->contents,
            std::string("- name: ") + name + "\n        if: ${{ always() }}",
            "reporting and enforcement must also run after setup or stage "
            "failures"
        );
    }
    const auto gate = root.path() / "gate.sh";
    write_text(
        gate,
        github_shell_program(workflow->contents, "Enforce required result", 8)
    );
    const auto* action = find_tracked_surface_file(
        files, ".github/actions/run-manifesto-stage/action.yml"
    );
    require_true(action != nullptr, "stage runner must exist");
    const auto program
        = github_shell_program(action->contents, "Run stage command", 6);
    const auto report
        = root.path() / ".ecosystem/github/reports/01-required-checks";
    const auto outputs = root.path() / "outputs";
    const auto stage = root.path() / "stage.sh";
    const auto run_gate = [&](const std::string& outcome,
                              const std::string& status,
                              const std::string& code) {
        return ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", gate.string() }, root.path(),
            { { "CHECK_OUTCOME", outcome },
              { "CHECK_STATUS", status },
              { "CHECK_EXIT_CODE", code } }
        );
    };
    require_true(
        run_gate("skipped", "", "").exit_code != 0,
        "failed bootstrap cannot yield a successful required result"
    );
    for (const auto& [command, code] :
         std::vector<std::pair<std::string, int>> {
             { "printf 'beauty finding: advisory\\n'", 0 },
             { "false\nprintf unreachable", 1 },
             { "false | cat", 1 },
             { "exit 3", 3 },
             { "exit 5", 5 },
             { "exit 127", 127 } }) {
        auto script = program;
        for (const auto& [name, value] :
             std::vector<std::pair<std::string, std::string>> {
                 { "stage-id", "01-required-checks" },
                 { "stage-label", "Required local checks" },
                 { "shell-command", command },
                 { "skipped-exit-codes", "" } }) {
            script = github_substitute(script, "inputs." + name, value);
        }
        write_text(stage, script);
        write_text(outputs, "");
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", stage.string() }, root.path(),
            { { "GITHUB_OUTPUT", outputs.string() },
              { "GITHUB_STEP_SUMMARY", (root.path() / "summary").string() } }
        );
        require_true(
            result.exit_code == 0 && fs::exists(report / "summary.md"),
            "every completed stage must preserve its report before enforcement"
        );
        const auto status = code == 0 ? "passed" : "failed";
        require_contains(
            read_text(outputs), "exit-code=" + std::to_string(code) + "\n",
            "stage must expose its actual process exit code"
        );
        require_true(
            (run_gate("success", status, std::to_string(code)).exit_code == 0)
                == (code == 0),
            "required CI outcome must follow the local exit code, including "
            "unsupported and missing tools"
        );
    }
    require_true(
        run_gate("failure", "passed", "0").exit_code != 0,
        "partial outputs from an incomplete reporting step cannot pass"
    );
    require_true(
        run_gate("success", "skipped", "3").exit_code != 0,
        "a required result cannot be skipped"
    );
    fs::remove(report / "output.log");
    require_true(
        run_gate("success", "passed", "0").exit_code != 0,
        "a missing required log must fail closed"
    );
    write_text(report / "output.log", "");
    write_text(report / "summary.md", "");
    require_true(
        run_gate("success", "passed", "0").exit_code != 0,
        "an empty required summary must fail closed"
    );
    fs::remove(report / "summary.md");
    require_true(
        run_gate("success", "passed", "0").exit_code != 0,
        "a missing required summary must fail closed"
    );

    const auto* codeql
        = find_tracked_surface_file(files, ".github/workflows/codeql.yml");
    require_true(codeql != nullptr, "CodeQL adapter must exist");
    write_text(
        gate, github_shell_program(codeql->contents, "Enforce CodeQL result", 8)
    );
    const std::vector<std::pair<std::string, std::string>> clean {
        { "REPOSITORY_STATUS", "passed" },
        { "CODEQL_INIT_STATUS", "success" },
        { "CODEQL_BUILD_STATUS", "passed" },
        { "CODEQL_ANALYZE_STATUS", "success" }
    };
    require_true(
        ecosystem::capture_command_result(
            { "bash", gate.string() }, root.path(), clean
        )
                .exit_code
            == 0,
        "completed CodeQL operations must pass"
    );
    for (std::size_t i = 0; i < clean.size(); ++i) {
        for (const auto* state :
             { "", "failed", "failure", "skipped", "cancelled" }) {
            auto env = clean;
            env[i].second = state;
            require_true(
                ecosystem::capture_command_result(
                    { "bash", gate.string() }, root.path(), env
                )
                        .exit_code
                    != 0,
                "every incomplete CodeQL operation must fail its workflow"
            );
        }
    }
}

void test_github_bootstrap_vars_validate_explicit_selection() {
    temp_dir root;
    const auto generate = [&](const json& config,
                              ecosystem::string_list* errors) {
        write_text(root.path() / "manifesto.github.vars.json", config.dump());
        return ecosystem::generate_tracked_surface_files(
            sample_manifest(), root.path(), errors
        );
    };
    for (const auto& config : std::vector<json> {
             { { "manifesto_bootstrap", "unknown" } },
             { { "manifesto_repository", "example/tools" } },
             { { "manifesto_ref", "reviewed-ref" } },
             { { "manifesto_bootstrap", "checkout" },
               { "manifesto_repository", "example/tools" },
               { "manifesto_ref", "old" } },
             { { "manifesto_source_path", "../tools" } },
             { { "manifesto_source_path", "/tools" } },
             { { "manifesto_source_path", "" } },
             { { "manifesto_source_path", "tools\nnext" } },
             { { "manifesto_source_path", "${{ github.ref }}" } },
             { { "manifesto_build_parallelism", "0" } },
             { { "manifesto_build_parallelism", "2\nnext" } } }) {
        ecosystem::string_list errors;
        generate(config, &errors);
        require_true(
            !errors.empty(),
            "invalid bootstrap inputs must fail generation: " + config.dump()
        );
    }
    for (const auto& config : std::vector<json> {
             json::object(),
             { { "manifesto_bootstrap", "checkout" } },
             { { "manifesto_repository", "example/tools" },
               { "manifesto_ref", "release/preview" },
               { "manifesto_source_path", "tools/ecosystem" } },
             { { "manifesto_repository", "example/tools" },
               { "manifesto_ref", "0123456789abcdef" },
               { "manifesto_source_path", "tool source/#quoted" } } }) {
        ecosystem::string_list errors;
        const auto files = generate(config, &errors);
        require_true(
            errors.empty(), "explicit compatible bootstrap inputs must render"
        );
        for (const auto* name : { "tests.yml", "codeql.yml", "html.yml" }) {
            const auto* workflow = find_tracked_surface_file(
                files, std::string(".github/workflows/") + name
            );
            require_true(
                workflow != nullptr, "all workflow consumers must exist"
            );
            require_not_contains(
                workflow->contents, "ninjaro/cppr",
                "no obsolete repository fallback"
            );
            require_not_contains(
                workflow->contents, "manifesto-ref: master",
                "no mutable implicit ref"
            );
            require_contains(
                workflow->contents,
                "bootstrap: "
                    + config.value("manifesto_bootstrap", json("repository"))
                          .dump(),
                "workflow must forward explicit bootstrap mode as a YAML scalar"
            );
            require_contains(
                workflow->contents,
                "source-path: "
                    + config.value("manifesto_source_path", json(".")).dump(),
                "workflow must preserve the selected source layout"
            );
        }
    }
}

void test_ci_bootstrap_builds_reviewed_checkout_and_repository_layouts() {
    const auto action = ecosystem::render_required_text_template(
        "tracked/.github/actions/setup-manifesto/action.yml.tpl",
        { { "checkout_action", "actions/checkout@v4" },
          { "install_qt_action", "vendor/qt@pin" } }
    );
    require_contains(
        action, "if: ${{ inputs.bootstrap == 'repository' }}",
        "self-bootstrap must not fetch a different tooling revision"
    );
    require_not_contains(
        action, "sparse-checkout",
        "consumer layout cannot assume a parent monorepo"
    );
    require_contains(
        action, "repository: nlohmann/json\n        ref: v3.12.0",
        "clean Ubuntu runners must obtain the declared JSON version"
    );
    require_contains(
        action, "CMAKE_PREFIX_PATH=$prefix",
        "bootstrapped prerequisites must reach local child builds"
    );
    const auto prepare = github_shell_program(action, "Set ecosystem paths", 6);
    const auto build = github_shell_program(action, "Build Marx and Engels", 6);
    temp_dir fixture;
    const auto repository = fixture.path() / "provider";
    const auto source = repository / "nested/tool source";
    const auto git = [&](const ecosystem::string_list& args) {
        auto command = ecosystem::string_list { "git" };
        command.insert(command.end(), args.begin(), args.end());
        const auto result
            = ecosystem::capture_command_result(command, repository);
        require_true(
            result.exit_code == 0,
            "isolated revision fixture failed: " + result.output
        );
    };
    write_text(
        source / "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.20)\nproject(tooling_fixture "
        "LANGUAGES C)\n"
        "add_executable(marx actor.c)\nadd_executable(engels actor.c)\n"
    );
    write_text(source / "manifest.json", "{}\n");
    write_text(source / "templates/version.txt", "runtime data\n");
    write_text(
        source / "actor.c",
        "#include <stdio.h>\nint main(void) { puts(\"old-revision\"); return "
        "0; }\n"
    );
    git({ "init", "-q" });
    git({ "add", "." });
    git({ "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
          "commit", "-qm", "old" });
    git({ "tag", "old" });
    write_text(
        source / "actor.c",
        "#include <stdio.h>\nint main(void) { puts(\"reviewed-revision\"); "
        "return 0; }\n"
    );
    git({ "add", "." });
    git({ "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
          "commit", "-qm", "reviewed" });
    git({ "tag", "reviewed" });

    for (const auto* mode : { "checkout", "repository" }) {
        temp_dir workspace;
        const auto clone_root = std::string(mode) == "checkout"
            ? workspace.path() / "review"
            : workspace.path() / ".ecosystem/tooling/source";
        const auto clone = ecosystem::capture_command_result(
            { "git", "clone", "-q", repository.string(), clone_root.string() }
        );
        require_true(
            clone.exit_code == 0, "fixture must clone without remote access"
        );
        const auto selected_ref
            = std::string(mode) == "checkout" ? "reviewed" : "old";
        require_true(
            ecosystem::capture_command_result(
                { "git", "checkout", "-q", selected_ref }, clone_root
            )
                    .exit_code
                == 0,
            "fixture checkout must select the requested revision"
        );
        const auto project
            = std::string(mode) == "checkout" ? clone_root : workspace.path();
        const auto outputs = project / "outputs";
        const auto paths_script = project / "paths.sh";
        write_text(paths_script, prepare);
        auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", paths_script.string() }, project,
            { { "GITHUB_WORKSPACE", project.string() },
              { "GITHUB_OUTPUT", outputs.string() },
              { "BOOTSTRAP", mode },
              { "SOURCE_PATH", "nested/tool source" },
              { "TOOL_REPOSITORY",
                std::string(mode) == "checkout" ? "" : "fixture/tooling" },
              { "TOOL_REF",
                std::string(mode) == "checkout" ? "" : selected_ref },
              { "BUILD_PARALLELISM", "2" } }
        );
        require_true(
            result.exit_code == 0,
            "explicit bootstrap must prepare paths: " + result.output
        );
        std::map<std::string, std::string> values;
        std::istringstream lines(read_text(outputs));
        std::string line;
        while (std::getline(lines, line)) {
            const auto split = line.find('=');
            values[line.substr(0, split)] = line.substr(split + 1);
        }
        const auto build_script = project / "build.sh";
        write_text(build_script, build);
        const auto build_env
            = std::vector<std::pair<std::string, std::string>> {
                  { "CHECKOUT_ROOT", values.at("manifesto-checkout-root") },
                  { "SOURCE_ROOT", values.at("manifesto-source-root") },
                  { "BUILD_ROOT", values.at("manifesto-build-root") },
                  { "BUILD_PARALLELISM", "2" }
              };
        result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", build_script.string() }, project,
            build_env
        );
        require_true(
            result.exit_code == 0,
            "rendered bootstrap must build both actors: " + result.output
        );
        for (const auto* actor : { "marx-binary", "engels-binary" }) {
            result = ecosystem::capture_command_result(
                { values.at(actor) }, project
            );
            require_true(
                result.exit_code == 0, "advertised actor must execute"
            );
            require_contains(
                result.output,
                std::string(mode) == "checkout" ? "reviewed-revision"
                                                : "old-revision",
                "bootstrap must build exactly the reviewed or explicitly "
                "selected revision"
            );
        }
        fs::remove_all(clone_root / "nested/tool source/templates");
        result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", build_script.string() }, project,
            build_env
        );
        require_true(
            result.exit_code != 0,
            "incompatible source revisions without runtime templates must fail"
        );
        write_text(
            clone_root / "nested/tool source/templates/version.txt",
            "runtime data\n"
        );
        write_text(
            clone_root / "nested/tool source/CMakeLists.txt",
            "cmake_minimum_required(VERSION 3.20)\nproject(incompatible "
            "LANGUAGES NONE)\n"
        );
        result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", build_script.string() }, project,
            build_env
        );
        require_true(
            result.exit_code != 0,
            "stale actors from a previous build cannot satisfy an incompatible "
            "revision"
        );
        require_contains(
            result.output, "must build both marx and engels",
            "incompatible revisions must identify the required binary layout"
        );
    }
}

void test_ci_bootstrap_rejects_missing_inputs_and_escaping_layouts() {
    temp_dir root;
    const auto action = ecosystem::render_required_text_template(
        "tracked/.github/actions/setup-manifesto/action.yml.tpl",
        { { "checkout_action", "actions/checkout@v4" },
          { "install_qt_action", "vendor/qt@pin" } }
    );
    const auto paths = root.path() / "paths.sh";
    write_text(paths, github_shell_program(action, "Set ecosystem paths", 6));
    const auto clean = std::vector<std::pair<std::string, std::string>> {
        { "GITHUB_WORKSPACE", root.path().string() },
        { "GITHUB_OUTPUT", (root.path() / "outputs").string() },
        { "BOOTSTRAP", "repository" },
        { "SOURCE_PATH", "." },
        { "TOOL_REPOSITORY", "fixture/tooling" },
        { "TOOL_REF", "selected-ref" },
        { "BUILD_PARALLELISM", "2" }
    };
    for (const auto& [name, value] :
         std::vector<std::pair<std::string, std::string>> {
             { "BOOTSTRAP", "unknown" },
             { "BOOTSTRAP", "checkout" },
             { "SOURCE_PATH", "../outside" },
             { "SOURCE_PATH", "/absolute" },
             { "SOURCE_PATH", "" },
             { "SOURCE_PATH", "line\nbreak" },
             { "TOOL_REPOSITORY", "" },
             { "TOOL_REF", "" },
             { "BUILD_PARALLELISM", "0" },
             { "BUILD_PARALLELISM", "2; false" } }) {
        auto env = clean;
        for (auto& item : env)
            if (item.first == name)
                item.second = value;
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", paths.string() }, root.path(), env
        );
        require_true(
            result.exit_code != 0,
            "invalid or incomplete bootstrap must fail before checkout/build: "
                + name
        );
    }
    const auto checkout = root.path() / "checkout";
    const auto outside = root.path() / "outside";
    fs::create_directories(checkout);
    fs::create_directories(outside);
    fs::create_directory_symlink(outside, checkout / "tooling");
    const auto build = root.path() / "build.sh";
    write_text(build, github_shell_program(action, "Build Marx and Engels", 6));
    const auto result = ecosystem::capture_command_result(
        { "bash", "-e", "-o", "pipefail", build.string() }, root.path(),
        { { "CHECKOUT_ROOT", checkout.string() },
          { "SOURCE_ROOT", (checkout / "tooling").string() },
          { "BUILD_ROOT", (root.path() / "build").string() },
          { "BUILD_PARALLELISM", "2" } }
    );
    require_true(
        result.exit_code != 0 && !fs::exists(root.path() / "build"),
        "symlinked source layouts cannot escape the selected revision"
    );
}

void test_generated_builds_reject_compiler_warnings_in_c_and_cxx() {
    temp_dir root;
    write_text(
        root.path() / "manifest.json",
        json(
            { { "id", "warning_sample" },
              { "description", "Required compiler warnings" },
              { "facade", "sample:app" },
              { "artifacts",
                json::array(
                    { { { "id", "sample:app" },
                        { "kind", "exe" },
                        { "owns", json::array({ "helper" }) },
                        { "entry", "src/main.cpp" } } }
                ) } }
        ).dump(2)
    );
    write_text(
        root.path() / "src/main.cpp",
        "int main() { int unused_value = 42; return 0; }\n"
    );
    write_text(
        root.path() / "src/helper.c", "int helper(void) { return 0; }\n"
    );
    for (const auto* profile : { "debug", "release" }) {
        const auto result
            = run_marx_cli(root.path(), std::string("build ") + profile);
        require_true(
            result.exit_code == 5,
            "compiler warnings must fail local builds:\n" + result.output
        );
        require_contains(
            result.output, "unused_value",
            "build failure must retain the compiler diagnostic"
        );
    }
    require_true(
        run_marx_cli(root.path(), "sync").exit_code == 0,
        "warning fixture must generate its visitor surface"
    );
    const auto visitor = root.path() / "visitor";
    require_true(
        ecosystem::run_command(
            { "cmake", "-S", root.path().string(), "-B", visitor.string(),
              "-DCMAKE_C_COMPILER=gcc", "-DCMAKE_CXX_COMPILER=g++" },
            root.path()
        ) == 0,
        "GNU visitor fixture must configure"
    );
    const auto build_visitor = [&]() {
        return ecosystem::capture_command_result(
            { "cmake", "--build", visitor.string(), "--parallel", "2" },
            root.path()
        );
    };
    auto visitor_result = build_visitor();
    require_true(
        visitor_result.exit_code != 0,
        "visitor builds must enforce the same compiler contract"
    );
    require_contains(
        visitor_result.output, "unused_value",
        "GNU must report its selected warning"
    );
    write_text(root.path() / "src/main.cpp", "int main() { return 0; }\n");
    require_true(
        run_marx_cli(root.path(), "build debug").exit_code == 0
            && build_visitor().exit_code == 0,
        "clean C and C++ sources must build through both compilers"
    );
    write_text(
        root.path() / "src/helper.c",
        "int helper(void) { int unused_c_value = 42; return 0; }\n"
    );
    const auto native = run_marx_cli(root.path(), "build debug");
    visitor_result = build_visitor();
    require_true(
        native.exit_code == 5 && visitor_result.exit_code != 0,
        "C warnings must be hard errors without applying C++-only options"
    );
    require_contains(
        native.output, "unused_c_value",
        "Clang must retain C diagnostic attribution"
    );
    require_contains(
        visitor_result.output, "unused_c_value",
        "GNU must retain C diagnostic attribution"
    );
}

void test_cli_required_checks_propagate_failures_and_reject_empty_ctest_runs() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_sample_leak_check_project(project);
    require_true(
        run_marx_cli(project, "sync --then format").exit_code == 0,
        "aggregate check fixture must prepare canonical policy and formatting"
    );
    auto result = run_engels_cli(project, "check ci");
    require_true(
        result.exit_code == 0,
        "all required external checks must pass on clean inputs:\n"
            + result.output
    );
    require_contains(
        result.output, "ci checks passed",
        "aggregate success must follow all required stages"
    );
    const auto tests = project / "tests/main_tests.cpp";
    const auto clean_tests = read_text(tests);
    write_text(tests, "int main() { return 1; }\n");
    result = run_engels_cli(project, "check ci");
    require_true(
        result.exit_code == 5,
        "failing required tests must fail aggregate checks"
    );
    require_contains(
        result.output, "tests failed",
        "aggregate test failure must retain stage attribution"
    );
    require_not_contains(
        result.output,
        "tidy report:", "aggregate checks must stop at failed required tests"
    );
    write_text(tests, "int main() { int unused_test_value = 0; return 0; }\n");
    result = run_engels_cli(project, "check tests");
    require_true(
        result.exit_code == 5,
        "test compilation must also enforce compiler warnings"
    );
    require_contains(
        result.output, "unused_test_value",
        "test compiler errors must remain visible"
    );
    write_text(tests, clean_tests);

    const auto bin = root.path() / "bin";
    fs::create_directory(bin);
    write_fake_clang_tidy_tool(bin / "clang-tidy");
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    {
        scoped_env tidy_failure("FAKE_CLANG_TIDY_EXIT_CODE", "1");
        scoped_env tidy_output(
            "FAKE_CLANG_TIDY_OUTPUT", "required tidy fixture failure"
        );
        result = run_engels_cli(project, "check ci");
        require_true(
            result.exit_code == 5,
            "aggregate checks must propagate analyzer failure"
        );
        require_contains(
            result.output, "required tidy fixture failure",
            "aggregate failures must preserve tool output"
        );
        require_not_contains(
            result.output, "format check passed",
            "aggregate checks must stop at tidy failure"
        );
    }
    write_text(tests, "int main(){return 0;}\n");
    result = run_engels_cli(project, "check ci");
    require_true(
        result.exit_code == 5, "format drift must fail aggregate checks"
    );
    require_contains(
        result.output, "clang-format reported formatting drift",
        "format stage must remain a hard contract"
    );
    require_true(
        run_marx_cli(project, "format").exit_code == 0,
        "canonical formatting must repair predictable drift"
    );
    require_true(
        run_engels_cli(project, "check ci").exit_code == 0,
        "local repair must restore required-check success"
    );

    const auto ctest = ecosystem::find_command_path("ctest");
    scoped_env real_ctest("MANIFESTO_TEST_REAL_CTEST", ctest);
    write_executable_script(
        bin / "ctest",
        "#!/bin/sh\n"
        "if [ \"$1\" != '--version' ]; then\n"
        "  printf '# deliberately missing test registration\\n' > "
        "CTestTestfile.cmake\n"
        "fi\n"
        "exec \"$MANIFESTO_TEST_REAL_CTEST\" \"$@\"\n"
    );
    for (const auto* request :
         { "check tests", "check coverage", "check ci" }) {
        result = run_engels_cli(project, request);
        require_true(
            result.exit_code == 5,
            "no registered tests must fail required CTest execution:\n"
                + result.output
        );
        require_contains(
            result.output, "No tests were found",
            "real CTest must reject the empty selected test run"
        );
        require_not_contains(
            result.output, "ci checks passed",
            "an empty test run cannot satisfy aggregate acceptance"
        );
    }
}

void test_cli_check_ci_fails_fast_on_repository_policy_drift() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    write_text(
        root.path() / "scripts/build.sh", "#!/usr/bin/env bash\nexit 0\n"
    );

    const cli_result result = run_cli(root.path(), "check ci");
    require_true(
        result.exit_code != 0,
        "ecos check ci must fail when repository policy drift is present"
    );
    require_contains(
        result.output, "forbidden repository entries:",
        "ecos check ci must run repository policy checks before "
        "external tooling checks"
    );
    require_not_contains(
        result.output, "clang-format is not available",
        "ecos check ci must fail on repository drift before "
        "probing format tooling"
    );
    require_not_contains(
        result.output, "clang-tidy is not available",
        "ecos check ci must fail on repository drift before "
        "probing tidy tooling"
    );
}

} // namespace ecosystem_test_support
