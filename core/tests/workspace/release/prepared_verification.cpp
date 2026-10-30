#include "../../suite/test_support.hpp"
#include "../release.hpp"
#include "workspace/prepared.hpp"

namespace ecosystem_test_support {
namespace {

    std::string shell_quote(const std::string& value) {
        std::string result = "'";
        for (const auto character : value)
            result += character == '\'' ? "'\\''" : std::string(1, character);
        return result + "'";
    }

    fs::path
    prepare(const fs::path& project, const std::string& artifact = {}) {
        const auto result = run_marx_cli(
            project, "prerelease " + artifact + " --prepare-only"
        );
        require_true(result.exit_code == 0, result.output);
        const std::string prefix = "prepared release input: ";
        const auto start = result.output.find(prefix);
        require_true(start != std::string::npos, result.output);
        return project
            / result.output.substr(
                start + prefix.size(),
                result.output.find('\n', start) - start - prefix.size()
            );
    }

    void
    command(const fs::path& project, const std::vector<std::string>& args) {
        const auto result = ecosystem::capture_command_result(args, project);
        require_true(result.exit_code == 0, result.output);
    }

    fs::path fixture(const fs::path& project, const bool with_tests = false) {
        write_runtime_package_project(project);
        auto authored = json::parse(read_text(project / "manifest.json"));
        authored["install_assets"] = true;
        if (with_tests) {
            authored["artifacts"].push_back(
                { { "id", "checks:tests" },
                  { "kind", "exe" },
                  { "owns", json::array() },
                  { "entry", "tests/check.cpp" } }
            );
            write_text(
                project / "tests/check.cpp",
                "#include <cstdlib>\nint main() { return "
                "std::getenv(\"ECOSYSTEM_PREPARED_TEST_FAIL\") != nullptr ? 1 "
                ": 0; }\n"
            );
        }
        write_text(project / "manifest.json", authored.dump(2));
        write_text(
            project / "assets/dataset/test/resource.txt", "owned resource\n"
        );
        write_text(
            project / "assets/dataset/test/.agent_cache/not-shipped.txt",
            "cache\n"
        );
        write_text(project / "assets/docs/index.md", "# Stable unit\n");
        require_true(
            run_marx_cli(project, "sync").exit_code == 0, "fixture sync"
        );
        require_true(
            run_marx_cli(project, "format").exit_code == 0, "fixture formatting"
        );
        command(project, { "git", "init", "-q" });
        command(project, { "git", "add", "." });
        command(
            project,
            { "git", "-c", "user.name=Fixture", "-c",
              "user.email=fixture@example.invalid", "commit", "-qm",
              "prepared fixture" }
        );
        return prepare(project);
    }

    const json& check(const json& report, const std::string& name) {
        for (const auto& row : report.at("checks"))
            if (row.at("name") == name)
                return row;
        throw std::runtime_error("missing verification check: " + name);
    }

    json verify(
        const fs::path& project, const fs::path& input, const int expected = 0,
        const std::string& artifact = {}
    ) {
        const auto result = run_cli(
            project,
            "check prepared " + artifact + " --input "
                + shell_quote(input.string())
        );
        require_true(result.exit_code == expected, result.output);
        const auto report = json::parse(result.output);
        require_true(
            report.at("release_ready") == false
                && report.at("publication_authorized") == false
                && report.at("all_release_gates_passed") == false,
            "local checks never authorize a release"
        );
        require_true(
            check(report, "REL-01-released-representation").at("status")
                    == "deferred"
                && check(report, "installed-product-tests").at("status")
                    == "deferred",
            "released-representation gates cannot be claimed before "
            "assembly/install"
        );
        return report;
    }

