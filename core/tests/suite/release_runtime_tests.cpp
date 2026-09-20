#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

void test_prerelease_packages_linked_runtime_closure() {
    temp_dir root;
    const auto project = root.path() / "project with spaces";
    write_runtime_package_project(project);
    const auto result
        = run_marx_cli(project, "prerelease --version-base 1.2.3");
    require_true(
        result.exit_code == 0,
        "native runtime packaging must succeed:\n" + result.output
    );
    const auto release = ecosystem::local_prerelease_dir(project);
    const auto metadata = json::parse(read_text(
        first_recursive_file_named(release / "work", "runtime-packages.json")
    ));
    const auto dependencies
        = metadata.at("dependencies").get<std::vector<std::string>>();
    require_true(
        !dependencies.empty(),
        "native linked payload must declare system package dependencies"
    );
    require_contains(
        result.output, "runtime dependency metadata is incomplete",
        "cross-distribution metadata limitation must be visible"
    );
    const auto attempt = json::parse(read_text(
        first_recursive_file_named(release / "attempts", "summary.json")
    ));
    const bool native_pacman = metadata.at("backend") == "Pacman";
    require_true(
        attempt.at(
            native_pacman ? "pacman_dependencies" : "debian_dependencies"
        ) == dependencies,
        "attempt summary must retain the native dependency declarations"
    );
    if (native_pacman) {
        const auto package = first_recursive_file_with_suffix(
            release / "pacman", ".pkg.tar.gz"
        );
        const auto info = ecosystem::capture_command_result(
            { "pacman", "-Qip", "--", package.string() }, root.path(),
            { { "LC_ALL", "C" } }
        );
        require_true(
            info.exit_code == 0,
            "native Pacman must accept the ELF archive: " + info.output
        );
        for (const auto& dependency : dependencies)
            require_contains(
                info.output, dependency,
                "native package reader must see every dependency"
            );
        std::vector<std::string> check { "pacman", "-T" };
        check.insert(check.end(), dependencies.begin(), dependencies.end());
        const auto installed = ecosystem::capture_command_result(check);
        require_true(
            installed.exit_code == 0,
            "the build host must satisfy the declared version floors: "
                + installed.output
        );
        const auto empty_db = root.path() / "empty-db";
        fs::create_directories(empty_db / "local");
        check.insert(
            check.begin() + 1,
            { "--config", "/dev/null", "--dbpath", empty_db.string() }
        );
        const auto missing = ecosystem::capture_command_result(check);
        require_true(
            missing.exit_code != 0,
            "an empty package database must not satisfy declared runtime "
            "dependencies"
        );
        for (const auto& dependency : dependencies)
            require_contains(
                missing.output, dependency,
                "native dependency checking must identify each missing "
                "requirement"
            );
    }
    const auto inventory = json::parse(
        read_text(first_recursive_file_named(release / "work", "runtime.json"))
    );
    require_true(
        inventory.at("bundled").size() == 3 && !inventory.at("system").empty(),
        "runtime inventory must distinguish three linked project libraries "
        "from system requirements"
    );
    const auto source_binary
        = ecosystem::local_build_dir(project, "release") / "runtime_app";
    const auto original = ecosystem::capture_command_result(
        { "readelf", "-d", source_binary.string() }
    );
    require_true(original.exit_code == 0, "source ELF inspection must succeed");
    require_contains(
        original.output, project.string(),
        "package relocation must not alter the original build binary"
    );

    const auto pacman = root.path() / "pacman-prefix";
    const auto debian = root.path() / "debian-prefix";
    fs::create_directories(pacman);
    fs::create_directories(debian);
    auto command = [&](const std::vector<std::string>& args,
                       const fs::path& cwd) {
        const auto execution = ecosystem::capture_command_result(args, cwd);
        require_true(
            execution.exit_code == 0,
            "native package extraction must succeed:\n" + execution.output
        );
    };
    command(
        { "tar", "-xzf",
          first_recursive_file_with_suffix(release / "pacman", ".pkg.tar.gz")
              .string(),
          "-C", pacman.string() },
        root.path()
    );
    command(
        { "ar", "x",
          first_recursive_file_with_suffix(release / "deb", ".deb").string() },
        debian
    );
    command(
        { "tar", "-xzf", (debian / "data.tar.gz").string(), "-C",
          debian.string() },
        root.path()
    );
    fs::rename(project, root.path() / "unavailable-project");
    for (const auto& prefix : { pacman, debian }) {
        for (const auto& library :
             { "libleaf.so", "libmiddle.so", "libextra.so" })
            require_true(
                fs::is_regular_file(prefix / "usr/lib" / library),
                "archive must contain linked runtime libraries"
            );
        require_true(
            !fs::exists(prefix / "usr/lib/libbridge.a")
                && !fs::exists(prefix / "usr/lib/libspare.so"),
            "runtime packaging must exclude intermediate archives and "
            "unrelated libraries"
        );
        for (const auto& executable : { "runtime_app", "runtime_tool" }) {
            const auto binary = prefix / "usr/bin" / executable;
            const auto dynamic = ecosystem::capture_command_result(
                { "readelf", "-d", binary.string() }
            );
            require_true(
                dynamic.exit_code == 0, "packaged ELF inspection must succeed"
            );
            require_not_contains(
                dynamic.output, "RPATH", "package must not retain build RPATH"
            );
            require_not_contains(
                dynamic.output, "RUNPATH",
                "package must not retain build RUNPATH"
            );
            const auto run = ecosystem::capture_command_result(
                { binary.string() }, root.path(),
                { { "LD_LIBRARY_PATH", (prefix / "usr/lib").string() },
                  { "LD_PRELOAD", "" } }
            );
            require_true(
                run.exit_code == 0,
                "extracted application must run using only its installed "
                "project libraries:\n"
                    + run.output
            );
        }
    }
}

