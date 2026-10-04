#include "../../suite/test_support.hpp"
#include "../release.hpp"

namespace ecosystem_test_support {

void test_release_dependency_queries() {
    temp_dir root;
    const auto binary = root.path() / "app";
    write_text(
        root.path() / "main.cpp",
        "#include <iostream>\nint main() { std::cout << 42; }\n"
    );
    const auto compiled = ecosystem::capture_command_result(
        { "c++", (root.path() / "main.cpp").string(), "-o", binary.string() }
    );
    require_true(
        compiled.exit_code == 0,
        "native dependency fixture must build: " + compiled.output
    );
    for (const bool debian : { false, true }) {
        const auto project = root.path() / (debian ? "debian" : "pacman");
        const auto bin = project / "tools";
        fs::create_directories(bin);
        for (const std::string tool : { "cmake", "objdump", "tar", "ar", "gzip",
                                        "sha256sum", "uname" }) {
            const auto path = ecosystem::find_command_path(tool);
            require_true(!path.empty(), "runtime fixture requires " + tool);
            fs::create_symlink(path, bin / tool);
        }
        write_executable_script(
            bin / (debian ? "dpkg-query" : "pacman"), R"(#!/bin/sh
case "$1" in
  -Qqo|-S)
    case "$QUERY_FAILURE" in
      owner_failure) printf 'injected owner failure\n' >&2; exit 17 ;;
      owner_ambiguous) printf 'fixture-runtime, other-package: %s\n' "$2"; exit 0 ;;
    esac
    if [ "$1" = -S ]; then
      case "$2" in /usr/lib/*|/usr/lib64/*) printf 'no pre-merge entry\n' >&2; exit 1 ;; esac
      printf '%s:amd64: %s\n' "${QUERY_OWNER:-fixture-runtime}" "$2"
    else
      printf '%s\n' "${QUERY_OWNER:-fixture-runtime}"
    fi ;;
  -Q|-W)
    case "$QUERY_FAILURE" in
      version_failure) printf 'injected version failure\n' >&2; exit 19 ;;
      version_invalid) printf 'fixture-runtime invalid=version installed\n'; exit 0 ;;
    esac
    if [ "$1" = -W ]; then
      printf '%s 2:3.4.5-6 %s\n' "${4%%:*}" "${QUERY_STATUS:-installed}"
    else
      printf '%s 2:3.4.5-6\n' "$3"
    fi ;;
  *) exit 99 ;;
esac
)"
        );
        scoped_env path_env("PATH", bin.string());
        auto value = sample_dual_run_manifest();
        const auto primary = ecosystem::resolve_artifact(
            value, ecosystem::artifact_ref { "app", "app" }
        );
        const auto companion = ecosystem::resolve_artifact(
            value, ecosystem::artifact_ref { "tool", "cli" }
        );
        require_true(
            primary.has_value() && companion.has_value(),
            "fixture artifacts must resolve"
        );
        ecosystem::prerelease_version version;
        ecosystem::prerelease_artifacts artifacts;
        std::string error;
        const auto prepare = [&](const ecosystem::resolved_artifact& selected,
                                 const fs::path& output) {
            return ecosystem::create_prerelease_packages(
                project, value, selected, output, "1.0.0", {}, &version,
                &artifacts, &error
            );
        };
        auto status = prepare(*primary, binary);
        require_true(
            status == ecosystem::command_error::ok,
            "native owner mapping must succeed: " + error
        );
        const std::string dependency = debian
            ? "fixture-runtime:amd64 (>= 2:3.4.5-6)"
            : "fixture-runtime>=2:3.4.5-6";
        const auto& declared = debian ? artifacts.debian_dependencies
                                      : artifacts.pacman_dependencies;
        require_true(
            declared == std::vector<std::string> { dependency },
            "runtime owners must be deduplicated and retain epochs/version "
            "releases"
        );
        require_true(
            artifacts.warnings.size() == 1,
            "unmapped cross-format metadata must be reported"
        );
        const auto release = ecosystem::local_prerelease_dir(project);
        const auto metadata = json::parse(read_text(first_recursive_file_named(
            release / "work", "runtime-packages.json"
        )));
        require_true(
            metadata.at("libraries").size() > 1,
            "fixture must exercise several libraries sharing one owner"
        );
        if (debian) {
            const auto unpacked = project / "unpacked";
            fs::create_directories(unpacked);
            require_true(
                ecosystem::capture_command_result(
                    { "ar", "x", artifacts.deb_package_path.string() }, unpacked
                )
                        .exit_code
                    == 0,
                "Debian package must extract"
            );
            const auto control = ecosystem::capture_command_result(
                { "tar", "-xOf", (unpacked / "control.tar.gz").string(),
                  "./control" }
            );
            require_true(control.exit_code == 0, "Debian control must extract");
            require_contains(
                control.output, "Depends: " + dependency,
                "Debian control must declare the native runtime dependency"
            );
            require_contains(
                read_text(artifacts.deb_packages_path),
                "Depends: " + dependency,
                "Debian index must retain the same dependency"
            );
        } else {
            const auto info = ecosystem::capture_command_result(
                { "tar", "-xOf", artifacts.pacman_package_path.string(),
                  ".PKGINFO" }
            );
            require_true(info.exit_code == 0, "Pacman metadata must extract");
            require_contains(
                info.output, "depend = " + dependency,
                "Pacman package must declare the native runtime dependency"
            );
            const auto desc = ecosystem::capture_command_result(
                { "tar", "-xOf", artifacts.pacman_db_path.string(),
                  "dual-run-sample-1.0.0pre1-1/desc" }
            );
            require_true(
                desc.exit_code == 0, "Pacman repository metadata must extract"
            );
            require_contains(
                desc.output, "%DEPENDS%\n" + dependency,
                "Pacman index must retain the same dependency"
            );
        }
        {
            scoped_env different_owner("QUERY_OWNER", "fixture-tool");
            require_true(
                prepare(*companion, binary) == ecosystem::command_error::ok,
                "second package must publish: " + error
            );
        }
        const auto state_path = release / "state.json";
        const std::string key = debian ? "latest_debian_dependencies"
                                       : "latest_pacman_dependencies";
        const auto state = json::parse(read_text(state_path));
        require_true(
            state.at("packages").at(0).at(key)
                == std::vector<std::string> { dependency },
            "rewriting another package must retain previous dependency metadata"
        );
        const auto snapshot = [&]() {
            std::map<fs::path, std::string> files;
            for (const auto& entry : { "deb", "pacman" })
                for (const auto& file :
                     fs::recursive_directory_iterator(release / entry))
                    if (file.is_regular_file())
                        files.emplace(file.path(), read_text(file.path()));
            files.emplace(state_path, read_text(state_path));
            return files;
        };
        const auto published = snapshot();
        for (const std::string failure :
             { "owner_failure", "owner_ambiguous", "version_failure",
               "version_invalid" }) {
            scoped_env injection("QUERY_FAILURE", failure);
            require_true(
                prepare(*primary, binary)
                    == ecosystem::command_error::task_failed,
                "invalid native package ownership/version must fail"
            );
            require_contains(
                error, "installed",
                "mapping error must identify the failed native query"
            );
            require_true(
                snapshot() == published,
                "query failure must preserve published packages, indexes and "
                "counters"
            );
        }
        if (debian) {
            scoped_env not_installed("QUERY_STATUS", "config-files");
            require_true(
                prepare(*primary, binary)
                    == ecosystem::command_error::task_failed,
                "uninstalled dpkg records must not satisfy runtime mapping"
            );
            require_true(
                snapshot() == published,
                "uninstalled dependency must preserve publication state"
            );
        }
        auto invalid_state = state;
        invalid_state["packages"][0][key]
            = json::array({ "invalid\nmetadata" });
        write_text(state_path, invalid_state.dump(2));
        const auto invalid_snapshot = snapshot();
        require_true(
            prepare(*primary, binary) == ecosystem::command_error::task_failed,
            "malformed stored dependencies must be rejected"
        );
        require_contains(
            error, "invalid persisted",
            "stored metadata rejection must name the invalid dependency field"
        );
        require_true(
            snapshot() == invalid_snapshot,
            "invalid stored metadata must not be overwritten"
        );
        write_text(state_path, published.at(state_path));
        const auto plain = project / "plain-output";
        write_text(plain, "plain payload");
        require_true(
            prepare(*primary, plain) == ecosystem::command_error::ok,
            "a new payload without ELF dependencies must publish: " + error
        );
        const auto updated = json::parse(read_text(state_path));
        require_true(
            updated.at("packages").at(0).at(key).empty(),
            "new payload must clear dependencies from its previous version"
        );
        require_true(
            !updated.at("packages").at(1).at(key).empty(),
            "clearing one package must preserve another package's dependencies"
        );
    }
}

// Opt-in integration acceptance: uses native cached distribution packages and
// user namespaces. It is intentionally outside the ordinary portable suite.

} // namespace ecosystem_test_support