    void release_tests(const fs::path& project) {
        const auto loaded = ecosystem::load_manifest(project / "manifest.json");
        require_true(loaded.value && loaded.errors.empty(), "fixture manifest");
        write_text(
            project / ".ecosystem/source/CMakeLists.txt",
            ecosystem::generate_developer_cmakelists(*loaded.value, project)
        );
        const auto build = ecosystem::local_build_dir(project, "release");
        command(
            project,
            { "cmake", "-S", (project / ".ecosystem/source").string(), "-B",
              build.string(), "-DECOSYSTEM_PROJECT_ROOT=" + project.string(),
              "-DCMAKE_BUILD_TYPE=Release", "-DECOSYSTEM_BUILD_TESTS=ON" }
        );
        command(
            project,
            { "cmake", "--build", build.string(), "--target", "checks__tests",
              "--parallel", "2" }
        );
    }

} // namespace

void test_engels_prepared_full_product() {
    temp_dir root;
    const auto project = root.path() / "real product";
    const auto input = fixture(project);
    const auto data = read_text(input);
    const auto state = read_text(project / ".git/index");
    const auto report = verify(project, input);
    require_true(
        report.at("status") == "prepared_checks_passed", report.dump(2)
    );
    for (const auto* name :
         { "handoff-identity", "prepared-distribution", "repository", "format",
           "tidy", "final-revalidation" })
        require_true(check(report, name).at("status") == "passed", name);
    require_true(
        check(report, "native-tests").at("status") == "not_applicable",
        "no invented tests"
    );
    const auto declaration = json::parse(data);
    require_true(
        report.at("files").size()
            == declaration.at("distribution").at("files").size(),
        "all payload files are measured"
    );
    bool runtime = false;
    bool resource = false;
    for (const auto& file : report.at("files")) {
        require_true(
            file.at("sha256") == file.at("reference_sha256"),
            "normalized current reference must match"
        );
        runtime = runtime || file.at("install_path") == "usr/lib/libmiddle.so";
        resource = resource
            || file.at("install_path")
                == "usr/bin/assets/dataset/test/resource.txt";
    }
    require_true(
        runtime && resource,
        "runtime and data cannot be replaced by only the primary executable"
    );
    require_true(
        read_text(input) == data && read_text(project / ".git/index") == state
            && !fs::exists(project / ".ecosystem/prerelease/state.json"),
        "verification preserves handoff, Git index and prerelease counters"
    );
    for (const auto* artifact : { "bridge:lib", "middle:lib" }) {
        const auto selected = prepare(project, artifact);
        verify(project, selected, 0, artifact);
    }
    const auto qt = root.path() / "qt-product";
    write_text(
        qt / "manifest.json",
        R"({"id":"prepared_qt","description":"Qt fixture","facade":"app:main","artifacts":[{"id":"app:main","kind":"qt_app","owns":[],"entry":"src/main.cpp","packages":{"qt":["Core"]}}]})"
    );
    write_text(
        qt / "src/main.cpp",
        "#include <QCoreApplication>\nint main(int argc, char** argv) { "
        "QCoreApplication application(argc, argv); return 0; }\n"
    );
    require_true(
        run_marx_cli(qt, "sync").exit_code == 0
            && run_marx_cli(qt, "format").exit_code == 0,
        "Qt setup"
    );
    const auto qt_input = prepare(qt);
    require_true(
        !fs::exists(
            ecosystem::local_build_dir(qt, "debug") / "compile_commands.json"
        ),
        "Qt verification must use Release inputs without manufacturing Debug "
        "configuration"
    );
    verify(qt, qt_input);
    require_true(
        !fs::exists(
            ecosystem::local_build_dir(qt, "debug") / "compile_commands.json"
        ),
        "verification must remain configure-free"
    );
}

void test_engels_prepared_rejects_inconsistent_inputs() {
    temp_dir root;
    const auto& project = root.path();
    const auto input = fixture(project);
    const auto original = json::parse(read_text(input));
    const auto reject = [&](const json& changed) {
        write_text(input, changed.dump(2));
        const auto report = verify(project, input, 5);
        require_true(
            report.at("status") == "failed",
            "invalid input cannot look verified"
        );
        write_text(input, original.dump(2));
        return report;
    };
    for (const auto* kind :
         { "companion", "resource", "project", "artifact", "revision",
           "manifest", "complete", "passed", "unsafe", "destination" }) {
        auto changed = original;
        if (std::string(kind) == "companion")
            changed["distribution"]["artifacts"].erase(1);
        if (std::string(kind) == "resource")
            changed["distribution"]["files"].erase(0);
        if (std::string(kind) == "project")
            changed["project"]["id"] = "wrong";
        if (std::string(kind) == "artifact")
            changed["product"]["artifact"] = "tool:cli";
        if (std::string(kind) == "revision")
            changed["provenance"]["source_revision"] = std::string(40, '0');
        if (std::string(kind) == "manifest")
            changed["provenance"]["manifest_sha256"] = std::string(64, '0');
        if (std::string(kind) == "complete")
            changed["distribution"]["complete"] = false;
        if (std::string(kind) == "passed")
            changed["passed"] = true;
        if (std::string(kind) == "unsafe")
            changed["distribution"]["files"][0]["source"] = "../outside";
        if (std::string(kind) == "destination")
            changed["distribution"]["files"][0]["install_path"]
                = "usr/bin/wrong";
        reject(changed);
    }
    const auto file = project
        / original.at("distribution")
              .at("files")
              .at(0)
              .at("source")
              .get<std::string>();
    const auto saved = read_text(file);
    write_text(file, saved + "altered");
    reject(original);
    auto forged = original;
    std::string error;
    forged["distribution"]["files"][0]["size"] = fs::file_size(file);
    forged["distribution"]["files"][0]["sha256"]
        = ecosystem::prepared_file_sha256(file, &error);
    reject(forged);
    write_text(file, saved);
    fs::rename(file, file.string() + ".missing");
    reject(original);
    fs::rename(file.string() + ".missing", file);
    fs::remove(file);
    fs::create_symlink(project / "assets/dataset/test/resource.txt", file);
    reject(original);
    fs::remove(file);
    write_text(file, saved);
    const auto mode = fs::status(file).permissions();
    fs::permissions(file, fs::perms::none);
    reject(original);
    fs::permissions(file, mode);
    const auto manifest = read_text(project / "manifest.json");
    write_text(project / "manifest.json", manifest + "\n");
    reject(original);
    write_text(project / "manifest.json", manifest);
    command(
        project,
        { "git", "-c", "user.name=Fixture", "-c",
          "user.email=fixture@example.invalid", "commit", "--allow-empty",
          "-qm", "new revision" }
    );
    reject(original);
    require_true(
        run_cli(project, "check prepared").exit_code == 2
            && run_cli(project, "check repo --input nope").exit_code == 2,
        "input is mandatory and exclusive to prepared checking"
    );
}

void test_engels_prepared_gates_and_no_build() {
    temp_dir root;
    const auto& project = root.path();
    const auto input = fixture(project, true);
    const auto incomplete = verify(project, input, 3);
    require_true(
        check(incomplete, "native-tests").at("status") == "not_run",
        "missing Release tests remain visibly incomplete"
    );
    release_tests(project);
    const auto passed = verify(project, input);
    require_true(
        check(passed, "native-tests").at("tests").size() == 1,
        "real declared Release test ran"
    );
    const auto ctest_file = ecosystem::local_build_dir(project, "release")
        / "CTestTestfile.cmake";
    const auto ctest_original = read_text(ctest_file);
    write_text(
        ctest_file,
        ctest_original
            + "\nset_tests_properties(checks__tests PROPERTIES "
              "SKIP_RETURN_CODE 1)\n"
    );
    {
        scoped_env skipped("ECOSYSTEM_PREPARED_TEST_FAIL", "yes");
        const auto unknown = verify(project, input, 3);
        require_true(
            check(unknown, "native-tests").at("status") == "not_run",
            "a skipped required test must not become a pass"
        );
    }
    write_text(ctest_file, ctest_original);
    {
        scoped_env failing("ECOSYSTEM_PREPARED_TEST_FAIL", "yes");
        const auto failed = verify(project, input, 5);
        require_true(
            check(failed, "native-tests").at("status") == "failed",
            "actual required test failures remain failures"
        );
    }
    auto declared = json::parse(read_text(input));
    declared["provenance"].erase("source_revision");
    declared["provenance"]["evidence"] = ".ecosystem/alleged-evidence.json";
    write_text(
        project / ".ecosystem/alleged-evidence.json",
        R"({"passed":true,"release_ready":true})"
    );
    write_text(input, declared.dump(2));
    const auto cmake = read_text(project / "CMakeLists.txt");
    write_text(project / "CMakeLists.txt", cmake + "# stale\n");
    const auto failed = verify(project, input, 5);
    require_true(
        check(failed, "repository").at("status") == "failed",
        "a passed field cannot replace actual repository checking"
    );
    write_text(project / "CMakeLists.txt", cmake);
    const auto tools = project / ".ecosystem/guard-tools";
    const auto calls = tools / "forbidden.log";
    for (const auto* name : { "cmake", "marx", "tar", "ar", "gzip", "gpg" }) {
        const auto native = ecosystem::find_command_path(name);
        write_executable_script(
            tools / name,
            "#!/bin/sh\ncase \"$1\" in\n-P) exec " + shell_quote(native)
                + " \"$@\" ;;\n*) printf 'forbidden\\n' >> "
                + shell_quote(calls.string()) + "; exit 98 ;;\nesac\n"
        );
    }
    {
        scoped_env path("PATH", tools.string() + ":" + current_path_env());
        verify(project, input);
    }
    require_true(
        !fs::exists(calls),
        "verifier must not configure, build, invoke Marx or package/sign"
    );
}

void test_engels_prepared_planner(const fs::path& planner) {
    temp_dir root;
    const auto& project = root.path();
    const auto input = fixture(project);
    const auto report = project / ".ecosystem/engels-result.json";
    write_text(report, "{}");
    auto document = json::parse(read_text(input));
    document["provenance"]["evidence"] = ".ecosystem/engels-result.json";
    write_text(input, document.dump(2));
    const auto verified = verify(project, input);
    write_text(report, verified.dump(2));
    const auto result = ecosystem::capture_command_result(
        { planner.string(), "plan", "--input", input.string() }, project,
        { { "PATH", "" } }
    );
    require_true(result.exit_code == 3, result.output);
    const auto plan = json::parse(result.output);
    require_true(
        plan.at("plan_valid") == true && plan.at("release_ready") == false
            && plan.at("evidence_status") == "referenced_unverified",
        "even real Engels observations do not authorize the independent planner"
    );
}

void test_engels_prepared_evidence(const fs::path& directory) {
    require_true(
        !fs::exists(directory), "use a fresh diagnostic fixture directory"
    );
    const auto project = directory / "product";
    const auto input = fixture(project, true);
    release_tests(project);
    const auto result = verify(project, input);
    write_text(directory / "verification.json", result.dump(2));
    write_text(
        directory / "paths.json",
        json { { "project", project.string() }, { "input", input.string() } }
            .dump(2)
    );
    std::cout << "[ok] prepared fixture: " << input << '\n';
}

} // namespace ecosystem_test_support
