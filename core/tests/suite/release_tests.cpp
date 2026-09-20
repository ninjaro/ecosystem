#include "test_support.hpp"
#include "test_cases.hpp"

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
            summary.at("stage") == (stage == "success" ? "published" : stage),
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
                    && summary.at("version") == "1.2.3-pre.1",
                "failed attempts must not consume release versions"
            );
            for (const auto& [key, value] : summary.at("artifacts").items()) {
                (void)key;
                require_true(
                    fs::exists(root.path() / value.get<std::string>()),
                    "summary artifacts must locate published outputs"
                );
            }
            require_contains(
                transcript, "data.tar.gz",
                "archive creation commands must be retained"
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
            "late report failure must preserve the successful publication "
            "outcome"
        );
        require_contains(
            result.output, "was published",
            "late reporting failure must explicitly state the real package "
            "outcome"
        );
        require_contains(
            result.output, "initial summary retained",
            "late reporting failure must locate retained evidence"
        );
        require_contains(
            read_text(release / "state.json"), "\"next_prerelease\": 3",
            "late reporting warning must not obscure completed publication"
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

void test_cli_prerelease_builds_shareable_repos_and_auto_increments_version() {
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

    const cli_result first_result
        = run_cli(root.path(), "prerelease --version-base 1.2.3");
    require_true(
        first_result.exit_code == 0,
        "ecos prerelease must succeed for a runnable sample"
    );
    require_contains(
        first_result.output, "version 1.2.3-pre.1",
        "ecos prerelease must report the first prerelease version"
    );
    require_contains(
        read_text(cmake_log), "-DCMAKE_BUILD_TYPE=Release",
        "ecos prerelease must build with the release CMake profile"
    );

    const fs::path prerelease_root = root.path() / ".ecosystem" / "prerelease";
    const fs::path deb_repo = prerelease_root / "deb";
    const fs::path deb_packages_path
        = first_recursive_file_named(deb_repo, "Packages");
    const fs::path deb_packages_gz_path
        = first_recursive_file_named(deb_repo, "Packages.gz");
    const fs::path deb_release_path
        = first_recursive_file_named(deb_repo, "Release");
    require_true(
        !deb_packages_path.empty(),
        "ecos prerelease must write a Debian Packages index"
    );
    require_true(
        !deb_packages_gz_path.empty(),
        "ecos prerelease must write a compressed Debian Packages index"
    );
    require_true(
        !deb_release_path.empty(),
        "ecos prerelease must write Debian Release metadata"
    );
    require_contains(
        read_text(deb_packages_path), "Version: 1.2.3~pre.1",
        "ecos prerelease must publish the Debian prerelease version"
    );
    require_contains(
        read_text(deb_release_path), "Suite: prerelease",
        "ecos prerelease must describe the Debian prerelease suite"
    );
    require_contains(
        read_text(deb_packages_path), "Filename: pool/main/",
        "ecos prerelease must index Debian packages through the pool layout"
    );

    const fs::path deb_package
        = first_recursive_file_with_suffix(deb_repo, ".deb");
    require_true(
        !deb_package.empty(),
        "ecos prerelease must emit a shareable Debian package"
    );
    require_contains(
        deb_package.generic_string(), "/pool/main/",
        "ecos prerelease must place Debian packages inside the pool layout"
    );

    const fs::path pacman_package = first_recursive_file_with_suffix(
        prerelease_root / "pacman", ".pkg.tar.gz"
    );
    require_true(
        !pacman_package.empty(),
        "ecos prerelease must emit a shareable Pacman package"
    );
    const fs::path pacman_db = first_recursive_file_named(
        prerelease_root / "pacman", "sample-prerelease.db"
    );
    require_true(
        !pacman_db.empty(),
        "ecos prerelease must emit a Pacman repository database"
    );
    const fs::path pacman_files = first_recursive_file_named(
        prerelease_root / "pacman", "sample-prerelease.files"
    );
    require_true(
        !pacman_files.empty(),
        "ecos prerelease must emit a Pacman file list database"
    );
    const auto deb_contents = root.path() / "deb-ownership";
    fs::create_directories(deb_contents);
    const auto unpacked = ecosystem::capture_command_result(
        { "ar", "x", deb_package.string() }, deb_contents
    );
    require_true(unpacked.exit_code == 0, "Debian archive must unpack");
    for (const auto& archive :
         { pacman_package, deb_contents / "control.tar.gz",
           deb_contents / "data.tar.gz" }) {
        const auto listing = ecosystem::capture_command_result(
            { "tar", "--numeric-owner", "-tvf", archive.string() }
        );
        require_true(
            listing.exit_code == 0 && !listing.output.empty(),
            "native tar must inspect ownership: " + listing.output
        );
        std::istringstream lines(listing.output);
        for (std::string line; std::getline(lines, line);) {
            std::istringstream entry(line);
            std::string permissions, owner;
            entry >> permissions >> owner;
            require_true(
                owner == "0/0",
                "installed package entries must belong to root, regardless "
                "of the builder's UID/GID: "
                    + archive.string() + ": " + line
            );
        }
    }
    const auto pkginfo = ecosystem::capture_command_result(
        { "tar", "-xOf", pacman_package.string(), ".PKGINFO" }
    );
    require_true(
        pkginfo.exit_code == 0, "Pacman package metadata must extract"
    );
    require_contains(
        pkginfo.output, "pkgver = 1.2.3pre1-1\n",
        "archive version must include the package release"
    );
    require_true(
        pkginfo.output.find("pkgrel =") == std::string::npos,
        "PKGINFO must not use the PKGBUILD-only pkgrel field"
    );
    const auto indexed = ecosystem::capture_command_result(
        { "tar", "-xOf", pacman_db.string(), "sample-1.2.3pre1-1/desc" }
    );
    require_true(
        indexed.exit_code == 0, "Pacman repository metadata must extract"
    );
    require_contains(
        indexed.output, "%VERSION%\n1.2.3pre1-1\n",
        "repository and archive versions must agree"
    );
    if (const auto pacman = ecosystem::find_command_path("pacman");
        !pacman.empty()) {
        const auto queried = ecosystem::capture_command_result(
            { pacman, "-Qp", "--", pacman_package.string() }
        );
        require_true(
            queried.exit_code == 0,
            "native Pacman must accept the archive: " + queried.output
        );
        require_contains(
            queried.output, "sample 1.2.3pre1-1",
            "native Pacman must read the same full version as the repository"
        );
        const auto database = root.path() / "reader-db";
        fs::create_directories(database / "sync");
        fs::create_directories(database / "local");
        fs::copy_file(pacman_db, database / "sync/sample-prerelease.db");
        const auto config = root.path() / "reader.conf";
        write_text(
            config,
            "[options]\nArchitecture = auto\n[sample-prerelease]\nSigLevel = "
            "Never\nServer = file://"
                + pacman_db.parent_path().string() + "\n"
        );
        const auto listed = ecosystem::capture_command_result(
            { pacman, "--config", config.string(), "--dbpath",
              database.string(), "-Sl" }
        );
        require_true(
            listed.exit_code == 0,
            "native Pacman must read the repository: " + listed.output
        );
        require_contains(
            listed.output, "sample-prerelease sample 1.2.3pre1-1",
            "native repository reader must expose the published full version"
        );
    }
    require_contains(
        read_text(prerelease_root / "state.json"), "\"next_prerelease\": 2",
        "ecos prerelease must persist the next prerelease counter "
        "after the first run"
    );

    const cli_result second_result = run_cli(root.path(), "prerelease");
    require_true(
        second_result.exit_code == 0,
        "ecos prerelease must reuse the stored base version on later runs"
    );
    require_contains(
        second_result.output, "version 1.2.3-pre.2",
        "ecos prerelease must auto-increment the prerelease number"
    );
    require_contains(
        read_text(deb_packages_path), "Version: 1.2.3~pre.2",
        "ecos prerelease must refresh the Debian repository index "
        "to the latest prerelease"
    );
    require_contains(
        read_text(prerelease_root / "state.json"), "\"next_prerelease\": 3",
        "ecos prerelease must advance the stored prerelease counter "
        "after the second run"
    );
}

void test_prerelease_preserves_published_state_on_late_failures() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    const auto bin = root.path() / "fake-tools";
    write_fake_configure_and_build_cmake(bin / "cmake");
    write_fake_gpg_tool(bin / "working-gpg");
    const auto gzip = ecosystem::find_command_path("gzip");
    for (const std::string name : { "gzip", "gpg" }) {
        const auto real = std::string(name) == "gzip"
            ? gzip
            : (bin / "working-gpg").string();
        write_executable_script(
            bin / name,
            "#!/bin/sh\n"
            "if [ \"$1\" != '--version' ] && [ "
            "\"$MANIFESTO_FAIL_RELEASE_TOOL\" = '"
                + name + "' ] && { [ '" + name
                + "' != gzip ] || [ \"$1\" = '-n' ]; }; then\n"
                  "  printf 'injected release tool failure\\n' >&2\n  exit "
                  "9\nfi\n"
                  "'"
                + real
                + "' \"$@\" || exit $?\n"
                  "if [ \"$MANIFESTO_FAIL_RELEASE_TOOL\" = cutover ]; then\n"
                  "  for arg do\n"
                  "    case \"$arg\" in *-prerelease.files)\n"
                  "      repo=$(dirname \"$(dirname \"$arg\")\")\n"
                  "      mv \"$repo\" \"$repo-interrupted\" || exit $?;;\n"
                  "    esac\n"
                  "  done\nfi\n"
        );
    }
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    scoped_env cmake_log(
        "FAKE_CMAKE_LOG", (root.path() / "cmake.log").string()
    );
    scoped_env gpg_log("FAKE_GPG_LOG", (root.path() / "gpg.log").string());
    const auto release = ecosystem::local_prerelease_dir(root.path());
    const auto snapshot = [&]() {
        std::map<std::string, std::string> files;
        for (const auto& entry : { "deb", "pacman", "state.json" }) {
            const auto path = release / entry;
            if (!fs::exists(path))
                continue;
            if (fs::is_regular_file(path))
                files.emplace(entry, read_text(path));
            else
                for (const auto& file :
                     fs::recursive_directory_iterator(path)) {
                    if (file.is_regular_file())
                        files.emplace(
                            file.path()
                                .lexically_relative(release)
                                .generic_string(),
                            read_text(file.path())
                        );
                }
        }
        return files;
    };
    {
        scoped_env failure("MANIFESTO_FAIL_RELEASE_TOOL", "gpg");
        const auto result = run_marx_cli(
            root.path(), "prerelease --version-base 1.2.3 --sign"
        );
        require_true(
            result.exit_code == 5,
            "first failed signing must fail the release: " + result.output
        );
        require_true(
            snapshot().empty(),
            "a failed first release must publish no packages, metadata or "
            "counter"
        );
    }
    auto result
        = run_marx_cli(root.path(), "prerelease --version-base 1.2.3 --sign");
    require_true(
        result.exit_code == 0,
        "initial signed release must succeed: " + result.output
    );
    const auto previous = snapshot();
    require_true(
        !previous.empty(), "successful publication must retain its payload"
    );
    const auto compressed
        = first_recursive_file_named(release / "deb", "Packages.gz");
    require_true(
        ecosystem::capture_command_result({ gzip, "-t", compressed.string() })
                .exit_code
            == 0,
        "Debian index must be a valid binary gzip stream"
    );
    require_true(
        ecosystem::capture_command(
            { gzip, "-d", "-c", compressed.string() }
        ) == read_text(first_recursive_file_named(release / "deb", "Packages")),
        "compressed index must preserve all metadata bytes"
    );
    for (const std::string tool : { "gzip", "gpg", "cutover" }) {
        scoped_env failure("MANIFESTO_FAIL_RELEASE_TOOL", tool);
        result = run_marx_cli(root.path(), "prerelease --sign");
        require_true(
            result.exit_code == 5,
            "late tool failure must fail the release: " + result.output
        );
        require_contains(
            result.output,
            tool == "cutover" ? "unable to publish release"
                              : "injected release tool failure",
            "native failure output must survive"
        );
        const std::string summary_prefix = "prerelease summary: ";
        const auto summary_start = result.output.find(summary_prefix);
        require_true(
            summary_start != std::string::npos,
            "late failures must locate their attempt summary"
        );
        const auto path_start = summary_start + summary_prefix.size();
        const auto summary_path = root.path()
            / result.output.substr(
                path_start, result.output.find('\n', path_start) - path_start
            );
        const auto summary = json::parse(read_text(summary_path));
        require_true(
            summary.at("status") == "failed"
                && summary.at("stage") == "package",
            "late failure summary must identify package preparation/publication"
        );
        if (tool != "cutover") {
            const auto transcript
                = read_text(summary_path.parent_path() / "commands.log");
            require_contains(
                transcript, "injected release tool failure",
                "archive/signing failure output must persist"
            );
            require_contains(
                transcript, "\"exit_code\":9",
                "archive/signing native exit status must persist"
            );
        }
        require_true(
            snapshot() == previous,
            "late failures must preserve all published bytes, signatures and "
            "version state"
        );
    }
    result = run_marx_cli(root.path(), "prerelease --sign");
    require_true(result.exit_code == 0, "retry must succeed: " + result.output);
    require_contains(
        result.output, "version 1.2.3-pre.2",
        "failed attempts must not consume versions"
    );
    require_true(
        !fs::exists(release / "work/publication/previous"),
        "successful cutover must retire backup state"
    );
}

