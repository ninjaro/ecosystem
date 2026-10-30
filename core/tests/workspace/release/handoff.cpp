#include "../../suite/test_support.hpp"
#include "../release.hpp"

namespace ecosystem_test_support {
namespace {

    void check_inventory(const json& input) {
        require_true(
            input.at("input_version") == 1
                && input.at("distribution").at("complete") == true,
            "prepared input must declare the current full-product contract"
        );
        const fs::path project(
            input.at("project").at("root").get<std::string>()
        );
        std::set<std::string> destinations;
        for (const auto& file : input.at("distribution").at("files")) {
            const auto path = project / file.at("source").get<std::string>();
            std::string error;
            require_true(
                fs::is_regular_file(path) && !fs::is_symlink(path)
                    && fs::file_size(path)
                        == file.at("size").get<std::uintmax_t>()
                    && ecosystem::prepared_file_sha256(path, &error)
                        == file.at("sha256").get<std::string>(),
                "every exported input must exist and match current "
                "size/SHA256: "
                    + error
            );
            require_true(
                destinations.insert(file.at("install_path").get<std::string>())
                    .second,
                "no duplicated payload destinations"
            );
        }
        require_true(
            !input.at("provenance").contains("evidence"),
            "preparation is not Engels evidence"
        );
    }

    void check_planner(const fs::path& planner, const json& input) {
        const fs::path project(
            input.at("project").at("root").get<std::string>()
        );
        const auto path = project / ".ecosystem/planner-input.json";
        write_text(path, input.dump(2));
        const auto before = read_text(path);
        const auto result = ecosystem::capture_command_result(
            { planner.string(), "plan", "--input", path.string() }, project,
            { { "PATH", "" } }
        );
        require_true(
            result.exit_code == 3,
            "independent planner must remain provisional: " + result.output
        );
        const auto plan = json::parse(result.output);
        require_true(
            plan.at("plan_valid") == true && plan.at("release_ready") == false
                && plan.at("files").size()
                    == input.at("distribution").at("files").size(),
            "real handoff is understood without authorizing publication"
        );
        require_true(
            read_text(path) == before,
            "the release actor must not rewrite handoff input"
        );
    }

} // namespace

void test_prerelease_prepared_full_product() {
    temp_dir root;
    write_sample_dual_run_project(root.path());
    auto value = sample_dual_run_manifest();
    value.version = "1.2.3";
    value.install_artifacts = { "app:app", "tool:cli" };
    value.install_assets = true;
    std::string error;
    require_true(
        ecosystem::save_manifest(root.path() / "manifest.json", value, &error),
        error
    );
    write_text(
        root.path() / "assets/dataset/test/input.txt", "runtime fixture\n"
    );
    write_text(
        root.path() / "assets/dataset/test/.agent_cache/cached.txt",
        "excluded cache\n"
    );
    write_text(root.path() / "assets/docs/index.md", "excluded stable docs\n");
    const auto tools = root.path() / "tools";
    write_fake_configure_and_build_cmake(tools / "cmake");
    write_executable_script(tools / "git", R"(#!/bin/sh
test "$GIT_OPTIONAL_LOCKS" = 0 || exit 41
case "$1" in
  ls-files) printf 'manifest.json\n' ;;
  status) test "$ECOSYSTEM_TEST_DIRTY_SOURCE" != yes || printf ' M src/main.cpp\n' ;;
  rev-parse) printf 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n' ;;
  *) exit 1 ;;
