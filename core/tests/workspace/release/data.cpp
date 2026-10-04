#include "../../suite/test_support.hpp"
#include "../release.hpp"

namespace ecosystem_test_support {

void test_release_tooling_bundle() {
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

void test_release_tooling_errors() {
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

void test_release_asset_intent() {
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