void test_prerelease_retains_recovery_state_and_rejects_publication_aliases() {
    temp_dir root;
    auto value = sample_dual_run_manifest();
    const auto binary = root.path() / "app";
    write_text(binary, "payload");
    const auto resolved = ecosystem::resolve_artifact(
        value, ecosystem::artifact_ref { "app", "app" }
    );
    require_true(resolved.has_value(), "primary must resolve");
    ecosystem::prerelease_version version;
    ecosystem::prerelease_artifacts artifacts;
    std::string error;
    const auto release = ecosystem::local_prerelease_dir(root.path());
    const auto recovery = release / "work/publication/previous";
    write_text(recovery / "state.json", "recovery marker");
    auto result = ecosystem::create_prerelease_packages(
        root.path(), value, *resolved, binary, "1.2.3", {}, &version,
        &artifacts, &error
    );
    require_true(
        result == ecosystem::command_error::task_failed,
        "pending recovery must stop a new attempt"
    );
    require_contains(
        error, "awaits recovery",
        "failure must locate recoverable release state"
    );
    require_true(
        read_text(recovery / "state.json") == "recovery marker",
        "retry must not discard backups"
    );
    fs::rename(recovery, root.path() / "saved-recovery");
    const auto outside = root.path() / "external";
    write_text(outside / "Packages", "external marker");
    fs::create_directories(release / "deb");
    fs::create_directory_symlink(outside, release / "deb/alias");
    result = ecosystem::create_prerelease_packages(
        root.path(), value, *resolved, binary, "1.2.3", {}, &version,
        &artifacts, &error
    );
    require_true(
        result == ecosystem::command_error::task_failed,
        "publication aliases must fail before candidate writes"
    );
    require_contains(
        error, "unsupported release publication entry",
        "alias rejection must name its path"
    );
    require_true(
        read_text(outside / "Packages") == "external marker",
        "publication must not follow aliases"
    );
    fs::remove(release / "deb/alias");
    fs::rename(release / "work", root.path() / "saved-work");
    write_text(release / "deb/keep", "published marker");
    fs::create_directory_symlink(release / "deb", release / "work");
    result = ecosystem::create_prerelease_packages(
        root.path(), value, *resolved, binary, "1.2.3", {}, &version,
        &artifacts, &error
    );
    require_true(
        result == ecosystem::command_error::task_failed,
        "internally aliased work storage must not overlap published state"
    );
    require_contains(
        error, "release publication path is a symlink",
        "overlap rejection must identify the storage alias"
    );
    require_true(
        read_text(release / "deb/keep") == "published marker"
            && !fs::exists(release / "deb/publication"),
        "work preflight must leave the published tree untouched"
    );
}