void test_prerelease_maps_native_dependencies_and_preserves_state_on_query_failures() {
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
void test_prerelease_installs_in_an_isolated_pacman_root(
    const fs::path& cache
) {
    temp_dir root;
    const auto pacman = ecosystem::find_command_path("pacman");
    const auto bwrap = ecosystem::find_command_path("bwrap");
    require_true(
        !pacman.empty() && !bwrap.empty(),
        "isolated install requires Pacman and bubblewrap"
    );
    require_true(
        fs::is_directory(cache),
        "Pacman package cache is missing: " + cache.string()
    );

    const auto project = root.path() / "project";
    write_runtime_package_project(project);
    const auto built = run_marx_cli(project, "prerelease --version-base 1.0.0");
    require_true(
        built.exit_code == 0, "install fixture must package: " + built.output
    );
    const auto release = ecosystem::local_prerelease_dir(project);
    const auto app_package
        = first_recursive_file_with_suffix(release / "pacman", ".pkg.tar.gz");
    const auto runtime = json::parse(read_text(
        first_recursive_file_named(release / "work", "runtime-packages.json")
    ));
    std::vector<std::string> required
        = runtime.at("dependencies").get<std::vector<std::string>>();

    const auto tooling_source = root.path() / "tooling-source";
    const auto tooling_build
        = ecosystem::local_build_dir(tooling_source, "release");
    fs::create_directories(tooling_build);
    fs::copy_file(marx_binary_path(), tooling_build / "marx");
    fs::copy_file(engels_binary_path(), tooling_build / "engels");
    fs::copy_file(
        test_cli_build_dir() / "CMakeCache.txt",
        tooling_build / "CMakeCache.txt"
    );
    fs::copy(
        fs::path(ECOS_TEST_SOURCE_DIR) / "templates",
        tooling_source / "templates", fs::copy_options::recursive
    );
    const auto surface = tooling_source / "templates/cmake/surface_prefix.tpl";
    write_text(
        surface, "# isolated package template marker\n" + read_text(surface)
    );
    const auto manifest = ecosystem::load_manifest(
        fs::path(ECOS_TEST_SOURCE_DIR) / "manifest.json"
    );
    require_true(manifest.value.has_value(), "tooling manifest must load");
    const auto selected
        = ecosystem::resolve_artifact(*manifest.value, std::nullopt);
    require_true(selected.has_value(), "tooling artifact must resolve");
    ecosystem::prerelease_version version;
    ecosystem::prerelease_artifacts artifacts;
    std::string error;
    require_true(
        ecosystem::create_prerelease_packages(
            tooling_source, *manifest.value, *selected, tooling_build / "marx",
            "1.0.0", {}, &version, &artifacts, &error
        ) == ecosystem::command_error::ok,
        "tooling install fixture must package: " + error
    );
    required.insert(
        required.end(), artifacts.pacman_dependencies.begin(),
        artifacts.pacman_dependencies.end()
    );

    // Resolve the installed dependency closure to exact cached archives. Native
    // Pacman validates all relations again during the actual transaction.
    std::set<std::string> visited;
    std::vector<std::string> packages;
    for (std::size_t index = 0; index < required.size(); ++index) {
        const auto requested
            = required[index].substr(0, required[index].find_first_of("<>= "));
        if (visited.contains(requested))
            continue;
        const auto installed = ecosystem::capture_command_result(
            { pacman, "-Q", "--", requested }
        );
        require_true(
            installed.exit_code == 0,
            "cannot resolve installed prerequisite " + requested + ": "
                + installed.output
        );
        std::istringstream record(installed.output);
        std::string name, installed_version, extra;
        record >> name >> installed_version;
        require_true(
            !name.empty() && !installed_version.empty() && !(record >> extra),
            "expected one installed provider for " + requested + ": "
                + installed.output
        );
        if (!visited.insert(name).second)
            continue;
        visited.insert(requested);
        fs::path archive;
        std::string inspected;
        for (const auto& entry : fs::directory_iterator(cache)) {
            if (!entry.is_regular_file())
                continue;
            const auto filename = entry.path().filename().string();
            if (!filename.starts_with(name + "-") || filename.ends_with(".sig")
                || filename.find(".pkg.tar") == std::string::npos)
                continue;
            const auto info = ecosystem::capture_command_result(
                { pacman, "-Qp", "--", entry.path().string() }
            );
            inspected += entry.path().filename().string() + " (exit "
                + std::to_string(info.exit_code) + "): " + info.output;
            if (info.exit_code == 0 && info.output == installed.output) {
                archive = entry.path();
                break;
            }
        }
        require_true(
            !archive.empty(),
            "cache lacks the installed prerequisite " + installed.output
                + "retain its distribution archive and rerun the isolated "
                  "install check\nInspected archives:\n"
                + inspected
        );
        packages.push_back(archive.string());
        const auto info = ecosystem::capture_command_result(
            { "tar", "-xOf", archive.string(), ".PKGINFO" }
        );
        require_true(
            info.exit_code == 0,
            "cached package metadata must extract: " + info.output
        );
        std::istringstream lines(info.output);
        for (std::string line; std::getline(lines, line);)
            if (line.starts_with("depend = "))
                required.push_back(line.substr(9));
    }

    const auto prefix = root.path() / "installed-root";
    const auto database = prefix / "var/lib/pacman";
    const auto config = root.path() / "pacman.conf";
    fs::create_directories(database / "local");
    fs::create_directories(prefix / "cache");
    fs::create_directories(prefix / "hooks");
    fs::create_directories(prefix / "project");
    write_text(
        config,
        "[options]\nArchitecture = auto\nSigLevel = Never\nLocalFileSigLevel = "
        "Never\n"
    );
    const auto install = [&](const std::vector<std::string>& inputs) {
        std::vector<std::string> command { bwrap,
                                           "--unshare-user",
                                           "--uid",
                                           "0",
                                           "--gid",
                                           "0",
                                           "--unshare-pid",
                                           "--unshare-net",
                                           "--die-with-parent",
                                           "--ro-bind",
                                           "/",
                                           "/",
                                           "--bind",
                                           prefix.string(),
                                           prefix.string(),
                                           "--proc",
                                           "/proc",
                                           "--dev",
                                           "/dev",
                                           "--",
                                           pacman,
                                           "--root",
                                           prefix.string(),
                                           "--dbpath",
                                           database.string(),
                                           "--config",
                                           config.string(),
                                           "--cachedir",
                                           (prefix / "cache").string(),
                                           "--logfile",
                                           (prefix / "pacman.log").string(),
                                           "--hookdir",
                                           (prefix / "hooks").string(),
                                           "--noconfirm",
                                           "--noscriptlet",
                                           "-U" };
        command.insert(command.end(), inputs.begin(), inputs.end());
        return ecosystem::capture_command_result(command, root.path());
    };
    const auto rejected = install({ app_package.string() });
    require_true(
        rejected.exit_code != 0,
        "empty root must reject the application without its native dependencies"
    );
    require_contains(
        rejected.output, "could not satisfy dependencies",
        "native installation must fail for missing dependencies"
    );
    require_true(
        !fs::exists(prefix / "usr/bin/runtime_app"),
        "failed dependency transaction must install no app payload"
    );
    packages.push_back(app_package.string());
    packages.push_back(artifacts.pacman_package_path.string());
    const auto installed = install(packages);
    require_true(
        installed.exit_code == 0,
        "native installation into an empty root must succeed with cached "
        "prerequisites: "
            + installed.output
    );
    require_true(
        fs::exists(prefix / "usr/bin/marx")
            && fs::exists(prefix / "usr/bin/engels"),
        "native installation must include both actors"
    );
    const auto registered = ecosystem::capture_command_result(
        { pacman, "--config", config.string(), "--root", prefix.string(),
          "--dbpath", database.string(), "-Q", "runtime-sample", "manifesto" }
    );
    require_true(
        registered.exit_code == 0,
        "isolated Pacman database must register both packages: "
            + registered.output
    );

    fs::rename(project, root.path() / "unavailable-project");
    fs::rename(tooling_source, root.path() / "unavailable-tooling-source");
    const auto managed = root.path() / "managed";
    write_sample_build_project(managed, "sample");
    const auto run_installed = [&](const std::vector<std::string>& arguments) {
        std::vector<std::string> command { bwrap,
                                           "--unshare-user",
                                           "--uid",
                                           "0",
                                           "--gid",
                                           "0",
                                           "--unshare-pid",
                                           "--unshare-net",
                                           "--die-with-parent",
                                           "--ro-bind",
                                           prefix.string(),
                                           "/",
                                           "--proc",
                                           "/proc",
                                           "--dev",
                                           "/dev",
                                           "--tmpfs",
                                           "/tmp",
                                           "--bind",
                                           managed.string(),
                                           "/project",
                                           "--chdir",
                                           "/project",
                                           "--clearenv",
                                           "--setenv",
                                           "PATH",
                                           "/usr/bin",
                                           "--setenv",
                                           "LC_ALL",
                                           "C",
                                           "--" };
        command.insert(command.end(), arguments.begin(), arguments.end());
        return ecosystem::capture_command_result(command);
    };
    for (const auto& executable :
         { "/usr/bin/runtime_app", "/usr/bin/runtime_tool" }) {
        const auto result = run_installed({ executable });
        require_true(
            result.exit_code == 0,
            "installed app/companion must run without host libraries, build "
            "trees or environment overrides: "
                + result.output
        );
    }
    auto result = run_installed({ "/usr/bin/marx", "sync" });
    require_true(
        result.exit_code == 0,
        "installed Marx must sync in the isolated root: " + result.output
    );
    require_contains(
        read_text(managed / "CMakeLists.txt"),
        "# isolated package template marker",
        "installed actor must use its packaged data"
    );
    result = run_installed({ "/usr/bin/engels", "check", "repo" });
    require_true(
        result.exit_code == 0,
        "installed Engels must verify the isolated managed project: "
            + result.output
    );
    result = run_installed(
        { "/usr/bin/marx", "mutate", "add", "module", "sample:app", "helper" }
    );
    require_true(
        result.exit_code == 0 && fs::exists(managed / "include/helper.hpp"),
        "installed Marx must mutate using its packaged templates: "
            + result.output
    );
    fs::remove(
        prefix / "usr/share/manifesto/templates/cmake/surface_prefix.tpl"
    );
    result = run_installed({ "/usr/bin/marx", "sync" });
    require_true(
        result.exit_code != 0,
        "missing installed template must not fall back to a host checkout"
    );
    require_contains(
        result.output, "surface_prefix.tpl",
        "missing installed data must be named"
    );
    std::cout << "native Pacman installation, runtime closure and installed "
                 "actor data passed\n";
}

void test_prerelease_packages_installed_source_provider_runtime() {
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto provider = root.path() / "provider";
    const auto consumer = root.path() / "consumer";
    for (const auto& project : { provider, consumer }) {
        auto authored = json::parse(read_text(project / "manifest.json"));
        authored["artifacts"][0]["kind"] = "shared_lib";
        if (project == consumer)
            authored["artifacts"][1]["name"] = "consumer";
        write_text(project / "manifest.json", authored.dump(2));
    }
    scoped_env override_path("NUMBERS_SOURCE_DIR", provider.string());
    const auto result
        = run_marx_cli(consumer, "prerelease --version-base 1.0.0");
    require_true(
        result.exit_code == 0,
        "source provider runtime must package:\n" + result.output
    );
    const auto release = ecosystem::local_prerelease_dir(consumer);
    const auto transcript = read_text(
        first_recursive_file_named(release / "attempts", "commands.log")
    );
    require_contains(
        transcript, "--install",
        "provider installation commands must join the attempt transcript"
    );
    require_contains(
        transcript, "numbers",
        "provider configure/build output must join the attempt transcript"
    );
    const auto inventory = json::parse(
        read_text(first_recursive_file_named(release / "work", "runtime.json"))
    );
    require_true(
        inventory.at("bundled").size() == 1,
        "installed provider shared library must be bundled once"
    );
    require_contains(
        inventory.at("bundled").at(0).at("source").get<std::string>(),
        "/install/release/",
        "provider runtime must come from its installed package"
    );
    const auto prefix = root.path() / "installed";
    fs::create_directories(prefix);
    const auto unpacked = ecosystem::capture_command_result(
        { "tar", "-xzf",
          first_recursive_file_with_suffix(release / "pacman", ".pkg.tar.gz")
              .string(),
          "-C", prefix.string() }
    );
    require_true(unpacked.exit_code == 0, "provider package must extract");
    fs::rename(provider, root.path() / "unavailable-provider");
    fs::rename(consumer, root.path() / "unavailable-consumer");
    const auto run = ecosystem::capture_command_result(
        { (prefix / "usr/bin/consumer").string() }, root.path(),
        { { "LD_LIBRARY_PATH", (prefix / "usr/lib").string() },
          { "LD_PRELOAD", "" } }
    );
    require_true(
        run.exit_code == 0,
        "packaged consumer must run without either project's "
        "source/build/install trees:\n"
            + run.output
    );
}

void test_prerelease_retains_soname_aliases_and_rejects_runtime_conflicts() {
    temp_dir root;
    const auto project = root.path() / "project";
    const auto first_lib = project / "version-one";
    const auto second_lib = project / "version-two";
    const auto primary = project / "binaries/app";
    const auto companion
        = ecosystem::local_build_dir(project, "release") / "cli";
    write_text(project / "value.cpp", "int value() { return 42; }\n");
    write_text(
        project / "main.cpp",
        "int value(); int main() { return value() == 42 ? 0 : 1; }\n"
    );
    auto command = [&](const std::vector<std::string>& arguments) {
        const auto run = ecosystem::capture_command_result(arguments, project);
        require_true(
            run.exit_code == 0,
            "versioned native fixture must build:\n" + run.output
        );
    };
    auto library = [&](const fs::path& directory) {
        fs::create_directories(directory);
        command(
            { "c++", "-shared", "-fPIC", (project / "value.cpp").string(),
              "-Wl,-soname,libversioned.so.3", "-o",
              (directory / "libversioned.so.3.2").string() }
        );
        fs::create_symlink(
            "libversioned.so.3.2", directory / "libversioned.so.3"
        );
        fs::create_symlink("libversioned.so.3", directory / "libversioned.so");
    };
    auto application = [&](const fs::path& output, const fs::path& directory) {
        fs::create_directories(output.parent_path());
        command(
            { "c++", (project / "main.cpp").string(), "-L" + directory.string(),
              "-lversioned", "-Wl,-rpath," + directory.string(), "-o",
              output.string() }
        );
    };
    library(first_lib);
    application(primary, first_lib);
    application(companion, first_lib);
    auto value = sample_dual_run_manifest();
    value.install_artifacts = { "tool:cli" };
    const auto selected = ecosystem::resolve_artifact(
        value, ecosystem::artifact_ref { "app", "app" }
    );
    require_true(selected.has_value(), "native fixture primary must resolve");
    ecosystem::prerelease_version version;
    ecosystem::prerelease_artifacts artifacts;
    std::string error;
    auto prepare = [&]() {
        return ecosystem::create_prerelease_packages(
            project, value, *selected, primary, "1.0.0", {}, &version,
            &artifacts, &error
        );
    };
    require_true(
        prepare() == ecosystem::command_error::ok,
        "versioned runtime must package: " + error
    );
    const auto prefix = root.path() / "installed";
    fs::create_directories(prefix);
    command(
        { "tar", "-xzf", artifacts.pacman_package_path.string(), "-C",
          prefix.string() }
    );
    for (const auto& name : { "libversioned.so.3", "libversioned.so.3.2" })
        require_true(
            fs::is_regular_file(prefix / "usr/lib" / name),
            "SONAME and target filename must both survive staging"
        );
    fs::rename(first_lib, project / "unavailable-version");
    const auto run = ecosystem::capture_command_result(
        { (prefix / "usr/bin/app").string() }, root.path(),
        { { "LD_LIBRARY_PATH", (prefix / "usr/lib").string() },
          { "LD_PRELOAD", "" } }
    );
    require_true(
        run.exit_code == 0,
        "installed SONAME must resolve without source library paths:\n"
            + run.output
    );
    fs::rename(project / "unavailable-version", first_lib);

    const auto release = ecosystem::local_prerelease_dir(project);
    const auto state = read_text(release / "state.json");
    library(second_lib);
    application(companion, second_lib);
    require_true(
        prepare() == ecosystem::command_error::task_failed,
        "ambiguous runtime names must fail publication"
    );
    require_contains(
        error, "Conflicting package runtime libraries",
        "collision must name the CMake dependency conflict"
    );
    require_contains(
        error, "libversioned.so.3", "conflict must identify the SONAME"
    );
    require_true(
        read_text(release / "state.json") == state,
        "runtime conflicts must preserve release state"
    );

    const auto outside = root.path() / "unmanaged-library";
    library(outside);
    application(primary, outside);
    application(companion, outside);
    require_true(
        prepare() == ecosystem::command_error::task_failed,
        "unmanaged non-system runtime dependencies must fail clearly"
    );
    require_contains(
        error, "outside the project and system library directories",
        "unsupported runtime location must have explicit guidance"
    );
    require_true(
        read_text(release / "state.json") == state,
        "unsupported runtime location must preserve release state"
    );
}

void test_prerelease_runtime_failures_preserve_previous_release() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_runtime_package_project(project);
    auto result = run_marx_cli(project, "prerelease --version-base 1.0.0");
    require_true(
        result.exit_code == 0,
        "initial runtime release must succeed:\n" + result.output
    );
    const auto release = ecosystem::local_prerelease_dir(project);
    const auto state = read_text(release / "state.json");
    const auto package
        = first_recursive_file_with_suffix(release / "pacman", ".pkg.tar.gz");
    const auto previous_package = read_text(package);
    const auto loaded = ecosystem::load_manifest(project / "manifest.json");
    require_true(loaded.value.has_value(), "runtime fixture must load");
    const auto resolved
        = ecosystem::resolve_artifact(*loaded.value, std::nullopt);
    const auto build = ecosystem::local_build_dir(project, "release");
    ecosystem::prerelease_version version;
    ecosystem::prerelease_artifacts artifacts;
    std::string error;
    auto prepare = [&]() {
        const auto status = ecosystem::create_prerelease_packages(
            project, *loaded.value, *resolved, build / "runtime_app",
            std::nullopt, {}, &version, &artifacts, &error
        );
        require_true(
            status == ecosystem::command_error::task_failed,
            "runtime inspection failure must stop publication"
        );
        require_true(
            read_text(release / "state.json") == state
                && read_text(package) == previous_package,
            "runtime failures must preserve the previous package and version "
            "counter"
        );
    };
    fs::rename(build / "libleaf.so", root.path() / "unavailable-library.so");
    prepare();
    require_contains(
        error, "Unresolved package runtime libraries",
        "missing runtime must be diagnosed before publication"
    );
    require_contains(
        error, "libleaf.so",
        "runtime failure must identify its unresolved library"
    );
    fs::rename(root.path() / "unavailable-library.so", build / "libleaf.so");
    const auto bin = root.path() / "tools";
    write_executable_script(
        bin / "cmake",
        "#!/bin/sh\nprintf 'runtime scanner failed\\n' >&2\nexit 17\n"
    );
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    prepare();
    require_contains(
        error, "runtime scanner failed",
        "native scanner errors must remain attributable"
    );
    require_contains(
        read_text(
            first_recursive_file_named(release / "work", "runtime-scan.log")
        ),
        "runtime scanner failed",
        "failed runtime inspection must retain its log"
    );
}

