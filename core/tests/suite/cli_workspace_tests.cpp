#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

void test_workspace_conformance_reports_all_projects() {
    temp_dir root;
    for (const auto* name : { "alpha", "beta" }) {
        write_sample_workspace_project(root.path(), name);
        require_sync_success(root.path() / name, "must sync fixture project");
    }
    require_true(
        workspace_conformance_errors(root.path()).empty(),
        "fresh isolated workspace must conform"
    );
    write_text(root.path() / "broken/manifest.json", "{ invalid JSON\n");
    write_text(root.path() / "alpha/CMakeLists.txt", "stale\n");
    write_text(root.path() / "beta/.clang-format", "stale\n");
    const auto errors = join_lines(workspace_conformance_errors(root.path()));
    for (const auto* issue :
         { "broken", "alpha/CMakeLists.txt", "beta/.clang-format" })
        require_contains(
            errors, issue,
            "conformance must report every broken project and surface"
        );
    require_true(
        read_text(root.path() / "alpha/CMakeLists.txt") == "stale\n",
        "conformance must be read-only"
    );
}

void test_cli_list_artifacts() {
    const cli_result result
        = run_cli(fs::path(ECOS_TEST_SOURCE_DIR), "list artifacts");
    require_true(
        result.exit_code == 0, "manifesto list artifacts must succeed"
    );
    require_not_contains(
        result.output, "manifesto:manifesto",
        "no dispatcher artifact may remain"
    );
    require_contains(
        result.output, "marx:marx : exe",
        "list artifacts must show the marx frontend artifact"
    );
    require_contains(
        result.output, "engels:engels : exe",
        "list artifacts must show the engels frontend artifact"
    );
}

void test_cli_actor_batches_preserve_ownership() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    const cli_result wrong_owner
        = run_marx_cli(root.path(), "sync --then list artifacts");
    require_true(wrong_owner.exit_code != 0, "cross-actor batch must fail");
    require_contains(
        wrong_owner.output, "available via engels",
        "batch must identify the owner"
    );
    require_true(
        !fs::exists(root.path() / "CMakeLists.txt"),
        "batch ownership must be checked before any mutation"
    );
    const cli_result reads = run_engels_cli(
        root.path(), "--quiet list artifacts --then report matrix"
    );
    require_true(
        reads.exit_code == 0 && reads.output.empty(),
        "quiet same-actor batch must work"
    );
    const cli_result writes = run_marx_cli(root.path(), "sync --then sync");
    require_true(
        writes.exit_code == 0 && fs::exists(root.path() / "CMakeLists.txt"),
        "Marx must retain same-actor chaining"
    );
    const cli_result failure
        = run_marx_cli(root.path(), "--quiet build nonsense --then sync");
    require_true(
        failure.exit_code != 0 && !failure.output.empty(),
        "quiet mode must retain failures"
    );
    const fs::path shell_input = root.path() / "shell-input";
    write_text(shell_input, "sync\nqueue\nexit\n");
    const cli_result shell = run_engels_cli(
        root.path(), "--interactive < '" + shell_input.string() + "'"
    );
    require_true(
        shell.exit_code == 0, "explicit queued shell must exit cleanly"
    );
    require_contains(
        shell.output, "available via marx", "shell must preserve ownership"
    );
    require_not_contains(
        shell.output, "queued #", "shell must never enqueue a foreign operation"
    );
    const cli_result bad
        = run_engels_cli(root.path(), "--interactive list artifacts");
    require_true(
        bad.exit_code != 0, "interactive mode must reject queued arguments"
    );
    require_contains(
        bad.output, "interactive mode does not accept queued requests",
        "actor mode error must remain explicit"
    );
}

