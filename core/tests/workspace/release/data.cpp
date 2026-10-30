#include "../../../src/workspace/release/internal.hpp"
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
    fs::create_directories(source / "assets");
    fs::copy(
        fs::path(ECOS_TEST_SOURCE_DIR) / "assets/templates",
        source / "assets/templates", fs::copy_options::recursive
    );
    const auto surface = source / "assets/templates/cmake/surface_prefix.tpl";
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
    std::string error;
    fs::path input_path;
    {
        const auto override_root = root.path() / "override-templates";
        write_text(
            override_root / "cmake/surface_prefix.tpl",
            "{{wrong_template_version}}\n"
        );
        scoped_env override_path(
            "ECOSYSTEM_TEMPLATE_ROOT", override_root.string()
        );
        input_path
            = prepare_fixture(source, *loaded.value, *selected, build / "marx");
    }
    const auto input = json::parse(read_text(input_path));
    std::vector<std::string> files;
    for (const auto& file : input.at("distribution").at("files"))
        files.push_back(file.at("install_path").get<std::string>());
    require_true(
        std::find(
            files.begin(), files.end(),
            "usr/share/ecosystem/templates/tracked/.github/workflows/"
            "tests.yml.tpl"
        ) != files.end(),
        "package metadata must account for hidden runtime template files"
    );
    const auto pacman = root.path() / "pacman-install";
    const auto debian = root.path() / "debian-install";
    fs::copy(
        input_path.parent_path() / "payload", pacman,
        fs::copy_options::recursive
    );
    fs::copy(
        input_path.parent_path() / "payload", debian,
        fs::copy_options::recursive
    );
    fs::rename(source, root.path() / "unavailable-tooling-source");
    scoped_env no_override("ECOSYSTEM_TEMPLATE_ROOT", "");
    scoped_env no_legacy_override("ECOSYSTEM_TEMPLATE_ROOT", "");
    scoped_env no_library_override("LD_LIBRARY_PATH", "");
    for (const auto& prefix : { pacman, debian }) {
        const auto bundle = prefix / "usr/share/ecosystem/templates";
        require_true(
            fs::is_regular_file(
                bundle / "tracked/.github/workflows/tests.yml.tpl"
            ),
            "each prepared product must contain the complete hidden template "
            "tree"
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
    value.id = "ecosystem";
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
    std::string error;
    auto reject = [&]() {
        bool rejected = false;
        try {
            (void)prepare_fixture(
                root.path(), value, *selected, build / "marx"
            );
        } catch (const std::exception& failure) {
            rejected = true;
            error = failure.what();
        }
        require_true(
            rejected, "unsupported tooling data must fail preparation"
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
    fs::create_directories(root.path() / "assets/templates");
    reject();
    require_contains(
        error, "template bundle is empty",
        "empty data must not produce a binary-only package"
    );
    const auto outside = root.path() / "external.tpl";
    write_text(outside, "external data");
    fs::create_symlink(outside, root.path() / "assets/templates/alias.tpl");
    reject();
    require_contains(
        error, "unsupported tooling template entry",
        "aliased data must not escape its bundle"
    );
    fs::remove(root.path() / "assets/templates/alias.tpl");
    write_text(root.path() / "assets/templates/example.tpl", "local data");
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
    write_text(root.path() / "assets/sets/visible.txt", "opted-in asset\n");
    write_text(
        root.path() / "assets/sets/showcase/runtime.dat",
        "nested runtime asset\n"
    );
    write_text(root.path() / "assets/showcase/demo.pdf", "presentation only\n");
    for (const auto* name : { ".cache", ".thumbnail_cache" })
        for (const auto* file :
             { "image.svg", "metadata.json", "download.bin" })
            write_text(
                root.path() / "assets/sets" / name / file, "transient output"
            );
    write_text(
        root.path() / "assets/sets/thumbnail_cache/authored.svg",
        "authored output"
    );
    std::string error;
    for (const bool enabled : { false, true, false }) {
        value.install_assets = enabled;
        const auto input_path
            = prepare_fixture(root.path(), value, *selected, binary);
        const auto input = json::parse(read_text(input_path));

        struct {
            std::string output;
        } listing;

        for (const auto& file : input.at("distribution").at("files"))
            listing.output += file.at("install_path").get<std::string>() + "\n";
        require_true(
            (listing.output.find("usr/bin/assets/sets/visible.txt")
             != std::string::npos)
                == enabled,
            "each prepared product must reflect current asset install intent, "
            "without "
            "stale payloads"
        );
        require_true(
            (listing.output.find("usr/bin/assets/sets/showcase/runtime.dat")
             != std::string::npos)
                == enabled,
            "nested runtime directories keep their ownership"
        );
        require_not_contains(
            listing.output, "usr/bin/assets/showcase/",
            "prepared products must exclude project presentation media"
        );
        require_not_contains(
            listing.output, "/.cache/",
            "prepared products must exclude ordinary hidden caches"
        );
        require_not_contains(
            listing.output, "/.thumbnail_cache/",
            "prepared products must exclude named hidden caches"
        );
        require_true(
            (listing.output.find(
                 "usr/bin/assets/sets/thumbnail_cache/authored.svg"
             )
             != std::string::npos)
                == enabled,
            "prepared products must preserve similarly named authored "
            "directories"
        );
    }
}

void test_release_cache_inventory() {
    temp_dir root;
    const auto source = root.path() / "tooling-source";
    const auto loaded = ecosystem::load_manifest(
        fs::path(ECOS_TEST_SOURCE_DIR) / "manifest.json"
    );
    require_true(loaded.value.has_value(), "tooling manifest must load");
    const auto selected
        = ecosystem::resolve_artifact(*loaded.value, std::nullopt);
    require_true(selected.has_value(), "tooling artifact must resolve");
    write_text(
        source / "assets/templates/tracked/.github/actions/native.tpl",
        "required hidden template\n"
    );
    write_text(
        source / "assets/templates/cache/reference.tpl", "authored lookalike\n"
    );
    write_text(
        source / "assets/templates/.cache/ignored.tpl", "transient template\n"
    );
    write_text(
        source / "assets/templates/nested/.renderer_cache/ignored.svg",
        "transient artwork\n"
    );
    const auto stage = root.path() / "payload";
    std::string error;
    require_true(
        ecosystem::release_detail::stage_tooling_templates(
            source, *loaded.value, selected->ref, stage, &error
        ),
        error
    );
    const auto bundle = stage / "usr/share/ecosystem/templates";
    require_true(
        read_text(bundle / "tracked/.github/actions/native.tpl")
            == "required hidden template\n",
        "public tooling bundle must retain hidden authored templates"
    );
    require_true(
        read_text(bundle / "cache/reference.tpl") == "authored lookalike\n",
        "public tooling bundle must retain similarly named source directories"
    );
    require_true(
        !fs::exists(bundle / ".cache")
            && !fs::exists(bundle / "nested/.renderer_cache"),
        "public tooling bundle must exclude reserved caches independently of "
        "install_assets"
    );
    fs::create_symlink(
        "cache/reference.tpl", source / "assets/templates/alias.tpl"
    );
    require_true(
        !ecosystem::release_detail::stage_tooling_templates(
            source, *loaded.value, selected->ref, stage, &error
        ),
        "tooling bundle's stricter symlink restriction must remain intact"
    );
    require_contains(
        error, "unsupported tooling template entry",
        "retain the original bundle diagnostic"
    );
}

} // namespace ecosystem_test_support