void test_prerelease_tooling_archives_carry_their_template_bundle() {
    temp_dir root;
    const auto source = root.path() / "tooling-source";
    const auto build = ecosystem::local_build_dir(source, "release");
    fs::create_directories(build);
    fs::copy_file(marx_binary_path(), build / "marx");
    fs::copy_file(engels_binary_path(), build / "engels");
    fs::copy_file(
        test_cli_build_dir() / "CMakeCache.txt", build / "CMakeCache.txt"
    );
    fs::copy(
        fs::path(ECOS_TEST_SOURCE_DIR) / "templates", source / "templates",
        fs::copy_options::recursive
    );
    const auto surface = source / "templates/cmake/surface_prefix.tpl";
    write_text(
        surface, "# package-owned template bundle\n" + read_text(surface)
    );
    write_text(
        source / "assets/unrequested.txt", "assets are not runtime templates\n"
    );
    const auto loaded = ecosystem::load_manifest(
        fs::path(ECOS_TEST_SOURCE_DIR) / "manifest.json"
    );
    require_true(loaded.value.has_value(), "tooling manifest must load");
    const auto selected
        = ecosystem::resolve_artifact(*loaded.value, std::nullopt);
    ecosystem::prerelease_version version;
    ecosystem::prerelease_artifacts artifacts;
    std::string error;
    {
        const auto override_root = root.path() / "override-templates";
        write_text(
            override_root / "cmake/surface_prefix.tpl",
            "{{wrong_template_version}}\n"
        );
        scoped_env override_path(
            "MANIFESTO_TEMPLATE_ROOT", override_root.string()
        );
        const auto status = ecosystem::create_prerelease_packages(
            source, *loaded.value, *selected, build / "marx", "1.0.0", {},
            &version, &artifacts, &error
        );
        require_true(
            status == ecosystem::command_error::ok,
            "tooling package must include its source data: " + error
        );
    }
    const auto state = json::parse(
        read_text(ecosystem::local_prerelease_dir(source) / "state.json")
    );
    const auto files = state.at("packages")
                           .at(0)
                           .at("latest_files")
                           .get<std::vector<std::string>>();
    require_true(
        std::find(
            files.begin(), files.end(),
            "usr/share/manifesto/templates/tracked/.github/workflows/"
            "tests.yml.tpl"
        ) != files.end(),
        "package metadata must account for hidden runtime template files"
    );
    const auto pacman = root.path() / "pacman-install";
    const auto debian = root.path() / "debian-install";
    fs::create_directories(pacman);
    fs::create_directories(debian);
    auto command
        = [&](const std::vector<std::string>& args, const fs::path& cwd) {
              const auto result = ecosystem::capture_command_result(args, cwd);
              require_true(
                  result.exit_code == 0,
                  "tooling archive extraction must succeed:\n" + result.output
              );
          };
    command(
        { "tar", "-xzf", artifacts.pacman_package_path.string(), "-C",
          pacman.string() },
        root.path()
    );
    command({ "ar", "x", artifacts.deb_package_path.string() }, debian);
    command(
        { "tar", "-xzf", (debian / "data.tar.gz").string(), "-C",
          debian.string() },
        root.path()
    );
    fs::rename(source, root.path() / "unavailable-tooling-source");
    scoped_env no_override("MANIFESTO_TEMPLATE_ROOT", "");
    scoped_env no_legacy_override("ECOSYSTEM_TEMPLATE_ROOT", "");
    scoped_env no_library_override("LD_LIBRARY_PATH", "");
    for (const auto& prefix : { pacman, debian }) {
        const auto bundle = prefix / "usr/share/manifesto/templates";
        require_true(
            fs::is_regular_file(
                bundle / "tracked/.github/workflows/tests.yml.tpl"
            ),
            "each archive must contain the complete hidden template tree"
        );
        require_true(
            !fs::exists(prefix / "usr/bin/assets"),
            "tooling runtime data must not require project assets"
        );
        const auto project = prefix / "managed-project";
        write_sample_build_project(project, "sample");
        const auto marx = prefix / "usr/bin/marx";
        const auto engels = prefix / "usr/bin/engels";
        auto result = run_cli_with_binary(marx, project, "sync");
        require_true(
            result.exit_code == 0,
            "extracted Marx must sync without a source bundle or override:\n"
                + result.output
        );
        require_contains(
            read_text(project / "CMakeLists.txt"),
            "# package-owned template bundle",
            "actor must use data shipped with its package"
        );
        result = run_cli_with_binary(engels, project, "check repo");
        require_true(
            result.exit_code == 0,
            "extracted Engels must share the packaged data contract:\n"
                + result.output
        );
        result = run_cli_with_binary(
            marx, project, "mutate add module sample:app helper"
        );
        require_true(
            result.exit_code == 0
                && fs::is_regular_file(project / "include/helper.hpp"),
            "packaged mutation must resolve its runtime templates:\n"
                + result.output
        );
        fs::rename(
            bundle / "cmake/surface_prefix.tpl",
            bundle / "cmake/surface_prefix.saved"
        );
        result = run_cli_with_binary(marx, project, "sync");
        require_true(
            result.exit_code != 0,
            "an incomplete installed bundle must not fall back to checkout "
            "templates"
        );
        require_contains(
            result.output, bundle.string(),
            "missing packaged data must identify its installed location"
        );
    }
}

