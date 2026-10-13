#include "../ci.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_build_warning_errors() {
    temp_dir root;
    write_text(
        root.path() / "manifest.json",
        json({ { "id", "warning_sample" },
               { "description", "Required compiler warnings" },
               { "facade", "sample:app" },
               { "artifacts",
                 json::array({ { { "id", "sample:app" },
                                 { "kind", "exe" },
                                 { "owns", json::array({ "helper" }) },
                                 { "entry", "src/main.cpp" } } }) } }
        ).dump(2)
    );
    write_text(
        root.path() / "src/main.cpp",
        "int main() { int unused_value = 42; return 0; }\n"
    );
    write_text(
        root.path() / "src/helper.c", "int helper(void) { return 0; }\n"
    );
    for (const auto* profile : { "debug", "release" }) {
        const auto result
            = run_marx_cli(root.path(), std::string("build ") + profile);
        require_true(
            result.exit_code == 5,
            "compiler warnings must fail local builds:\n" + result.output
        );
        require_contains(
            result.output, "unused_value",
            "build failure must retain the compiler diagnostic"
        );
    }
    require_true(
        run_marx_cli(root.path(), "sync").exit_code == 0,
        "warning fixture must generate its visitor surface"
    );
    const auto visitor = root.path() / "visitor";
    require_true(
        ecosystem::run_command(
            { "cmake", "-S", root.path().string(), "-B", visitor.string(),
              "-DCMAKE_C_COMPILER=gcc", "-DCMAKE_CXX_COMPILER=g++" },
            root.path()
        ) == 0,
        "GNU visitor fixture must configure"
    );
    const auto build_visitor = [&]() {
        return ecosystem::capture_command_result(
            { "cmake", "--build", visitor.string(), "--parallel", "2" },
            root.path()
        );
    };
    auto visitor_result = build_visitor();
    require_true(
        visitor_result.exit_code != 0,
        "visitor builds must enforce the same compiler contract"
    );
    require_contains(
        visitor_result.output, "unused_value",
        "GNU must report its selected warning"
    );
    write_text(root.path() / "src/main.cpp", "int main() { return 0; }\n");
    require_true(
        run_marx_cli(root.path(), "build debug").exit_code == 0
            && build_visitor().exit_code == 0,
        "clean C and C++ sources must build through both compilers"
    );
    write_text(
        root.path() / "src/helper.c",
        "int helper(void) { int unused_c_value = 42; return 0; }\n"
    );
    const auto native = run_marx_cli(root.path(), "build debug");
    visitor_result = build_visitor();
    require_true(
        native.exit_code == 5 && visitor_result.exit_code != 0,
        "C warnings must be hard errors without applying C++-only options"
    );
    require_contains(
        native.output, "unused_c_value",
        "Clang must retain C diagnostic attribution"
    );
    require_contains(
        visitor_result.output, "unused_c_value",
        "GNU must retain C diagnostic attribution"
    );
}

void test_check_failure_propagation() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_sample_leak_check_project(project);
    require_true(
        run_marx_cli(project, "sync --then format").exit_code == 0,
        "aggregate check fixture must prepare canonical policy and formatting"
    );
    auto result = run_engels_cli(project, "check ci");
    require_true(
        result.exit_code == 0,
        "all required external checks must pass on clean inputs:\n"
            + result.output
    );
    require_contains(
        result.output, "ci checks passed",
        "aggregate success must follow all required stages"
    );
    const auto tests = project / "tests/main_tests.cpp";
    const auto clean_tests = read_text(tests);
    write_text(tests, "int main() { return 1; }\n");
    result = run_engels_cli(project, "check ci");
    require_true(
        result.exit_code == 5,
        "failing required tests must fail aggregate checks"
    );
    require_contains(
        result.output, "tests failed",
        "aggregate test failure must retain stage attribution"
    );
    require_not_contains(
        result.output,
        "tidy report:", "aggregate checks must stop at failed required tests"
    );
    write_text(tests, "int main() { int unused_test_value = 0; return 0; }\n");
    result = run_engels_cli(project, "check tests");
    require_true(
        result.exit_code == 5,
        "test compilation must also enforce compiler warnings"
    );
    require_contains(
        result.output, "unused_test_value",
        "test compiler errors must remain visible"
    );
    write_text(tests, clean_tests);

    const auto bin = root.path() / "bin";
    fs::create_directory(bin);
    write_fake_clang_tidy_tool(bin / "clang-tidy");
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    {
        scoped_env tidy_failure("FAKE_CLANG_TIDY_EXIT_CODE", "1");
        scoped_env tidy_output(
            "FAKE_CLANG_TIDY_OUTPUT", "required tidy fixture failure"
        );
        result = run_engels_cli(project, "check ci");
        require_true(
            result.exit_code == 5,
            "aggregate checks must propagate analyzer failure"
        );
        require_contains(
            result.output, "required tidy fixture failure",
            "aggregate failures must preserve tool output"
        );
        require_not_contains(
            result.output, "format check passed",
            "aggregate checks must stop at tidy failure"
        );
    }
    write_text(tests, "int main(){return 0;}\n");
    result = run_engels_cli(project, "check ci");
    require_true(
        result.exit_code == 5, "format drift must fail aggregate checks"
    );
    require_contains(
        result.output, "clang-format reported formatting drift",
        "format stage must remain a hard contract"
    );
    require_true(
        run_marx_cli(project, "format").exit_code == 0,
        "canonical formatting must repair predictable drift"
    );
    require_true(
        run_engels_cli(project, "check ci").exit_code == 0,
        "local repair must restore required-check success"
    );

    const auto ctest = ecosystem::find_command_path("ctest");
    scoped_env real_ctest("MANIFESTO_TEST_REAL_CTEST", ctest);
    write_executable_script(
        bin / "ctest",
        "#!/bin/sh\n"
        "if [ \"$1\" != '--version' ]; then\n"
        "  printf '# deliberately missing test registration\\n' > "
        "CTestTestfile.cmake\n"
        "fi\n"
        "exec \"$MANIFESTO_TEST_REAL_CTEST\" \"$@\"\n"
    );
    for (const auto* request :
         { "check tests", "check coverage", "check ci" }) {
        result = run_engels_cli(project, request);
        require_true(
            result.exit_code == 5,
            "no registered tests must fail required CTest execution:\n"
                + result.output
        );
        require_contains(
            result.output, "No tests were found",
            "real CTest must reject the empty selected test run"
        );
        require_not_contains(
            result.output, "ci checks passed",
            "an empty test run cannot satisfy aggregate acceptance"
        );
    }
}