void test_cli_actor_help_and_queue_validation() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    for (const fs::path& binary :
         { marx_binary_path(), engels_binary_path() }) {
        const auto short_help = run_cli_with_binary(binary, root.path(), "-h");
        const auto long_help
            = run_cli_with_binary(binary, root.path(), "--help");
        const auto welcome = run_cli_with_binary(binary, root.path(), "");
        require_true(
            welcome.exit_code == 0 && welcome.output == long_help.output,
            "a visitor launch without arguments must show help successfully"
        );
        const auto empty_quiet
            = run_cli_with_binary(binary, root.path(), "--quiet");
        require_true(
            empty_quiet.exit_code != 0,
            "request options without a command must still fail"
        );
        require_true(
            short_help.exit_code == 0 && short_help.output == long_help.output,
            "both public actors must retain the short help option"
        );
        const bool marx = binary.filename() == "marx";
        require_contains(
            long_help.output, marx ? "  format [" : "check profiles:",
            "help must expose the canonical daily operations"
        );
        require_true(
            run_cli_with_binary(binary, root.path(), "init").exit_code != 0,
            "init must remain outside the public daily interface"
        );
        const fs::path input = root.path() / "queue-input";
        write_text(
            input,
            std::string(
                marx ? "sync --then list artifacts"
                     : "list artifacts --then sync"
            )
                + "\nlog 1junk\nskip -1\nlog +1\nlog "
                  "184467440737095516160000\nlog 1\nexit\n"
        );
        const auto shell = run_cli_with_binary(
            binary, root.path(), "--interactive < '" + input.string() + "'"
        );
        require_true(
            shell.exit_code == 0, "shell must recover from invalid requests"
        );
        require_not_contains(
            shell.output, "queued #",
            "mixed-actor lines must be rejected before any enqueue"
        );
        require_true(
            !fs::exists(root.path() / "CMakeLists.txt"),
            "mixed-actor lines must not start writes"
        );
        for (const std::string id :
             { "1junk", "-1", "+1", "184467440737095516160000" }) {
            require_contains(
                shell.output, "invalid queue id: " + id,
                "queue identifiers must reject suffixes, signs and overflow"
            );
        }
        require_contains(
            shell.output, "unknown queue item: 1",
            "valid numeric IDs must still reach queue lookup"
        );
    }
}

void test_cli_all_operations_have_one_actor() {
    temp_dir root;
    for (const std::string command : { "list", "check", "doctor", "report" }) {
        const cli_result wrong = run_marx_cli(root.path(), command);
        require_true(
            wrong.exit_code != 0, "Marx must reject every Engels operation"
        );
        require_contains(
            wrong.output, "available via engels",
            "Engels ownership must be explicit"
        );
    }
    for (const std::string command : { "sync", "format", "mutate", "build",
                                       "benchmark", "run", "prerelease" }) {
        const cli_result wrong = run_engels_cli(root.path(), command);
        require_true(
            wrong.exit_code != 0, "Engels must reject every Marx operation"
        );
        require_contains(
            wrong.output, "available via marx",
            "Marx ownership must be explicit"
        );
    }
}

void test_cli_engels_rejects_marx_commands() {
    const cli_result result
        = run_engels_cli(fs::path(ECOS_TEST_SOURCE_DIR), "sync");
    require_true(
        result.exit_code != 0,
        "engels must reject write-capable commands owned by marx"
    );
    require_contains(
        result.output, "command 'sync' is available via marx; run `marx sync`",
        "engels must route sync requests to the marx frontend"
    );
}

void test_cli_marx_rejects_engels_commands() {
    const cli_result result
        = run_marx_cli(fs::path(ECOS_TEST_SOURCE_DIR), "report matrix");
    require_true(
        result.exit_code != 0,
        "marx must reject diagnostic commands owned by engels"
    );
    require_contains(
        result.output,
        "command 'report' is available via engels; run `engels report`",
        "marx must route report requests to the engels frontend"
    );
}

void test_cli_workspace_list() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    write_text(root.path() / "tooling/readme.md", "Unmanaged tools.\n");
    const cli_result result = run_cli(root.path(), "list");
    require_true(
        result.exit_code == 0, "workspace manifesto list must succeed"
    );
    require_contains(
        result.output,
        "workspace:", "workspace list must identify the workspace root"
    );
    require_contains(
        result.output, "alpha", "workspace list must include alpha"
    );
    require_contains(result.output, "beta", "workspace list must include beta");
    require_not_contains(
        result.output, "tooling",
        "workspace list must ignore non-managed legacy tooling"
    );
}