void test_prerelease_rejects_missing_or_aliased_tooling_data() {
    temp_dir root;
    auto value = sample_dual_run_manifest();
    value.id = "manifesto";
    value.install_artifacts = { "tool:cli" };
    value.components.front().artifacts.front().name = "marx";
    value.components.back().artifacts.front().name = "engels";
    const auto selected = ecosystem::resolve_artifact(
        value, ecosystem::artifact_ref { "app", "app" }
    );
    require_true(selected.has_value(), "tooling fixture must resolve");
    const auto build = ecosystem::local_build_dir(root.path(), "release");
    write_text(build / "marx", "actor fixture");
    write_text(build / "engels", "actor fixture");
    ecosystem::prerelease_version version;
    ecosystem::prerelease_artifacts artifacts;
    std::string error;
    auto reject = [&]() {
        const auto status = ecosystem::create_prerelease_packages(
            root.path(), value, *selected, build / "marx", "1.0.0", {},
            &version, &artifacts, &error
        );
        require_true(
            status == ecosystem::command_error::task_failed,
            "unsupported tooling data must fail packaging"
        );
        require_true(
            !fs::exists(
                ecosystem::local_prerelease_dir(root.path()) / "state.json"
            ),
            "tooling data failure must not publish version state"
        );
    };
    reject();
    require_contains(
        error, "requires its source template bundle",
        "missing data must have an actionable path"
    );
    fs::create_directories(root.path() / "templates");
    reject();
    require_contains(
        error, "template bundle is empty",
        "empty data must not produce a binary-only package"
    );
    const auto outside = root.path() / "external.tpl";
    write_text(outside, "external data");
    fs::create_symlink(outside, root.path() / "templates/alias.tpl");
    reject();
    require_contains(
        error, "unsupported tooling template entry",
        "aliased data must not escape its bundle"
    );
    fs::remove(root.path() / "templates/alias.tpl");
    write_text(root.path() / "templates/example.tpl", "local data");
    write_text(
        build / "CMakeCache.txt",
        "CMAKE_INSTALL_BINDIR:PATH=tools/bin\nCMAKE_INSTALL_DATADIR:PATH=data/"
        "share\n"
    );
    reject();
    require_contains(
        error, "use cmake --install for a custom layout",
        "incompatible baked template paths must fail clearly"
    );
}