void test_prerelease_rejects_colliding_install_payloads() {
    temp_dir root;
    auto value = sample_dual_run_manifest();
    value.install_artifacts = { "app:app", "tool:cli" };
    value.components.back().artifacts.front().name = "app";
    const fs::path primary = root.path() / "primary/app";
    const fs::path companion
        = ecosystem::local_build_dir(root.path(), "release") / "app";
    write_text(primary, "primary output");
    write_text(companion, "companion output");
    const auto resolved = ecosystem::resolve_artifact(
        value, ecosystem::artifact_ref { "app", "app" }
    );
    require_true(resolved.has_value(), "primary artifact must resolve");
    ecosystem::prerelease_version version;
    ecosystem::prerelease_artifacts packages;
    std::string error;
    const auto status = ecosystem::create_prerelease_packages(
        root.path(), value, *resolved, primary, "1.2.3", {}, &version,
        &packages, &error
    );
    require_true(
        status == ecosystem::command_error::task_failed,
        "duplicate installed filenames must fail packaging"
    );
    require_contains(
        error,
        "package payload collision:", "collision must identify its destination"
    );
    require_contains(
        error, "tool:cli", "collision must identify the companion artifact"
    );
    const fs::path release = ecosystem::local_prerelease_dir(root.path());
    const fs::path staged = first_recursive_file_named(release / "work", "app");
    require_true(
        !staged.empty() && read_text(staged) == "primary output",
        "the first payload must not be overwritten"
    );
    require_true(
        !fs::exists(release / "state.json"),
        "collision must not advance release state"
    );
    require_true(
        !fs::exists(release / "deb") && !fs::exists(release / "pacman"),
        "collision must fail before publishing packages"
    );
}

