#include "test_cases.hpp"
#include "test_support.hpp"

namespace ecosystem_test_support {

void test_prerelease_command_log_preserves_output_and_fails_closed() {
    temp_dir root;
    const auto log_path = root.path() / "commands.log";
    {
        ecosystem::scoped_command_log log(log_path);
        const auto captured = ecosystem::capture_command_result(
            { "/bin/sh", "-c",
              "printf 'a\\000b'; printf 'stderr-marker' >&2; exit 17" },
            root.path(),
            { { "PRIVATE_TEST_VALUE", "do-not-record-this-value" } }
        );
        require_true(
            captured.exit_code == 17, "logging must preserve native exit status"
        );
        require_true(
            captured.output == std::string("a\0bstderr-marker", 16),
            "capture must retain every output byte"
        );
        require_true(
            ecosystem::run_command(
                { "/bin/sh", "-c", "printf 'live-marker'; exit 23" },
                root.path()
            ) == 23,
            "streamed commands must retain their native exit status"
        );
        const auto signaled = ecosystem::capture_command_result(
            { "/bin/sh", "-c", "kill -TERM $$" }
        );
        require_true(
            signaled.exit_code == 143,
            "signal termination must have shell-compatible status"
        );
        require_true(
            log.error().empty(), "ordinary command logging must succeed"
        );
    }
    const auto transcript = read_text(log_path);
    require_contains(
        transcript, std::string("a\0bstderr-marker", 16),
        "log must retain binary bytes and stderr"
    );
    require_contains(
        transcript, "live-marker", "streamed stdout must be retained"
    );
    require_contains(
        transcript, "\"exit_code\":17",
        "log must identify native failure status"
    );
    require_contains(
        transcript, "\"cwd\":", "log must identify working directories"
    );
    require_true(
        transcript.find("do-not-record-this-value") == std::string::npos,
        "command headers must not dump environment values"
    );
    const auto after_scope = ecosystem::capture_command_result(
        { "/bin/sh", "-c", "printf 'after-scope'" }
    );
    require_true(
        after_scope.output == "after-scope"
            && read_text(log_path) == transcript,
        "logging must end with its scope"
    );

    const pid_t child = ::fork();
    require_true(child >= 0, "must fork the log write failure fixture");
    if (child == 0) {
        std::signal(SIGXFSZ, SIG_IGN);
        const rlimit limit { 512, 512 };
        if (::setrlimit(RLIMIT_FSIZE, &limit) != 0)
            ::_exit(2);
        ecosystem::scoped_command_log log(root.path() / "limited.log");
        const auto first = ecosystem::capture_command_result(
            { "/bin/sh", "-c", "printf '%4096s' x" }, root.path()
        );
        const auto next = ecosystem::capture_command_result(
            { "/bin/sh", "-c", "touch should-not-run" }, root.path()
        );
        ::_exit(
            first.exit_code == -1 && next.exit_code == -1
                    && !log.error().empty()
                    && !fs::exists(root.path() / "should-not-run")
                ? 0
                : 1
        );
    }
    int child_status = 0;
    require_true(
        ::waitpid(child, &child_status, 0) == child,
        "must wait for log failure fixture"
    );
    require_true(
        WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0,
        "partial log writes must stop subsequent native commands"
    );
    require_true(
        fs::file_size(root.path() / "limited.log") == 512,
        "fixture must exercise a late transcript write failure"
    );
}

void test_cli_prerelease_retains_attempt_logs_and_summaries() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    const auto bin = root.path() / "fake-tools";
    write_fake_configure_and_build_cmake(bin / "working-cmake");
    write_executable_script(bin / "cmake", R"(#!/bin/sh
case "$1" in
  --version) exec "$(dirname "$0")/working-cmake" "$@" ;;
  --build) stage=build ;;
  *) stage=configure ;;
