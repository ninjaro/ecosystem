#include "../ci.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_ci_analysis_cache_saving_requires_evidence() {
    temp_dir root;
    const auto files = ecosystem::generate_tracked_surface_files(
        sample_manifest(), root.path()
    );
    const auto* checks
        = find_tracked_surface_file(files, ".github/workflows/tests.yml");
    require_true(checks != nullptr, "Checks must have a generated workflow");
    require_contains(
        checks->contents,
        "- name: Save verified analysis cache\n"
        "        if: ${{ always() && steps.required.outcome == 'success' && "
        "github.event_name != 'pull_request' && "
        "steps.analysis_cache.outputs.available == 'true' }}",
        "cache saving requires successful verification and actual evidence"
    );
    const auto script = root.path() / "cache.sh";
    const auto outputs = root.path() / "outputs";
    const auto summary = root.path() / "summary";
    const auto cache = root.path() / ".ecosystem/cache/verification";
    write_text(
        script, ci_shell(checks->contents, "Inspect verified analysis cache", 8)
    );
    for (const auto* state : { "missing", "empty", "invalid", "available" }) {
        if (std::string(state) == "empty")
            fs::create_directories(cache);
        if (std::string(state) == "invalid") {
            fs::create_directory(cache / "directory.json");
            write_text(cache / "empty.json", "");
            write_text(root.path() / "external.json", "not cache evidence");
            fs::create_symlink(
                root.path() / "external.json", cache / "alias.json"
            );
        }
        if (std::string(state) == "available")
            write_text(
                cache / "ast-key.json", R"({"schema":1,"status":"clean"})"
            );
        write_text(outputs, "");
        write_text(summary, "");
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", script.string() }, root.path(),
            { { "GITHUB_OUTPUT", outputs.string() },
              { "GITHUB_STEP_SUMMARY", summary.string() } }
        );
        const bool available = std::string(state) == "available";
        require_true(
            result.exit_code == 0
                && read_text(outputs)
                    == (available ? "available=true\n" : "available=false\n"),
            "absence of clean analysis evidence must not trigger a cache save"
        );
        require_contains(
            read_text(summary),
            available ? "1 evidence files"
                      : "no reusable evidence was produced",
            "cache availability must have an observable reason"
        );
    }
}