void test_invalid_workspace_projects_remain_visible() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_text(root.path() / "broken/manifest.json", "{ invalid JSON\n");
    write_text(
        root.path() / "structural/manifest.json", "{\"id\":\"damaged\"}\n"
    );
    write_text(root.path() / "unmanaged/note.txt", "not a project\n");

    const auto workspace = ecosystem::discover_workspace(root.path());
    require_true(
        workspace.has_value() && workspace->projects.size() == 3U,
        "discovery must retain valid and invalid manifests only"
    );
    const auto* broken
        = ecosystem::find_workspace_project(*workspace, "broken");
    const auto* damaged
        = ecosystem::find_workspace_project(*workspace, "damaged");
    require_true(
        broken != nullptr && !broken->valid() && !broken->errors.empty(),
        "malformed JSON must retain path identity and errors"
    );
    require_true(
        damaged != nullptr && !damaged->valid(),
        "structural failures must retain parsed identity"
    );

    for (const std::string target : { "", "projects", "components", "artifacts",
                                      "profiles", "platforms" }) {
        const cli_result result = run_cli(root.path(), "list " + target);
        require_true(
            result.exit_code == 0, "listing invalid state must succeed"
        );
        require_contains(
            result.output, "broken/manifest.json [invalid]",
            "list must expose invalid paths"
        );
        require_contains(
            result.output, "damaged",
            "list must expose parsed invalid identities"
        );
    }
    const cli_result matrix
        = run_cli(root.path(), "report matrix --project damaged");
    require_true(matrix.exit_code == 0, "reporting invalid state must succeed");
    const json project = json::parse(matrix.output).at("projects").at(0);
    require_true(
        !project.at("valid").get<bool>() && !project.at("errors").empty(),
        "canonical workspace matrix must carry validity and errors"
    );
    require_true(
        project.at("manifest_path") == "structural/manifest.json",
        "invalid report must identify its manifest"
    );

    for (const std::string request :
         { "check repo", "check repo --project broken",
           "check repo --project damaged", "sync", "format", "build debug",
           "benchmark", "prerelease", "run debug --project broken",
           "check tests broken/core:core" }) {
        const cli_result result = run_cli(root.path(), request);
        require_true(
            result.exit_code != 0,
            "invalid selected state must fail: " + request
        );
        require_contains(
            result.output, "invalid project:",
            "failure must distinguish invalid from unknown projects"
        );
    }
    require_true(
        !fs::exists(root.path() / "alpha/CMakeLists.txt")
            && !fs::exists(root.path() / "alpha/.ecosystem"),
        "workspace writes must validate all selected projects before side "
        "effects"
    );
    const cli_result unknown = run_cli(root.path(), "sync --project absent");
    require_contains(
        unknown.output, "unknown project: absent",
        "unknown selection must remain distinct"
    );
    const cli_result filtered = run_cli(root.path(), "sync --project alpha");
    require_true(
        filtered.exit_code == 0,
        "a valid explicit scope may exclude invalid siblings"
    );
}

void test_invalid_workspace_group_and_root_manifest() {
    temp_dir root;
    write_text(root.path() / "broken/manifest.json", "[]\n");
    write_text(
        root.path() / "manifesto.workspace.json",
        "{\"groups\":{\"broken_group\":[\"broken\"]}}\n"
    );
    const auto workspace = ecosystem::discover_workspace(root.path());
    require_true(
        workspace.has_value() && workspace->errors.empty(),
        "a group member with an invalid manifest is known"
    );
    const cli_result result = run_cli(root.path(), "sync --group broken_group");
    require_true(result.exit_code != 0, "invalid group must prevent sync");
    require_contains(
        result.output, "invalid project: broken",
        "group failure must attribute its invalid member"
    );
    write_text(root.path() / "manifest.json", "{ broken\n");
    const cli_result project = run_cli(root.path(), "list");
    require_true(
        project.exit_code != 0,
        "a malformed root manifest must not fall through to workspace discovery"
    );
    require_contains(
        project.output, "invalid JSON in manifest",
        "root manifest errors must be preserved"
    );
}

void test_cli_workspace_list_groups() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    write_sample_workspace_project(root.path(), "gamma");
    write_sample_workspace_config(root.path());

    const cli_result result = run_cli(root.path(), "list groups");
    require_true(
        result.exit_code == 0,
        "workspace ecos list groups must succeed when a workspace config exists"
    );
    require_contains(
        result.output, "core : alpha beta",
        "workspace list groups must emit the configured core group"
    );
    require_contains(
        result.output, "apps : gamma",
        "workspace list groups must emit the configured apps group"
    );
}