esac
printf '%s stdout-marker\n' "$stage"
printf '%s stderr-marker\n' "$stage" >&2
if [ "$FAIL_ATTEMPT_STAGE" = "$stage" ]; then exit 37; fi
if [ "$BLOCK_ATTEMPT_SUMMARY" = yes ]; then
  for attempt in .ecosystem/prerelease/attempts/*; do mkdir -p "$attempt/summary.tmp"; done
fi
exec "$(dirname "$0")/working-cmake" "$@"
)");
    scoped_env path_env("PATH", bin.string() + ":" + current_path_env());
    scoped_env cmake_log(
        "FAKE_CMAKE_LOG", (root.path() / "cmake.log").string()
    );
    const auto release = ecosystem::local_prerelease_dir(root.path());
    std::map<fs::path, std::string> retained;
    for (const std::string stage : { "configure", "build", "success" }) {
        scoped_env failure("FAIL_ATTEMPT_STAGE", stage);
        const auto result
            = run_marx_cli(root.path(), "prerelease --version-base 1.2.3");
        require_true(
            result.exit_code == (stage == "success" ? 0 : 5),
            "attempt must report its actual outcome: " + result.output
        );
        fs::path latest;
        for (const auto& entry : fs::directory_iterator(release / "attempts")) {
            const auto path = entry.path() / "summary.json";
            if (retained.find(path) == retained.end()) {
                require_true(
                    latest.empty(),
                    "each invocation must add exactly one attempt"
                );
                latest = path;
            }
        }
        require_true(
            !latest.empty(), "each invocation must retain its summary"
        );
        for (const auto& [path, contents] : retained)
            require_true(
                read_text(path) == contents,
                "retry must retain previous attempt evidence"
            );
        const auto summary = json::parse(read_text(latest));
        const auto log = latest.parent_path() / "commands.log";
        const auto transcript = read_text(log);
        require_contains(
            result.output,
            latest.lexically_relative(root.path()).generic_string(),
            "CLI must locate the attempt summary"
        );
        require_contains(
            result.output, log.lexically_relative(root.path()).generic_string(),
            "CLI must locate the command transcript"
        );
        require_true(
            summary.at("exit_code") == result.exit_code,
            "summary must match CLI status"
        );
        require_true(
            summary.at("stage") == (stage == "success" ? "prepared" : stage),
            "summary must identify the failed or completed stage"
        );
        require_contains(
            transcript, "configure stdout-marker",
            "configure stdout must survive"
        );
        require_contains(
            transcript, "configure stderr-marker",
            "configure stderr must survive"
        );
        if (stage != "configure")
            require_contains(
                transcript, "build stdout-marker", "build output must survive"
            );
        if (stage == "success") {
            require_true(
                summary.at("status") == "succeeded"
                    && summary.contains("release_input"),
                "successful build must retain the full prepared input"
            );
            require_true(
                fs::is_regular_file(
                    root.path() / summary.at("release_input").get<std::string>()
                ),
                "summary must locate the prepared product"
            );
            require_true(
                transcript.find("data.tar.gz") == std::string::npos,
                "Marx must not invoke native archive creation"
            );
        } else {
            require_true(
                summary.at("status") == "failed"
                    && !summary.at("diagnostics").get<std::string>().empty(),
                "failed summaries must retain diagnostics"
            );
            require_contains(
                transcript, "\"exit_code\":37",
                "native failure code must survive CLI status mapping"
            );
            require_true(
                !fs::exists(release / "state.json"),
                "build failures must not publish state"
            );
        }
        retained.emplace(latest, read_text(latest));
        retained.emplace(log, transcript);
    }
    {
        scoped_env block("BLOCK_ATTEMPT_SUMMARY", "yes");
        const auto result = run_marx_cli(root.path(), "prerelease");
        require_true(
            result.exit_code == 0,
            "late report failure must preserve the successful preparation "
            "outcome"
        );
        require_contains(
            result.output, "was prepared",
            "late reporting failure must explicitly state the real preparation "
            "outcome"
        );
        require_contains(
            result.output, "initial summary retained",
            "late reporting failure must locate retained evidence"
        );
        require_true(
            !fs::exists(release / "state.json") && !fs::exists(release / "deb")
                && !fs::exists(release / "pacman"),
            "preparation must not create published state"
        );
    }
}

void test_cli_prerelease_rejects_unusable_attempt_storage() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    const auto release = ecosystem::local_prerelease_dir(root.path());
    write_text(release / "attempts", "keep-existing-file");
    auto result = run_marx_cli(root.path(), "prerelease --version-base 1.2.3");
    require_true(result.exit_code == 5, "blocked attempt storage must fail");
    require_true(
        read_text(release / "attempts") == "keep-existing-file",
        "attempt preflight must preserve existing entries"
    );
    require_true(
        !fs::exists(ecosystem::local_build_dir(root.path(), "release")),
        "attempt storage must be checked before configure/build"
    );
    fs::rename(release / "attempts", root.path() / "saved-attempts");
    fs::create_directories(release / "deb");
    fs::create_directory_symlink(release / "deb", release / "attempts");
    result = run_marx_cli(root.path(), "prerelease --version-base 1.2.3");
    require_true(
        result.exit_code == 5, "internally aliased attempt storage must fail"
    );
    require_contains(
        result.output, "attempt path is a symlink",
        "rejection must identify the storage alias"
    );
    require_true(
        fs::is_empty(release / "deb"),
        "attempt storage must not overlap published packages"
    );
}

void test_cli_prerelease_prepares_without_publication() {
    temp_dir root;
    write_sample_dual_run_project(root.path());
    const auto tools = root.path() / "tools";
    write_fake_configure_and_build_cmake(tools / "cmake");
    scoped_env path("PATH", tools.string() + ":" + current_path_env());
    scoped_env log("FAKE_CMAKE_LOG", (root.path() / "cmake.log").string());
    const auto published = ecosystem::local_prerelease_dir(root.path());
    write_text(
        published / "state.json",
        "{\"packages\":[{\"package_name\":\"dual-run-sample\",\"base_version\":"
        "\"1.2.3\",\"next_prerelease\":7}]}\n"
    );
    write_text(published / "deb/keep", "published bytes");
    write_text(published / "pacman/keep.sig", "published signature");
    const auto before = read_text(published / "state.json");
    for (const auto& command :
         { "prerelease", "prerelease --prepare-only", "prerelease tool:cli" }) {
        const auto result = run_marx_cli(root.path(), command);
        require_true(result.exit_code == 0, result.output);
        require_contains(
            result.output, "prepared release input:", "every route prepares"
        );
        require_true(
            read_text(published / "state.json") == before
                && read_text(published / "deb/keep") == "published bytes"
                && read_text(published / "pacman/keep.sig")
                    == "published signature",
            "no route can change repositories, signatures or counters"
        );
    }
    for (const auto& flags :
         { "--sign", "--sign-key unavailable", "--prepare-only --sign" }) {
        const auto result
            = run_marx_cli(root.path(), std::string("prerelease ") + flags);
        require_true(result.exit_code == 2, "obsolete signing flags must fail");
        require_contains(
            result.output, "ecosystem-release package",
            "diagnose migrated ownership"
        );
        require_true(
            read_text(published / "state.json") == before,
            "flags cannot advance counters"
        );
    }
    const auto calls = read_text(root.path() / "cmake.log");
    require_contains(
        calls, "CMAKE_BUILD_TYPE=Release",
        "ordinary prerelease still builds Release"
    );
    require_contains(
        calls, "--target tool__cli", "complete companions are built"
    );
}

} // namespace ecosystem_test_support