void test_ci_selection() {
    temp_dir root;
    const auto files = ecosystem::generate_tracked_surface_files(
        sample_manifest(), root.path()
    );
    const auto* action = find_tracked_surface_file(
        files, ".github/actions/select-ecosystem-ci/action.yml"
    );
    const auto* checks
        = find_tracked_surface_file(files, ".github/workflows/tests.yml");
    const auto* codeql
        = find_tracked_surface_file(files, ".github/workflows/codeql.yml");
    require_true(
        action && checks && codeql, "all CI selection consumers exist"
    );
    for (const auto* workflow : { checks, codeql }) {
        require_contains(
            workflow->contents,
            "VERIFICATION_SCOPE: ${{ steps.classification.outputs.scope }}",
            "receipts must require an explicit full verification scope"
        );
        require_contains(
            workflow->contents, "fetch-depth: 0\n      - id: selection",
            "change resolution requires history before selection"
        );
        require_contains(
            workflow->contents,
            "changes: ${{ steps.selection.outputs.changes }}",
            "later jobs must receive the same structured event delta"
        );
        require_contains(
            workflow->contents,
            "path: ${{ steps.selection.outputs.changes-file }}",
            "change resolution must remain inspectable as an artifact"
        );
    }
    require_contains(
        action->contents,
        "require(process.env.CHANGES_MODULE).collect({context, core});",
        "selection must collect event changes without duplicating local policy"
    );
    const auto selection = root.path() / "select.sh";
    const auto outputs = root.path() / "outputs";
    write_text(
        selection, ci_shell(action->contents, "Select verification scope", 6)
    );
    for (const auto& [event, ref, branch, base, expected] :
         std::vector<std::tuple<
             std::string, std::string, std::string, std::string, std::string>> {
             { "push", "refs/heads/main", "main", "", "full" },
             { "push", "refs/heads/trunk", "trunk", "", "full" },
             { "push", "refs/heads/feature", "main", "", "cheap" },
             { "push", "refs/tags/main", "main", "", "skip" },
             { "pull_request", "refs/pull/42/merge", "main", "main", "full" },
             { "pull_request", "refs/pull/42/merge", "main", "release",
               "cheap" },
             { "workflow_dispatch", "refs/heads/feature", "main", "", "full" },
             { "schedule", "refs/heads/main", "main", "", "full" },
             { "release", "refs/tags/v1", "main", "", "skip" },
             { "push", "refs/heads/main", "", "", "error" },
             { "pull_request", "refs/pull/42/merge", "main", "", "error" } }) {
        write_text(outputs, "");
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", selection.string() }, root.path(),
            { { "EVENT_NAME", event },
              { "CURRENT_REF", ref },
              { "DEFAULT_BRANCH", branch },
              { "PR_BASE_REF", base },
              { "GITHUB_OUTPUT", outputs.string() },
              { "GITHUB_STEP_SUMMARY", (root.path() / "summary").string() } }
        );
        require_true(
            (result.exit_code == 0) == (expected != "error"),
            "event selection must fail when required context is missing"
        );
        require_true(
            read_text(outputs)
                == (expected == "error" ? "" : "scope=" + expected + "\n"),
            std::string("event selection mismatch: ")
                .append(event)
                .append(" ")
                .append(ref)
        );
    }
    write_text(outputs, "");
    const auto scheduled_security = ecosystem::capture_command_result(
        { "bash", "-e", "-o", "pipefail", selection.string() }, root.path(),
        { { "EVENT_NAME", "schedule" },
          { "CHECK_KIND", "codeql" },
          { "DEFAULT_BRANCH", "main" },
          { "CURRENT_REF", "refs/heads/main" },
          { "GITHUB_OUTPUT", outputs.string() },
          { "GITHUB_STEP_SUMMARY", (root.path() / "summary").string() } }
    );
    require_true(
        scheduled_security.exit_code == 0
            && read_text(outputs) == "scope=security\n",
        "scheduled CodeQL remains full security while scheduled Checks is full "
        "native"
    );
    require_contains(
        codeql->contents, "CCACHE_DISABLE: '1'",
        "CodeQL tracing must execute compilers"
    );
    require_not_contains(
        checks->contents, "build debug",
        "required CI must not duplicate the production build"
    );
    require_contains(
        codeql->contents, "  schedule:\n    - cron:",
        "CodeQL must maintain an independent scheduled default-branch baseline"
    );
    require_contains(
        codeql->contents,
        "group: codeql-${{ github.workflow }}-${{ github.event_name }}-",
        "scheduled CodeQL must not cancel or be cancelled by push verification"
    );
    require_contains(
        codeql->contents,
        "if: ${{ needs.select.outputs.scope == 'cheap' || "
        "needs.select.outputs.scope == 'full' || "
        "needs.select.outputs.scope == 'security' }}",
        "native classification decides cheap-branch analysis; unsupported tags "
        "skip"
    );
    require_contains(
        checks->contents,
        "if: ${{ needs.select.outputs.scope == 'cheap' || "
        "needs.select.outputs.scope == 'full' }}",
        "required checks must skip package/release events"
    );
    require_contains(
        checks->contents,
        "if: ${{ steps.classification.outputs.scope == 'full' && "
        "steps.required.outputs.status == 'passed' }}",
        "coverage must require a successful full verification stage"
    );
    require_contains(
        checks->contents, "permissions:\n  contents: read\n\n",
        "project checks must have read-only repository credentials"
    );

    require_contains(
        checks->contents,
        "install-project-deps: ${{ needs.select.outputs.scope == 'full' }}",
        "cheap verification must not install project build/test dependencies"
    );
    require_contains(
        checks->contents, "fetch-depth: 0",
        "fresh verification requires history in its own checkout"
    );
    require_contains(
        checks->contents,
        "- name: Resolve checked-out changes\n"
        "        id: changes\n",
        "fresh checks must bind changes to their own checkout before setup"
    );
    const auto* setup = find_tracked_surface_file(
        files, ".github/actions/setup-ecosystem/action.yml"
    );
    require_true(setup != nullptr, "local dependency setup must exist");
    require_contains(
        setup->contents,
        "- name: Install Qt 6\n      if: ${{ inputs.install-project-deps == "
        "'true' }}",
        "cheap verification must skip Qt installation"
    );
    const auto native_setup
        = ci_shell(setup->contents, "Install shared native dependencies", 6);
    const auto bin = root.path() / "bin";
    write_executable_script(bin / "sudo", "#!/bin/sh\nprintf '%s\\n' \"$*\"\n");
    // Synthetic package roots reproduce the hosted Ubuntu image: an older
    // incomplete Clang export is found before the installed default SDK.
    const auto stale = root.path() / "LLVM 17";
    const auto installed = root.path() / "LLVM 18/lib/cmake";
    write_text(stale / "lib/cmake/llvm/LLVMConfig.cmake", "");
    write_text(
        stale / "lib/cmake/clang/ClangConfig.cmake",
        "message(FATAL_ERROR \"The imported target clangBasic references the "
        "missing file libclangBasic.a\")\n"
    );
    write_text(installed / "llvm/LLVMConfig.cmake", "set(TEST_LLVM 18)\n");
    write_text(installed / "clang/ClangConfig.cmake", "set(TEST_CLANG 18)\n");
    write_executable_script(
        bin / "llvm-config",
        "#!/bin/sh\n[ \"$1\" = --cmakedir ] || exit 2\n"
        "printf '%s\\n' \"$CI_LLVM_CMAKE_DIR\"\n"
    );
    const auto probe = root.path() / "sdk-probe";
    write_text(
        probe / "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(sdk_selection LANGUAGES NONE)\n"
        "find_package(LLVM CONFIG REQUIRED)\n"
        "find_package(Clang CONFIG REQUIRED)\n"
        "if(NOT TEST_LLVM STREQUAL \"18\" OR NOT TEST_CLANG STREQUAL \"18\")\n"
        "  message(FATAL_ERROR \"SDK selection differs\")\nendif()\n"
    );
    const auto configure = [&](const fs::path& build, const auto& env) {
        return ecosystem::capture_command_result(
            { "cmake", "-S", probe.string(), "-B", build.string() },
            root.path(), env
        );
    };
    const auto unselected = configure(
        root.path() / "before-sdk-selection",
        std::vector<std::pair<std::string, std::string>> {
            { "CMAKE_PREFIX_PATH", stale.string() },
            { "LLVM_DIR", "" },
            { "Clang_DIR", "" } }
    );
    require_true(unselected.exit_code != 0, "the stale hosted SDK must fail");
    require_contains(
        unselected.output, "clangBasic", "reproduce the hosted error"
    );
    const auto dependencies = root.path() / "dependencies.sh";
    const auto environment = root.path() / "sdk-environment";
    for (const auto& [project_deps, docs] :
         std::vector<std::pair<std::string, std::string>> {
             { "false", "false" }, { "true", "false" }, { "false", "true" } }) {
        auto script = ci_bind(
            native_setup, "inputs.install-project-deps", project_deps
        );
        script = ci_bind(script, "inputs.install-docs", docs);
        write_text(dependencies, script);
        write_text(environment, "");
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", dependencies.string() },
            root.path(),
            { { "PATH", bin.string() + ":" + current_path_env() },
              { "GITHUB_ENV", environment.string() },
              { "CI_LLVM_CMAKE_DIR", (installed / "llvm").string() },
              { "GITHUB_WORKSPACE", root.path().string() } }
        );
        require_true(
            result.exit_code == 0, "dependency selection must execute"
        );
        for (const auto* package :
             { "clang-format", "libclang-dev", "llvm-dev", "libcxxopts-dev" })
            require_contains(
                result.output, package,
                "tool bootstrap prerequisites remain required"
            );
        for (const auto* package :
             { "clang-tidy", "default-jdk", "libopencv-dev", "libgtest-dev",
               "libbenchmark-dev" })
            require_true(
                (result.output.find(package) != std::string::npos)
                    == (project_deps == "true"),
                "project dependencies must follow the selected scope"
            );
        require_true(
            (result.output.find("doxygen graphviz") != std::string::npos)
                == (project_deps == "true" || docs == "true"),
            "native docs dependencies support full tests or explicit "
            "documentation"
        );
        for (const auto* package :
             { "python3-venv", "poppler-utils", "ffmpeg" })
            require_true(
                (result.output.find(package) != std::string::npos)
                    == (docs == "true"),
                "presentation dependencies remain explicit"
            );
        std::vector<std::pair<std::string, std::string>> sdk_env {
            { "CMAKE_PREFIX_PATH", stale.string() },
            { "LLVM_DIR", "" },
            { "Clang_DIR", "" }
        };
        std::istringstream exported(read_text(environment));
        std::string line;
        while (std::getline(exported, line)) {
            const auto split = line.find('=');
            require_true(split != std::string::npos, "SDK export is KEY=VALUE");
            sdk_env.emplace_back(line.substr(0, split), line.substr(split + 1));
        }
        const auto configured = configure(
            root.path() / ("selected-sdk-" + project_deps + "-" + docs), sdk_env
        );
        require_true(
            configured.exit_code == 0,
            "minimal/full/docs bootstrap must select the installed LLVM/Clang "
            "SDK despite the older broken export:\n"
                + configured.output
        );
    }
    for (const auto* missing :
         { "llvm/LLVMConfig.cmake", "clang/ClangConfig.cmake" }) {
        const auto file = installed / missing;
        const auto contents = read_text(file);
        fs::remove(file);
        write_text(environment, "");
        const auto failed = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", dependencies.string() },
            root.path(),
            { { "PATH", bin.string() + ":" + current_path_env() },
              { "GITHUB_WORKSPACE", root.path().string() },
              { "CI_LLVM_CMAKE_DIR", (installed / "llvm").string() },
              { "GITHUB_ENV", environment.string() } }
        );
        require_true(
            failed.exit_code != 0 && read_text(environment).empty(),
            "incomplete installed SDKs must fail without partial exports"
        );
        write_text(file, contents);
    }

    // Execute the exact generated local-operation selection with recording
    // actors.
    for (const auto* actor : { "engels", "marx" })
        write_executable_script(
            root.path() / actor,
            "#!/bin/sh\nprintf '" + std::string(actor)
                + " %s\\n' \"$*\" >> \"$CI_CALLS\"\n"
                  "if [ \"$CI_FAIL_COMMAND\" = \""
                + actor + " $*\" ]; then exit 5; fi\n"
        );
    auto content = checks->contents;
    const auto command_key = content.find("shell-command: |");
    require_true(command_key != std::string::npos, "required operation exists");
    content.replace(command_key, std::string("shell-command").size(), "run");
    auto program = ci_shell(content, "Run required local checks", 10);
    for (const auto* actor : { "engels", "marx" })
        program = ci_bind(
            program,
            "steps.ecosystem.outputs." + std::string(actor) + "-binary",
            (root.path() / actor).string()
        );
    const auto command = root.path() / "checks.sh";
    const auto calls = root.path() / "calls";
    write_text(command, program);
    for (const auto& [scope, failure, expected] :
         std::vector<std::tuple<std::string, std::string, std::string>> {
             { "cheap", "",
               "engels check repo\nengels check format --changes\n" },
             { "full", "", "engels check ci\n" },
             { "cheap", "engels check repo", "engels check repo\n" },
             { "cheap", "engels check format --changes",
               "engels check repo\nengels check format --changes\n" },
             { "full", "engels check ci", "engels check ci\n" },
             { "missing", "invalid scope", "" } }) {
        write_text(calls, "");
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", command.string() }, root.path(),
            { { "CHECK_SCOPE", scope },
              { "CI_FAIL_COMMAND", failure },
              { "CI_CALLS", calls.string() } }
        );
        require_true(
            (result.exit_code == 0) == failure.empty()
                && read_text(calls) == expected,
            "selected operations must preserve order and failure propagation:\n"
                + result.output
        );
    }

    const auto gate = root.path() / "gate.sh";
    write_text(gate, ci_shell(checks->contents, "Enforce required result", 8));
    const auto reports = root.path() / ".ecosystem/github/reports";
    for (const auto* stage : { "01-required-checks", "02-coverage-check" }) {
        write_text(reports / stage / "summary.md", "current result");
        write_text(reports / stage / "output.log", "");
    }
    const auto report = root.path() / ".ecosystem/reports/coverage.json";
    write_text(report, "current coverage");
    write_text(reports / "00-workflow/result.json", R"({"status":"passed"})");
    const auto run_gate = [&](const std::string& outcome,
                              const std::string& status,
                              const std::string& code) {
        return ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", gate.string() }, root.path(),
            { { "DIAGNOSTIC_OUTCOME", "success" },
              { "DIAGNOSTIC_STATUS", "passed" },
              { "CHECK_SCOPE", "full" },
              { "CHECK_OUTCOME", "success" },
              { "CHECK_STATUS", "passed" },
              { "CHECK_EXIT_CODE", "0" },
              { "COVERAGE_OUTCOME", outcome },
              { "COVERAGE_STATUS", status },
              { "COVERAGE_EXIT_CODE", code } }
        );
    };
    require_true(
        run_gate("success", "passed", "0").exit_code == 0,
        "full coverage may pass"
    );
    for (const auto& [outcome, status, code] :
         std::vector<std::tuple<std::string, std::string, std::string>> {
             { "", "", "" },
             { "failure", "passed", "0" },
             { "skipped", "skipped", "3" },
             { "success", "failed", "5" },
             { "success", "skipped", "4" },
             { "success", "passed", "3" },
             { "success", "failed", "0" } })
        require_true(
            run_gate(outcome, status, code).exit_code != 0,
            "full verification cannot hide coverage failure"
        );
    fs::remove(report);
    require_true(
        run_gate("success", "passed", "0").exit_code != 0,
        "successful coverage must have its report"
    );
    require_true(
        run_gate("success", "skipped", "3").exit_code == 0,
        "unsupported coverage must be explicit"
    );
    fs::remove(reports / "02-coverage-check/output.log");
    require_true(
        run_gate("success", "skipped", "3").exit_code != 0,
        "even unsupported coverage needs a completed stage record"
    );
}

} // namespace ecosystem_test_support