esac
exit 0
)");
    scoped_env path("PATH", tools.string() + ":" + current_path_env());
    scoped_env cmake_log(
        "FAKE_CMAKE_LOG", (root.path() / "cmake.log").string()
    );
    const auto result = run_marx_cli(root.path(), "prerelease");
    const auto input = prepared_input(root.path(), result);
    check_inventory(input);
    require_true(
        input.at("provenance").at("source_revision").get<std::string>()
            == std::string(40, 'a'),
        "clean tracked project identity is reported using read-only Git "
        "commands"
    );
    require_true(
        input.at("distribution").at("artifacts").size() == 2
            && input.at("product").at("package_name") == "dual-run-sample"
            && input.at("version_base") == "1.2.3",
        "facade relation and actual install companions must be retained"
    );
    std::set<std::string> files;
    for (const auto& file : input.at("distribution").at("files"))
        files.insert(file.at("install_path").get<std::string>());
    require_true(
        files
            == std::set<
                std::string> { "usr/bin/app", "usr/bin/cli",
                               "usr/bin/assets/dataset/test/input.txt" },
        "inventory uses actual staged resource policy, not all assets or only "
        "executables"
    );
    auto mismatched = input;
    mismatched["version_base"] = "9.9.9";
    const auto bad_version
        = prepared_payload(input).parent_path() / "input.json";
    const auto original_input = read_text(bad_version);
    write_text(bad_version, mismatched.dump(2));
    const auto rejected_version = run_engels_cli(
        root.path(),
        "check prepared --input "
            + bad_version.lexically_relative(root.path()).generic_string()
    );
    write_text(bad_version, original_input);
    require_true(
        rejected_version.exit_code == 5,
        "Engels must reject conflicting project version intent"
    );
    require_contains(
        rejected_version.output,
        "prepared version conflicts with manifest version",
        "Engels checks manifest intent independently of a preparation summary"
    );
    const auto release = ecosystem::local_prerelease_dir(root.path());
    require_true(
        !fs::exists(release / "state.json") && !fs::exists(release / "deb")
            && !fs::exists(release / "pacman"),
        "prepare-only must not reserve versions or publish repositories"
    );
    require_contains(
        read_text(root.path() / "cmake.log"), "--target tool__cli",
        "Marx builds companions"
    );
    const auto commands = read_text(
        first_recursive_file_named(release / "attempts", "commands.log")
    );
    for (const auto* tool : { "tar", "ar", "gzip", "gpg" })
        require_true(
            commands.find('"' + ecosystem::find_command_path(tool) + '"')
                == std::string::npos,
            "prepare-only must not probe or invoke package archive/signing "
            "tools"
        );
    write_text(release / "state.json", "{\"packages\":[]}\n");
    const auto state_before = read_text(release / "state.json");
    scoped_env dirty("ECOSYSTEM_TEST_DIRTY_SOURCE", "yes");
    const auto selected = prepared_input(
        root.path(),
        run_marx_cli(root.path(), "prerelease tool:cli --prepare-only")
    );
    require_true(
        selected.at("distribution").at("artifacts").size() == 1
            && selected.at("product").at("package_name")
                == "dual-run-sample-tool-cli",
        "explicit non-facade selection does not pull unrelated install roots"
    );
    require_true(
        !selected.at("provenance").contains("source_revision")
            && read_text(release / "state.json") == state_before,
        "dirty source does not claim a clean revision; preparation preserves "
        "existing counters"
    );
    const auto contradictory = run_marx_cli(
        root.path(), "prerelease --prepare-only --version-base 9.9.9"
    );
    require_true(
        contradictory.exit_code == 2
            && read_text(release / "state.json") == state_before,
        "caller version cannot replace manifest project identity or consume a "
        "counter"
    );
    require_contains(
        contradictory.output, "contradicts manifest version",
        "version conflict must identify the authoritative project intent"
    );
    const auto rejected
        = run_marx_cli(root.path(), "prerelease --prepare-only --sign");
    require_true(
        rejected.exit_code == 2,
        "prepare-only rejects misleading signing requests"
    );
}

void test_prerelease_prepared_failures() {
    temp_dir root;
    write_sample_dual_run_project(root.path());
    auto value = sample_dual_run_manifest();
    value.install_artifacts = { "tool:cli" };
    value.install_assets = true;
    std::string error;
    require_true(
        ecosystem::save_manifest(root.path() / "manifest.json", value, &error),
        error
    );
    const auto loaded = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        loaded.value.has_value(), "negative fixture manifest must load"
    );
    const auto& current = *loaded.value;
    const auto primary = ecosystem::resolve_artifact(current, std::nullopt);
    require_true(primary.has_value(), "negative fixture primary must resolve");
    const auto build = ecosystem::local_build_dir(root.path(), "release");
    const auto output = build / "app";
    write_text(output, "prepared primary\n");
    const auto expected = ecosystem::prepared_file_sha256(
        root.path() / "manifest.json", &error
    );
    const auto prepare = [&](const std::string& name, const std::string& digest,
                             const fs::path& primary_file) {
        fs::path input;
        error.clear();
        const auto target = ecosystem::local_prerelease_dir(root.path())
            / "attempts" / name / "prepared";
        const auto status = ecosystem::export_prepared_release_input(
            root.path(), current, *primary, primary_file, target, digest,
            std::nullopt, &input, &error
        );
        require_true(
            status != ecosystem::command_error::ok && input.empty()
                && !fs::exists(target / "input.json"),
            "failed export must never present a complete inventory: " + name
        );
        return error;
    };
    require_contains(
        prepare("missing-companion", expected, output),
        "missing install artifact", "missing companion blocks export"
    );
    write_text(build / "cli", "prepared companion\n");
    require_contains(
        prepare("missing-primary", expected, build / "absent"),
        "missing primary output", "missing selected primary blocks export"
    );
    require_contains(
        prepare("stale-manifest", std::string(64, '0'), output),
        "manifest changed", "stale manifest preparation is rejected"
    );
    write_text(build / "guessed", "wrong primary\n");
    require_contains(
        prepare("wrong-output", expected, build / "guessed"), "primary output",
        "guessed executable is not the selected product"
    );
    write_text(
        root.path() / "assets/dataset/test/unrepresentable\\name.txt",
        "cannot express this in input v1\n"
    );
    require_contains(
        prepare("unsafe-destination", expected, output), "not representable",
        "unrepresentable data cannot be silently omitted"
    );
    fs::remove(root.path() / "assets/dataset/test/unrepresentable\\name.txt");
    fs::create_symlink(
        "absent.txt", root.path() / "assets/dataset/test/missing.txt"
    );
    const auto failed_data = prepare("missing-data", expected, output);
    require_true(
        !failed_data.empty(), "missing selected runtime data must block staging"
    );
    fs::remove(root.path() / "assets/dataset/test/missing.txt");
    write_text(
        root.path() / "manifest.json",
        read_text(root.path() / "manifest.json") + "\n"
    );
    require_contains(
        prepare("changed-bytes", expected, output), "manifest changed",
        "exact raw manifest bytes are bound"
    );
    const auto collision = root.path() / "collision";
    write_sample_build_project(collision, "sample");
    auto conflict = sample_build_manifest("sample");
    conflict.install_assets = true;
    conflict.components.front().artifacts.front().name = "assets";
    require_true(
        ecosystem::save_manifest(collision / "manifest.json", conflict, &error),
        error
    );
    write_text(
        collision / "assets/dataset/test/data.txt", "required resource\n"
    );
    const auto parsed = ecosystem::load_manifest(collision / "manifest.json");
    require_true(
        parsed.value.has_value(), "destination-conflict fixture must load"
    );
    const auto selected
        = ecosystem::resolve_artifact(*parsed.value, std::nullopt);
    require_true(
        selected.has_value(), "destination-conflict product must resolve"
    );
    const auto binary
        = ecosystem::local_build_dir(collision, "release") / "assets";
    write_text(binary, "prepared executable named assets\n");
    const auto target = ecosystem::local_prerelease_dir(collision)
        / "attempts/collision/prepared";
    fs::path input;
    const auto digest
        = ecosystem::prepared_file_sha256(collision / "manifest.json", &error);
    require_true(
        ecosystem::export_prepared_release_input(
            collision, *parsed.value, *selected, binary, target, digest,
            std::nullopt, &input, &error
        ) != ecosystem::command_error::ok
            && input.empty() && !fs::exists(target / "input.json"),
        "artifact/resource file-directory collision must fail, not drop "
        "resources"
    );
    const auto bad_tools = root.path() / "bad-tools";
    write_executable_script(
        bad_tools / "sha256sum",
        "#!/bin/sh\nprintf 'broken hash tool\\n'\nexit 23\n"
    );
    scoped_env bad_hash("PATH", bad_tools.string() + ":" + current_path_env());
    const auto failed_hash
        = run_marx_cli(root.path(), "prerelease --prepare-only");
    require_true(
        failed_hash.exit_code == 5, "failed mandatory hashing must block export"
    );
    require_contains(
        failed_hash.output, "exit 23", "retain native hash-tool failure status"
    );
}

