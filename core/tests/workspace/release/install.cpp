#include "../../suite/test_support.hpp"
#include "../release.hpp"

namespace ecosystem_test_support {

void test_release_isolated_install(const fs::path& cache) {
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
    fs::create_directories(tooling_source / "assets");
    fs::copy(
        fs::path(ECOS_TEST_SOURCE_DIR) / "assets/templates",
        tooling_source / "assets/templates", fs::copy_options::recursive
    );
    const auto surface
        = tooling_source / "assets/templates/cmake/surface_prefix.tpl";
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
        const auto installed
            = ecosystem::capture_command_result({ pacman, "-Q", "--",
                                                  requested });
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
            const auto info
                = ecosystem::capture_command_result({ pacman, "-Qp", "--",
                                                      entry.path().string() });
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
    result = run_installed({ "/usr/bin/marx", "mutate", "add", "module",
                             "sample:app", "helper" });
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

} // namespace ecosystem_test_support