void test_cli_prerelease_includes_install_companions() {
    temp_dir root;
    write_sample_dual_run_project(root.path());
    auto value = sample_dual_run_manifest();
    value.install_artifacts = { "app:app", "tool:cli" };
    std::string error;
    require_true(
        ecosystem::save_manifest(root.path() / "manifest.json", value, &error),
        error
    );
    const fs::path fake_bin = root.path() / "fake-tools";
    const fs::path log = root.path() / "cmake.log";
    write_fake_configure_and_build_cmake(fake_bin / "cmake");
    scoped_env path_env("PATH", fake_bin.string() + ":" + current_path_env());
    scoped_env log_env("FAKE_CMAKE_LOG", log.string());
    const auto result
        = run_marx_cli(root.path(), "prerelease --version-base 1.2.3");
    require_true(
        result.exit_code == 0,
        "public companion packaging must succeed: " + result.output
    );
    const fs::path work = root.path() / ".ecosystem/prerelease";
    require_true(
        !first_recursive_file_named(work, "app").empty()
            && !first_recursive_file_named(work, "cli").empty(),
        "package payload must retain both independently built install artifacts"
    );
    require_contains(
        read_text(log), "--target tool__cli",
        "companion must be built before packaging"
    );
}

void test_cli_prerelease_signs_repo_metadata_when_requested() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path cmake_log = fake_root / "cmake.log";
    const fs::path gpg_log = fake_root / "gpg.log";
    fs::create_directories(fake_bin);

    write_fake_configure_and_build_cmake(fake_bin / "cmake");
    write_fake_gpg_tool(fake_bin / "gpg");
    write_executable_script(
        fake_bin / "clang++", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_executable_script(
        fake_bin / "clang", "#!/usr/bin/env bash\nexit 0\n"
    );

    scoped_env path_env("PATH", fake_bin.string() + ":" + current_path_env());
    scoped_env cmake_log_env("FAKE_CMAKE_LOG", cmake_log.string());
    scoped_env gpg_log_env("FAKE_GPG_LOG", gpg_log.string());

    const cli_result result = run_cli(
        root.path(),
        "prerelease --version-base 1.2.3 --sign --sign-key prerelease-key"
    );
    require_true(
        result.exit_code == 0,
        "ecos prerelease --sign must succeed with gpg available"
    );
    require_contains(
        result.output, "signed prerelease metadata",
        "ecos prerelease --sign must report that signing ran"
    );

    const fs::path prerelease_root = root.path() / ".ecosystem" / "prerelease";
    const fs::path deb_repo = prerelease_root / "deb";
    const fs::path inrelease_path
        = first_recursive_file_named(deb_repo, "InRelease");
    const fs::path release_gpg_path
        = first_recursive_file_named(deb_repo, "Release.gpg");
    require_true(
        !inrelease_path.empty(),
        "ecos prerelease --sign must write a clearsigned Debian InRelease file"
    );
    require_true(
        !release_gpg_path.empty(),
        "ecos prerelease --sign must write a detached Debian Release signature"
    );

    const fs::path pacman_db = first_recursive_file_named(
        prerelease_root / "pacman", "sample-prerelease.db"
    );
    require_true(
        !pacman_db.empty(),
        "ecos prerelease --sign must still emit the Pacman repo database"
    );
    require_true(
        fs::exists(fs::path(pacman_db.string() + ".sig")),
        "ecos prerelease --sign must sign the Pacman repo database"
    );
    const fs::path pacman_files = first_recursive_file_named(
        prerelease_root / "pacman", "sample-prerelease.files"
    );
    require_true(
        !pacman_files.empty(),
        "ecos prerelease --sign must emit the Pacman file list database"
    );
    require_true(
        fs::exists(fs::path(pacman_files.string() + ".sig")),
        "ecos prerelease --sign must sign the Pacman file list database"
    );

    const fs::path pacman_package = first_recursive_file_with_suffix(
        prerelease_root / "pacman", ".pkg.tar.gz"
    );
    require_true(
        !pacman_package.empty(),
        "ecos prerelease --sign must still emit the Pacman package"
    );
    require_true(
        fs::exists(fs::path(pacman_package.string() + ".sig")),
        "ecos prerelease --sign must sign the Pacman package"
    );

    require_contains(
        read_text(gpg_log), "--clearsign",
        "ecos prerelease --sign must clear-sign Debian Release metadata"
    );
    require_contains(
        read_text(gpg_log), "--detach-sign",
        "ecos prerelease --sign must use detached signatures for repo artifacts"
    );
    require_contains(
        read_text(gpg_log), "key=prerelease-key",
        "ecos prerelease --sign-key must forward the selected signing key"
    );
}

} // namespace ecosystem_test_support