void test_release_prepared_planner(const fs::path& planner) {
    require_true(
        fs::is_regular_file(planner), "installed independent planner must exist"
    );
    temp_dir root;
    const auto project = root.path() / "real product";
    write_runtime_package_project(project);
    auto authored = json::parse(read_text(project / "manifest.json"));
    authored["install_assets"] = true;
    write_text(project / "manifest.json", authored.dump(2));
    write_text(
        project / "assets/dataset/test/resource.txt",
        "actual runtime resource\n"
    );
    auto input = prepared_input(
        project, run_marx_cli(project, "prerelease --prepare-only")
    );
    check_inventory(input);
    check_planner(planner, input);

    for (const auto* selected : { "bridge:lib", "middle:lib" }) {
        input = prepared_input(
            project,
            run_marx_cli(
                project,
                std::string("prerelease ") + selected + " --prepare-only"
            )
        );
        check_inventory(input);
        check_planner(planner, input);
    }
    const auto qt = root.path() / "qt-product";
    write_text(
        qt / "manifest.json",
        R"({"id":"qt_product","description":"Prepared Qt application","facade":"app:main","artifacts":[{"id":"app:main","kind":"qt_app","owns":[],"entry":"src/main.cpp","packages":{"qt":["Core"]}}]})"
    );
    write_text(
        qt / "src/main.cpp",
        "#include <QCoreApplication>\nint main(int argc, char** argv) { "
        "QCoreApplication application(argc, argv); return 0; }\n"
    );
    input = prepared_input(qt, run_marx_cli(qt, "prerelease --prepare-only"));
    check_inventory(input);
    check_planner(planner, input);

    const auto previous
        = input.at("distribution").at("files").front().at("sha256");
    write_text(
        qt / "src/main.cpp",
        "#include <QCoreApplication>\nint main(int argc, char** argv) { "
        "QCoreApplication application(argc, argv); "
        "QCoreApplication::setApplicationName(\"reprepared fixture\"); return "
        "0; }\n"
    );
    const auto updated
        = prepared_input(qt, run_marx_cli(qt, "prerelease --prepare-only"));
    check_inventory(updated);
    check_planner(planner, updated);
    require_true(
        updated.at("distribution").at("files").front().at("sha256") != previous,
        "preparation rebuilds changed sources rather than blessing a stale "
        "binary"
    );
    std::cout << "[ok] prepared executable/companions, shared/static libraries "
                 "and real Qt application: planner exit 3; payload parity\n";
}

} // namespace ecosystem_test_support