void test_ci_repo_drift() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    write_text(
        root.path() / "scripts/build.sh", "#!/usr/bin/env bash\nexit 0\n"
    );

    const cli_result result = run_cli(root.path(), "check ci");
    require_true(
        result.exit_code != 0,
        "ecos check ci must fail when repository policy drift is present"
    );
    require_contains(
        result.output, "forbidden repository entries:",
        "ecos check ci must run repository policy checks before "
        "external tooling checks"
    );
    require_not_contains(
        result.output, "clang-format is not available",
        "ecos check ci must fail on repository drift before "
        "probing format tooling"
    );
    require_not_contains(
        result.output, "clang-tidy is not available",
        "ecos check ci must fail on repository drift before "
        "probing tidy tooling"
    );
}

void test_ci_optional_checks() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_sample_leak_check_project(project);
    write_text(project / "docs/index.md", "# Authored documentation\n");
    write_text(project / "docs/conf.py", "# Authored configuration\n");
    require_true(
        run_marx_cli(project, "sync --then format").exit_code == 0,
        "required-check fixture must start with canonical tracked surfaces"
    );
    const std::vector<fs::path> preserved {
        "manifest.json",
        "docs/index.md",
        "docs/conf.py",
        ".ecosystem/sphinx/html/index.html",
        ".ecosystem/doxygen/html/index.html",
        ".ecosystem/reports/coverage.json",
        ".ecosystem/reports/naming.json",
        ".ecosystem/reports/style.json",
        ".ecosystem/reports/benchmark/core/core/result.json"
    };
    std::vector<std::string> contents;
    for (const auto& relative : preserved) {
        if (!fs::exists(project / relative))
            write_text(project / relative, "previous optional output\n");
        contents.push_back(read_text(project / relative));
    }
    const auto bin = root.path() / "bin";
    const auto invocations = root.path() / "optional-invocations";
    for (const auto* name :
         { "sphinx-build", "doxygen", "dot", "llvm-cov", "llvm-profdata",
           "java", "javac", "gradle", "adb", "androiddeployqt", "makepkg",
           "repo-add", "dpkg-deb", "gpg" })
        write_executable_script(
            bin / name,
            "#!/bin/sh\n"
            "printf '%s\\n' \"$0 $*\" >> \"$MANIFESTO_TEST_OPTIONAL_LOG\"\n"
            "echo 'optional tool deliberately unavailable' >&2\n"
            "exit 97\n"
        );
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    scoped_env log("MANIFESTO_TEST_OPTIONAL_LOG", invocations.string());
    auto result = run_engels_cli(project, "check ci");
    require_true(
        result.exit_code == 0,
        "required native checks must pass without optional tools:\n"
            + result.output
    );
    require_contains(
        result.output, "ci checks passed", "required checks must finish"
    );
    require_true(
        !fs::exists(invocations), "CI must not even probe unrelated tools"
    );
    require_true(
        !fs::exists(ecosystem::local_build_dir(project, "coverage"))
            && !fs::exists(ecosystem::local_build_dir(project, "leaks")),
        "ordinary tests must not enable coverage or sanitizer profiles"
    );
    for (std::size_t i = 0; i < preserved.size(); ++i)
        require_true(
            read_text(project / preserved[i]) == contents[i],
            "required checks must preserve authored docs and optional results"
        );

    result = run_engels_cli(project, "check sphinx");
    require_true(
        result.exit_code == 4,
        "an explicitly requested dormant operation must propagate tool failure"
    );
    require_contains(
        result.output, "sphinx-build is unavailable",
        "explicit failure must name Sphinx"
    );
    require_contains(
        result.output, "optional tool deliberately unavailable",
        "explicit failure must retain the tool's explanation"
    );
    require_not_contains(
        result.output, "sphinx generated", "failed output is not success"
    );
    for (std::size_t i = 0; i < preserved.size(); ++i)
        require_true(
            read_text(project / preserved[i]) == contents[i],
            "failed dormant execution must preserve unrelated state"
        );
}

} // namespace ecosystem_test_support