void test_cli_workspace_group_filter_selects_configured_projects() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    write_sample_workspace_project(root.path(), "gamma");
    write_sample_workspace_config(root.path());

    const cli_result result = run_cli(root.path(), "build debug --group core");
    require_true(
        result.exit_code == 0,
        "workspace ecos build must accept named workspace groups"
    );
    require_contains(
        result.output,
        "== alpha (alpha) ==", "workspace group selection must include alpha"
    );
    require_contains(
        result.output,
        "== beta (beta) ==", "workspace group selection must include beta"
    );
    require_not_contains(
        result.output, "gamma",
        "workspace group selection must exclude projects outside the group"
    );
}

void test_cli_workspace_group_filter_rejects_unknown_groups() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");

    const cli_result result = run_cli(root.path(), "build debug --group core");
    require_true(
        result.exit_code != 0,
        "workspace group filters must reject unknown group ids"
    );
    require_contains(
        result.output, "unknown workspace group: core",
        "workspace group filter errors must name the missing group"
    );
}

void test_cli_workspace_config_rejects_unknown_group_project_selector() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_text(
        root.path() / "manifesto.workspace.json",
        "{\n"
        "  \"groups\": {\n"
        "    \"broken\": [\"missing\"]\n"
        "  }\n"
        "}\n"
    );

    const cli_result result = run_cli(root.path(), "list");
    require_true(
        result.exit_code != 0,
        "invalid workspace group selectors must reject workspace commands"
    );
    require_contains(
        result.output,
        "manifesto.workspace.json.groups.broken references unknown project "
        "selector: missing",
        "workspace config errors must explain the broken selector"
    );
}

void test_cli_workspace_build_on_sample_workspace() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");

    const cli_result sync_result = run_cli(root.path(), "sync");
    require_true(
        sync_result.exit_code == 0,
        "workspace ecos sync must succeed on sample workspace"
    );

    const cli_result build_result
        = run_cli(root.path(), "build debug --project alpha");
    require_true(
        build_result.exit_code == 0,
        "workspace ecos build debug must succeed with a project filter"
    );
    require_contains(
        build_result.output,
        "== alpha (alpha) ==", "workspace build must identify the project"
    );
    require_contains(
        build_result.output, "built alpha:app",
        "workspace build must build the facade entry artifact"
    );
    require_not_contains(
        build_result.output, "beta",
        "workspace build filter must exclude other projects"
    );
    require_true(
        fs::exists(root.path() / "alpha/.ecosystem/source/CMakeLists.txt"),
        "workspace build must materialize the local developer surface"
    );
    require_contains(
        read_text(root.path() / "alpha/.ecosystem/source/CMakeLists.txt"),
        "get_filename_component(ECOSYSTEM_PROJECT_ROOT "
        "\"${CMAKE_CURRENT_SOURCE_DIR}/../..\" ABSOLUTE)",
        "workspace build must use the generated local developer surface"
    );
    require_not_contains(
        read_text(root.path() / "alpha/CMakeLists.txt"),
        "ECOSYSTEM_BUILD_TESTS",
        "workspace sync must keep the tracked facade slim"
    );
}

void test_cli_workspace_build_supports_multiple_artifact_filters() {
    temp_dir root;
    write_sample_dual_run_workspace_project(root.path(), "alpha");
    write_sample_dual_run_workspace_project(root.path(), "beta");

    const cli_result result
        = run_cli(root.path(), "build debug alpha/app:app beta/tool:cli");
    require_true(
        result.exit_code == 0,
        "workspace ecos build must support multiple qualified artifact filters"
    );
    require_contains(
        result.output, "== alpha (alpha) ==",
        "multi-artifact workspace build must identify alpha"
    );
    require_contains(
        result.output, "built app:app",
        "multi-artifact workspace build must build the selected alpha artifact"
    );
    require_contains(
        result.output,
        "== beta (beta) ==", "multi-artifact workspace build must identify beta"
    );
    require_contains(
        result.output, "built tool:cli",
        "multi-artifact workspace build must build the selected beta artifact"
    );
}

void test_cli_workspace_sync_supports_project_filter() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");

    const cli_result sync_result = run_cli(root.path(), "sync --project alpha");
    require_true(
        sync_result.exit_code == 0,
        "workspace ecos sync must support a project filter"
    );
    require_contains(
        sync_result.output, "alpha: wrote alpha/CMakeLists.txt",
        "workspace sync filter must write alpha"
    );
    require_not_contains(
        sync_result.output,
        "beta:", "workspace sync filter must exclude beta output"
    );
    require_true(
        fs::exists(root.path() / "alpha/CMakeLists.txt"),
        "workspace sync filter must generate alpha facade"
    );
    require_true(
        fs::exists(root.path() / "alpha/.gitignore"),
        "workspace sync filter must generate alpha gitignore"
    );
    require_true(
        !fs::exists(root.path() / "beta/CMakeLists.txt"),
        "workspace sync filter must not generate non-selected project facades"
    );
}