void test_prerelease_respects_asset_install_intent() {
    temp_dir root;
    auto value = sample_dual_run_manifest();
    const auto selected = ecosystem::resolve_artifact(
        value, ecosystem::artifact_ref { "app", "app" }
    );
    require_true(selected.has_value(), "asset fixture must resolve");
    const auto binary = root.path() / "output/app";
    write_text(binary, "application fixture");
    write_text(root.path() / "assets/visible.txt", "opted-in asset\n");
    ecosystem::prerelease_version version;
    ecosystem::prerelease_artifacts artifacts;
    std::string error;
    for (const bool enabled : { false, true, false }) {
        value.install_assets = enabled;
        const auto status = ecosystem::create_prerelease_packages(
            root.path(), value, *selected, binary, "1.0.0", {}, &version,
            &artifacts, &error
        );
        require_true(
            status == ecosystem::command_error::ok,
            "asset intent packaging must succeed: " + error
        );
        const auto listing = ecosystem::capture_command_result(
            { "tar", "-tzf", artifacts.pacman_package_path.string() }
        );
        require_true(
            listing.exit_code == 0, "native archive listing must succeed"
        );
        require_true(
            (listing.output.find("usr/bin/assets/visible.txt")
             != std::string::npos)
                == enabled,
            "each archive must reflect current asset install intent, without "
            "stale payloads"
        );
    }
}

} // namespace ecosystem_test_support