void test_cli_workspace_sync_supports_multi_project_filter() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    write_sample_workspace_project(root.path(), "gamma");

    const cli_result sync_result
        = run_cli(root.path(), "sync --project alpha --project beta");
    require_true(
        sync_result.exit_code == 0,
        "workspace ecos sync must support multiple project filters"
    );
    require_contains(
        sync_result.output, "alpha: wrote alpha/CMakeLists.txt",
        "workspace sync multi-project filter must write alpha"
    );
    require_contains(
        sync_result.output, "beta: wrote beta/CMakeLists.txt",
        "workspace sync multi-project filter must write beta"
    );
    require_not_contains(
        sync_result.output,
        "gamma:", "workspace sync multi-project filter must exclude gamma"
    );
    require_true(
        fs::exists(root.path() / "alpha/CMakeLists.txt"),
        "workspace sync must generate alpha facade"
    );
    require_true(
        fs::exists(root.path() / "beta/CMakeLists.txt"),
        "workspace sync must generate beta facade"
    );
    require_true(
        !fs::exists(root.path() / "gamma/CMakeLists.txt"),
        "workspace sync must not generate gamma facade"
    );
}

void test_cli_workspace_unqualified_artifact_filter_rejects_multi_project_selection() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");

    const cli_result result = run_cli(
        root.path(), "report matrix --project alpha --project beta alpha:app"
    );
    require_true(
        result.exit_code != 0,
        "unqualified workspace artifact filters must reject multi-project "
        "selection"
    );
    require_contains(
        result.output,
        "unqualified workspace artifact filters require exactly one selected "
        "workspace project",
        "unqualified workspace artifact filter error must explain the "
        "ambiguity"
    );
}

void test_cli_run_executes_facade_entry() {
    temp_dir root;
    write_sample_build_project(
        root.path(), "sample",
        "#include <iostream>\nint main() { std::cout << "
        "\"sample-run\\n\"; return 0; }\n"
    );

    const cli_result result = run_cli(root.path(), "run debug");
    require_true(
        result.exit_code == 0, "ecos run must execute the facade entry artifact"
    );
    require_contains(
        result.output, "sample-run",
        "ecos run must execute the facade entry binary"
    );
    require_contains(
        result.output, "ran sample:app",
        "ecos run must report the executed artifact"
    );
}

void test_cli_run_executes_requested_artifact_with_passthrough_args() {
    temp_dir root;
    write_sample_dual_run_project(root.path());

    const cli_result result
        = run_cli(root.path(), "run debug tool:cli -- alpha --then beta");
    require_true(
        result.exit_code == 0,
        "ecos run must execute an explicitly selected runnable artifact"
    );
    require_contains(
        result.output, "tool-run alpha --then beta",
        "ecos run must forward passthrough arguments to the child artifact"
    );
    require_not_contains(
        result.output, "app-run",
        "ecos run must not execute the facade entry when "
        "another artifact was requested"
    );
}

void test_cli_workspace_run_executes_selected_project() {
    temp_dir root;
    write_sample_build_project(
        root.path() / "alpha", "alpha",
        "#include <iostream>\nint main() { std::cout << "
        "\"alpha-run\\n\"; return 0; }\n"
    );
    write_sample_build_project(
        root.path() / "beta", "beta",
        "#include <iostream>\nint main() { std::cout << "
        "\"beta-run\\n\"; return 0; }\n"
    );

    const cli_result result = run_cli(root.path(), "run debug --project alpha");
    require_true(
        result.exit_code == 0,
        "workspace ecos run must execute the selected project"
    );
    require_contains(
        result.output, "== alpha (alpha) ==",
        "workspace ecos run must identify the selected project"
    );
    require_contains(
        result.output, "alpha-run",
        "workspace ecos run must execute the selected project's facade entry"
    );
    require_not_contains(
        result.output, "beta-run",
        "workspace ecos run must not execute unselected projects"
    );
}

void test_cli_workspace_run_rejects_multiple_artifact_filters() {
    temp_dir root;
    write_sample_dual_run_workspace_project(root.path(), "alpha");

    const cli_result result
        = run_cli(root.path(), "run debug --project alpha app:app tool:cli");
    require_true(
        result.exit_code != 0,
        "workspace ecos run must reject multiple artifact filters"
    );
    require_contains(
        result.output, "run supports at most one workspace artifact filter",
        "workspace ecos run must explain why multiple artifact filters "
        "are invalid"
    );
}

void test_cli_workspace_check_ci_rejects_multiple_artifact_filters() {
    temp_dir root;
    write_sample_dual_run_workspace_project(root.path(), "alpha");

    const cli_result result
        = run_cli(root.path(), "check ci --project alpha app:app tool:cli");
    require_true(
        result.exit_code != 0,
        "workspace ecos check ci must reject multiple artifact filters"
    );
    require_contains(
        result.output,
        "check ci supports at most one workspace artifact filter",
        "workspace ecos check ci must explain that multi-artifact scope is "
        "unsupported"
    );
}

void test_cli_build_release_uses_release_build_type() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path cmake_log = fake_root / "cmake.log";
    fs::create_directories(fake_bin);

    write_fake_configure_and_build_cmake(fake_bin / "cmake");
    write_executable_script(
        fake_bin / "clang++", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_executable_script(
        fake_bin / "clang", "#!/usr/bin/env bash\nexit 0\n"
    );

    scoped_env path_env("PATH", fake_bin.string() + ":" + current_path_env());
    scoped_env cmake_log_env("FAKE_CMAKE_LOG", cmake_log.string());

    const cli_result result = run_cli(root.path(), "build release");
    require_true(
        result.exit_code == 0,
        "ecos build release must succeed for a runnable sample"
    );
    require_contains(
        result.output,
        "built sample:app in " + relative_build_dir(root.path(), "release"),
        "ecos build release must report the release build directory"
    );
    require_contains(
        read_text(cmake_log), "-DCMAKE_BUILD_TYPE=Release",
        "ecos build release must configure CMake in Release mode"
    );
}

void test_cli_check_repo_rejects_legacy_repository_entries() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    write_text(
        root.path() / "scripts/build.sh", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_text(root.path() / "Makefile", "all:\n\t@true\n");
    write_text(root.path() / "cli/run.sh", "#!/usr/bin/env bash\nexit 0\n");

    const cli_result result = run_cli(root.path(), "check repo");
    require_true(
        result.exit_code != 0,
        "ecos check repo must fail on legacy repository scaffolding"
    );
    require_contains(
        result.output, "forbidden repository entries:",
        "ecos check repo must print the repository policy header"
    );
    require_contains(
        result.output, "scripts/: script directories are forbidden",
        "ecos check repo must report committed scripts directories"
    );
    require_contains(
        result.output, "Makefile: Makefiles are forbidden",
        "ecos check repo must report committed Makefiles"
    );
    require_contains(
        result.output, "cli/run.sh: committed shell wrappers are forbidden",
        "ecos check repo must report committed shell wrappers"
    );
}

void test_cli_check_repo_rejects_tracked_surface_drift() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    require_sync_success(
        root.path(),
        "sample project must sync before tracked-surface drift "
        "checks"
    );

    write_text(root.path() / ".github/workflows/tests.yml", "broken\n");

    const cli_result result = run_cli(root.path(), "check repo");
    require_true(
        result.exit_code != 0,
        "ecos check repo must fail on tracked-surface drift"
    );
    require_contains(
        result.output, "tracked surfaces out of sync:",
        "ecos check repo must report tracked-surface drift"
    );
    require_contains(
        result.output,
        ".github/workflows/tests.yml is out of sync with ecosystem "
        "defaults; run `marx sync`",
        "ecos check repo must report generated workflow drift"
    );
}

void test_cli_workspace_check_repo_rejects_gitlink_project_scripts() {
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
        "workspace gitlink repository test must initialize a temporary git repo"
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
        "workspace gitlink repository test must add a synthetic gitlink entry"
    );

    const cli_result result = run_cli(workspace_root, "check repo");
    require_true(
        result.exit_code != 0,
        "workspace ecos check repo must reject gitlink-backed managed "
        "projects with committed scripts"
    );
    require_contains(
        result.output, "scripts/: script directories are forbidden",
        "workspace ecos check repo must surface scripts committed "
        "inside gitlink-backed managed projects"
    );
}

} // namespace ecosystem_test_support
