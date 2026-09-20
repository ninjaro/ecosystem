#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

void test_cli_check_leaks_runs_sanitized_tests() {
    temp_dir root;
    write_sample_leak_check_project(root.path());

    const fs::path copied_engels = root.path() / "bin" / "engels";
    fs::create_directories(copied_engels.parent_path());
    fs::copy_file(
        engels_binary_path(), copied_engels,
        fs::copy_options::overwrite_existing
    );
    fs::permissions(
        copied_engels,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add
    );

    // This probe deliberately copies only the actor; supply its matching data
    // explicitly.
    scoped_env templates(
        "MANIFESTO_TEMPLATE_ROOT",
        (fs::path(ECOS_TEST_SOURCE_DIR) / "templates").string()
    );
    const cli_result result
        = run_cli_with_binary(copied_engels, root.path(), "check leaks");
    if (result.exit_code == 0) {
        require_contains(
            result.output, "leak check passed",
            "ecos check leaks must report success after the sanitized test run"
        );
        return;
    }
    if (result.output.find(
            "sanitizer-backed leak checks are unavailable while "
            "the process is being traced"
        )
        != std::string::npos) {
        return;
    }
    require_contains(
        result.output, "LeakSanitizer does not work under ptrace",
        "ecos check leaks must either preflight traced environments "
        "or surface the underlying LSAN ptrace limitation"
    );
}

void test_cli_check_tidy_uses_clang_tidy_when_available() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path cmake_log = fake_root / "cmake.log";
    const fs::path clang_tidy_log = fake_root / "clang-tidy.log";
    fs::create_directories(fake_bin);
    write_fake_configure_cmake_with_compile_database(fake_bin / "cmake");
    write_fake_clang_tidy_tool(fake_bin / "clang-tidy");
    write_executable_script(fake_bin / "clang++", "#!/bin/bash\nexit 0\n");
    write_executable_script(fake_bin / "clang", "#!/bin/bash\nexit 0\n");

    scoped_env path_env("PATH", fake_bin.string() + ":" + current_path_env());
    scoped_env cmake_log_env("FAKE_CMAKE_LOG", cmake_log.string());
    scoped_env clang_tidy_log_env(
        "FAKE_CLANG_TIDY_LOG", clang_tidy_log.string()
    );

    const cli_result result = run_cli(root.path(), "check tidy");
    require_true(
        result.exit_code == 0,
        "ecos check tidy must succeed when clang-tidy is available and "
        "reports no diagnostics"
    );
    require_contains(
        read_text(clang_tidy_log), "--warnings-as-errors=*",
        "ecos check tidy must run clang-tidy with warnings promoted to "
        "errors"
    );
    require_contains(
        read_text(clang_tidy_log), "src/main.cpp",
        "ecos check tidy must pass the selected manifest-declared source "
        "files to clang-tidy"
    );

    const json report
        = json::parse(read_text(root.path() / ".ecosystem/reports/tidy.json"));
    require_true(
        report.at("clang_tidy_available").get<bool>()
            && report.at("clang_tidy_used").get<bool>()
            && report.at("compilation_database_available").get<bool>(),
        "tidy report must record clang-tidy execution when the tool and "
        "compilation database are available"
    );
    require_true(
        report.at("clang_tidy_exit_code").get<int>() == 0,
        "tidy report must record a successful clang-tidy exit code"
    );
}

void test_cli_check_tidy_fails_when_clang_tidy_reports_diagnostics() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path cmake_log = fake_root / "cmake.log";
    fs::create_directories(fake_bin);
    write_fake_configure_cmake_with_compile_database(fake_bin / "cmake");
    write_fake_clang_tidy_tool(fake_bin / "clang-tidy");
    write_executable_script(fake_bin / "clang++", "#!/bin/bash\nexit 0\n");
    write_executable_script(fake_bin / "clang", "#!/bin/bash\nexit 0\n");

    scoped_env path_env("PATH", fake_bin.string() + ":" + current_path_env());
    scoped_env cmake_log_env("FAKE_CMAKE_LOG", cmake_log.string());
    scoped_env clang_tidy_output_env(
        "FAKE_CLANG_TIDY_OUTPUT",
        "src/main.cpp:1:1: warning: fake tidy warning [fake-check]"
    );
    scoped_env clang_tidy_exit_env("FAKE_CLANG_TIDY_EXIT_CODE", "1");

    const cli_result result = run_cli(root.path(), "check tidy");
    require_true(
        result.exit_code != 0,
        "ecos check tidy must fail when clang-tidy reports diagnostics"
    );
    require_contains(
        result.output, "clang-tidy reported diagnostics",
        "ecos check tidy must surface clang-tidy failures distinctly"
    );
    require_not_contains(
        result.output, "Clang analysis reported diagnostics",
        "clang-tidy failures must not be misreported as libclang analysis "
        "failures"
    );

    const json report
        = json::parse(read_text(root.path() / ".ecosystem/reports/tidy.json"));
    require_true(
        report.at("clang_tidy_used").get<bool>()
            && report.at("clang_tidy_exit_code").get<int>() == 1,
        "tidy report must persist clang-tidy failure state"
    );
    require_true(
        !report.at("clang_tidy_output").empty(),
        "tidy report must persist clang-tidy output lines"
    );
}

void test_tidy_requires_tool_and_complete_compilation_database() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    const auto loaded = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(loaded.value.has_value(), "tidy fixture must load");
    const auto database = ecosystem::local_build_dir(root.path(), "debug")
        / "compile_commands.json";
    const auto bin = root.path() / "bin";
    fs::create_directory(bin);
    const auto inspect = [&]() {
        return ecosystem::run_tidy_check(
            *loaded.value, root.path(), std::nullopt, true, false, "debug"
        );
    };
    {
        scoped_env path("PATH", bin.string());
        const auto report = inspect();
        require_true(
            !report.clang_tidy_used && !report.clang_tidy_available,
            "missing clang-tidy must remain an unexecuted check"
        );
        require_contains(
            report.clang_tidy_skip_reason, "clang-tidy is not available",
            "missing analyzer must have an actionable reason"
        );
    }
    write_fake_clang_tidy_tool(bin / "clang-tidy");
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    auto report = inspect();
    require_true(
        !report.clang_tidy_used && !report.compilation_database_available,
        "missing database must not trigger fallback analysis"
    );
    for (const std::string contents :
         { "not JSON", "{}", "[]",
           "[{\"directory\":\"/"
           "tmp\",\"file\":\"absent.cpp\",\"command\":\"clang++ -c "
           "absent.cpp\"}]" }) {
        write_text(database, contents);
        report = inspect();
        require_true(
            !report.clang_tidy_used && !report.clang_tidy_skip_reason.empty(),
            "invalid or incomplete databases must not silently use guessed "
            "flags"
        );
    }

    write_fake_configure_and_build_cmake(bin / "cmake");
    write_executable_script(bin / "clang++", "#!/bin/sh\nexit 0\n");
    write_executable_script(bin / "clang", "#!/bin/sh\nexit 0\n");
    scoped_env cmake_log(
        "FAKE_CMAKE_LOG", (root.path() / "cmake.log").string()
    );
    fs::remove(database);
    auto result = run_engels_cli(root.path(), "check tidy");
    require_true(
        result.exit_code == 5, "CLI must fail an unexecuted required tidy check"
    );
    require_contains(
        result.output, "compile_commands.json is not available",
        "CLI must name missing configured inputs"
    );
    require_contains(
        result.output,
        "tidy report:", "failed checks must locate their persisted report"
    );

    fs::remove(bin / "clang-tidy");
    {
        scoped_env missing("PATH", bin.string());
        result = run_engels_cli(root.path(), "check tidy");
        require_true(
            result.exit_code == 4,
            "CLI must classify missing clang-tidy as missing tooling"
        );
        require_contains(
            result.output, "clang-tidy is not available",
            "missing tool must not be reported as success"
        );
    }
}

void test_cli_check_tidy_honors_full_artifact_identity_and_configuration() {
    temp_dir root;
    const auto project = root.path() / "project with spaces";
    write_text(
        project / "manifest.json",
        json(
            { { "id", "tidy_sample" },
              { "description", "Required tidy scope" },
              { "facade", "core:app" },
              { "artifacts",
                json::array(
                    { { { "id", "core:app" },
                        { "kind", "exe" },
                        { "owns", json::array() },
                        { "entry", "src/main.cpp" } },
                      { { "id", "core:other" },
                        { "kind", "exe" },
                        { "owns", json::array() },
                        { "entry", "src/other_main.cpp" } } }
                ) } }
        ).dump(2)
    );
    const std::string clean
        = "#ifndef REQUIRED_FROM_FLAGS\n#error configured compiler definition "
          "missing\n#endif\nint main() { return 0; }\n";
    write_text(project / "src/main.cpp", clean);
    write_text(
        project / "src/other_main.cpp",
        "#error unselected artifact must not be analyzed\nint main() { return "
        "0; }\n"
    );
    write_text(
        project / ".clang-tidy",
        "Checks: '-*,clang-analyzer-core.NullDereference'\n"
    );
    scoped_env flags("CXXFLAGS", "-DREQUIRED_FROM_FLAGS=1");
    auto result = run_engels_cli(project, "check tidy core:app");
    require_true(
        result.exit_code == 0,
        "native tidy must honor selected configured inputs:\n" + result.output
    );
    const auto report_path = project / ".ecosystem/reports/tidy.json";
    auto report = json::parse(read_text(report_path));
    require_true(
        report.at("clang_tidy_used").get<bool>()
            && report.at("files_analyzed") == 1,
        "both analyzers must select only the requested artifact owner"
    );
    require_true(
        report.at("sources").at(0).at("file") == "src/main.cpp",
        "report must preserve the selected source identity"
    );
    const auto before_invalid = read_text(report_path);
    result = run_engels_cli(project, "check tidy core:absent");
    require_true(
        result.exit_code == 2 && read_text(report_path) == before_invalid,
        "unknown artifacts must fail before analysis or report replacement"
    );

    write_text(
        project / "src/main.cpp",
        "int main() { int *value = nullptr; return *value; }\n"
    );
    result = run_engels_cli(project, "check tidy core:app");
    require_true(
        result.exit_code == 5,
        "actual clang-tidy diagnostics must fail the operation"
    );
    require_contains(
        result.output, "clang-analyzer-core.NullDereference",
        "terminal output must show the actual diagnostic and check"
    );
    require_contains(
        result.output, "src/main.cpp",
        "terminal output must identify the failing source"
    );
    report = json::parse(read_text(report_path));
    require_true(
        report.at("clang_tidy_exit_code").get<int>() != 0
            && !report.at("clang_tidy_output").empty(),
        "native diagnostic evidence must persist on failure"
    );

    write_text(project / "src/main.cpp", clean);
    write_text(project / ".clang-tidy", "UnknownConfigurationKey: true\n");
    result = run_engels_cli(project, "check tidy core:app");
    require_true(
        result.exit_code == 5,
        "invalid tidy configuration must fail rather than skip"
    );
    write_text(
        project / ".clang-tidy",
        "Checks: '-*,clang-analyzer-core.NullDereference'\n"
    );
    result = run_engels_cli(project, "check tidy");
    require_true(
        result.exit_code == 5,
        "whole-project tidy must still check the other artifact"
    );
    require_contains(
        result.output, "unselected artifact must not be analyzed",
        "whole-project diagnostics must retain the failing compiler message"
    );
}

void test_cli_format_shares_verifier_selection_and_tool() {
    temp_dir root;
    const auto real_formatter = ecosystem::find_command_path("clang-format");
    require_true(
        !real_formatter.empty(), "format acceptance requires clang-format"
    );
    const auto project = root.path() / "project with spaces";
    const json authored
        = { { "id", "format_sample" },
            { "description", "Format selection" },
            { "facade", "core:app" },
            { "artifacts",
              json::array(
                  { { { "id", "core:lib" },
                      { "kind", "static_lib" },
                      { "owns",
                        json::array(
                            { "math", "tests/lib_tests.cpp",
                              "benchmarks/lib_bench.cpp" }
                        ) } },
                    { { "id", "core:app" },
                      { "kind", "exe" },
                      { "owns", json::array() },
                      { "entry", "src/main.cpp" },
                      { "dependencies", json::array({ "core:lib" }) } } }
              ) } };
    write_text(project / "manifest.json", authored.dump(2));
    const std::string unformatted = "int helper(){return 2;}\n";
    const std::vector<fs::path> selected {
        project / "include/math.hpp", project / "src/math.cpp",
        project / "tests/lib_tests.cpp", project / "benchmarks/lib_bench.cpp"
    };
    for (const auto& file : selected)
        write_text(file, unformatted);
    write_text(project / "src/main.cpp", "int main(){return 0;}\n");
    write_text(project / "src/unowned.cpp", unformatted);
    write_text(project / "build/generated.cpp", unformatted);
    const std::string style = "BasedOnStyle: LLVM\nIndentWidth: 2\n";
    write_text(project / ".clang-format", style);
    write_text(
        project / "tests/.clang-format",
        "BasedOnStyle: LLVM\nIndentWidth: 4\nAllowShortFunctionsOnASingleLine: "
        "None\n"
    );
    const auto bin = root.path() / "bin";
    const auto log = root.path() / "formatter.log";
    write_executable_script(
        bin / "clang-format",
        "#!/bin/sh\n"
        "if [ \"$1\" != '--version' ]; then\n"
        "  printf 'CALL\\n' >> \"$MANIFESTO_TEST_FORMAT_LOG\"\n"
        "  printf '%s\\n' \"$@\" >> \"$MANIFESTO_TEST_FORMAT_LOG\"\n"
        "fi\n"
        "exec \"$MANIFESTO_TEST_REAL_FORMATTER\" \"$@\"\n"
    );
    scoped_env tool("MANIFESTO_TEST_REAL_FORMATTER", real_formatter);
    scoped_env output("MANIFESTO_TEST_FORMAT_LOG", log.string());
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    auto verify = run_engels_cli(project, "check format core:lib");
    require_true(verify.exit_code != 0, "format drift must fail verification");
    const auto check_call = read_text(log);
    for (const auto& file : selected)
        require_true(
            read_text(file) == unformatted,
            "verification must not write source files"
        );
    write_text(log, "");
    auto formatted = run_marx_cli(project, "format core:lib");
    require_true(
        formatted.exit_code == 0,
        "canonical formatter must succeed:\n" + formatted.output
    );
    const auto format_call = read_text(log);
    require_contains(
        check_call, "CALL\n--dry-run\n--Werror\n-style=file\n",
        "verification must retain strict formatter flags"
    );
    require_contains(
        format_call, "CALL\n-i\n-style=file\n",
        "Marx must use the same formatter configuration"
    );
    const auto config_end = std::string("-style=file\n");
    require_true(
        check_call.substr(check_call.find(config_end) + config_end.size())
            == format_call.substr(
                format_call.find(config_end) + config_end.size()
            ),
        "formatter and verifier must receive exactly the same files"
    );
    for (const auto& file : selected) {
        require_contains(
            format_call, file.string(),
            "format selection must include every owned file kind"
        );
        require_true(
            read_text(file) != unformatted,
            "selected files must actually be formatted"
        );
    }
    require_contains(
        read_text(project / "tests/lib_tests.cpp"), "    return 2;",
        "formatting must honor the selected file's nested configuration"
    );
    for (const auto& file :
         { project / "src/unowned.cpp", project / "build/generated.cpp" })
        require_true(
            read_text(file) == unformatted,
            "unowned/generated files must stay outside formatting"
        );
    require_true(
        read_text(project / "src/main.cpp") == "int main(){return 0;}\n",
        "artifact formatting must not widen to another owner or dependency"
    );
    require_true(
        read_text(project / ".clang-format") == style
            && read_text(project / "manifest.json") == authored.dump(2),
        "format must preserve authored intent and configuration"
    );
    require_true(
        run_engels_cli(project, "check format core:lib").exit_code == 0,
        "format must repair the same verifier scope"
    );
    require_true(
        run_engels_cli(project, "check format core:app").exit_code != 0,
        "unselected drift must remain visible in its own scope"
    );
    formatted = run_marx_cli(project, "--quiet format core:app");
    require_true(
        formatted.exit_code == 0 && formatted.output.empty(),
        "formatter must support the common quiet front door"
    );
    require_true(
        run_engels_cli(project, "check format").exit_code == 0,
        "whole-project verification must pass after both scopes are fixed"
    );
    const auto before_invalid = read_text(log);
    require_true(
        run_marx_cli(project, "format ghost:lib").exit_code != 0
            && run_engels_cli(project, "check format ghost:lib").exit_code != 0,
        "unknown format scopes must fail"
    );
    require_true(
        read_text(log) == before_invalid,
        "unknown scopes must fail before tool execution"
    );
    require_contains(
        run_engels_cli(project, "format").output, "available via marx",
        "format ownership must be explicit"
    );
    const auto empty_bin = root.path() / "empty-bin";
    fs::create_directory(empty_bin);
    {
        scoped_env missing("PATH", empty_bin.string());
        require_true(
            run_marx_cli(project, "format").exit_code == 4
                && run_engels_cli(project, "check format").exit_code == 4,
            "formatter and verifier must report the same missing tool"
        );
    }
    write_text(project / ".clang-format", "UnknownStyleKey: true\n");
    require_true(
        run_marx_cli(project, "format core:app").exit_code != 0
            && run_engels_cli(project, "check format core:app").exit_code != 0,
        "invalid formatter policy must fail both operations"
    );
}

void test_cli_workspace_format_respects_selected_projects_and_artifacts() {
    temp_dir root;
    write_sample_dual_run_workspace_project(root.path(), "alpha");
    write_sample_dual_run_workspace_project(root.path(), "beta");
    write_sample_dual_run_workspace_project(root.path(), "gamma");
    write_sample_workspace_config(root.path());
    const std::string unformatted = "int main(){return 0;}\n";
    for (const auto* name : { "alpha", "beta", "gamma" }) {
        write_text(
            root.path() / name / ".clang-format", "BasedOnStyle: LLVM\n"
        );
        write_text(root.path() / name / "src/main.cpp", unformatted);
        write_text(root.path() / name / "src/cli_main.cpp", unformatted);
    }
    write_text(root.path() / "broken/manifest.json", "invalid JSON\n");
    require_true(
        run_marx_cli(root.path(), "format").exit_code != 0,
        "invalid workspace state must fail before any formatting"
    );
    require_true(
        read_text(root.path() / "alpha/src/main.cpp") == unformatted,
        "workspace validity must be checked before source writes"
    );
    const auto selected
        = run_marx_cli(root.path(), "format alpha/app:app beta/tool:cli");
    require_true(
        selected.exit_code == 0,
        "qualified formatting must succeed:\n" + selected.output
    );
    require_true(
        run_engels_cli(root.path(), "check format alpha/app:app beta/tool:cli")
                .exit_code
            == 0,
        "verification must use the same multiple artifact scope"
    );
    require_true(
        read_text(root.path() / "alpha/src/cli_main.cpp") == unformatted
            && read_text(root.path() / "beta/src/main.cpp") == unformatted,
        "qualified formatting must preserve unselected artifact files"
    );
    require_true(
        run_marx_cli(root.path(), "format --group core").exit_code == 0,
        "group formatting must succeed"
    );
    require_true(
        run_engels_cli(root.path(), "check format --group core").exit_code == 0,
        "group verification must pass after formatting"
    );
    require_true(
        read_text(root.path() / "gamma/src/main.cpp") == unformatted,
        "group formatting must preserve excluded projects"
    );
    require_true(
        run_marx_cli(root.path(), "format --project gamma --project absent")
                .exit_code
            != 0,
        "invalid selectors must be preflighted as a complete request"
    );
    require_true(
        read_text(root.path() / "gamma/src/main.cpp") == unformatted,
        "invalid combined selectors must not partially format projects"
    );
    require_true(
        run_marx_cli(root.path(), "format --project gamma").exit_code == 0
            && run_engels_cli(root.path(), "check format --project gamma")
                    .exit_code
                == 0,
        "explicit project selection must share its scope with verification"
    );
}

void test_cli_check_java_builds_shared_libraries_and_runs_gradle_tests() {
    temp_dir root;
    write_sample_java_binding_project(root.path());

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path cmake_log = fake_root / "cmake.log";
    const fs::path gradle_log = fake_root / "gradle.log";
    fs::create_directories(fake_bin);

    write_executable_script(
        fake_bin / "cmake",
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "if [ \"$1\" = \"--build\" ]; then\n"
        "  build_dir=\"$2\"\n"
        "  target=\"\"\n"
        "  shift 2\n"
        "  while [ $# -gt 0 ]; do\n"
        "    if [ \"$1\" = \"--target\" ]; then\n"
        "      target=\"$2\"\n"
        "      shift 2\n"
        "      continue\n"
        "    fi\n"
        "    shift\n"
        "  done\n"
        "  mkdir -p \"$build_dir\"\n"
        "  printf '%s\\n' \"$target\" >> \"$FAKE_CMAKE_LOG\"\n"
        "  exit 0\n"
        "fi\n"
        "build_dir=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  if [ \"$1\" = \"-B\" ]; then\n"
        "    build_dir=\"$2\"\n"
        "    shift 2\n"
        "    continue\n"
        "  fi\n"
        "  shift\n"
        "done\n"
        "mkdir -p \"$build_dir\"\n"
        "exit 0\n"
    );
    write_executable_script(
        fake_bin / "gradle",
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "printf '%s\\n' \"$*\" >> \"$FAKE_GRADLE_LOG\"\n"
        "exit 0\n"
    );
    write_executable_script(fake_bin / "java", "#!/usr/bin/env bash\nexit 0\n");
    write_executable_script(
        fake_bin / "javac", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_executable_script(
        fake_bin / "clang++", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_executable_script(
        fake_bin / "clang", "#!/usr/bin/env bash\nexit 0\n"
    );

    const std::string original_path = []() {
        const char* value = std::getenv("PATH");
        return value == nullptr ? std::string() : std::string(value);
    }();
    scoped_env path_env("PATH", fake_bin.string() + ":" + original_path);
    scoped_env cmake_log_env("FAKE_CMAKE_LOG", cmake_log.string());
    scoped_env gradle_log_env("FAKE_GRADLE_LOG", gradle_log.string());

    const cli_result result = run_cli(root.path(), "check java");
    require_true(
        result.exit_code == 0,
        "ecos check java must run the Gradle-backed Java test workflow"
    );
    require_contains(
        result.output, "java tests passed",
        "ecos check java must report success after the Gradle test workflow"
    );
    require_contains(
        read_text(cmake_log), "api__api",
        "ecos check java must build the shared API library"
    );
    require_contains(
        read_text(cmake_log), "jni__jni",
        "ecos check java must build the JNI bridge library"
    );
    require_contains(
        read_text(gradle_log), "-PnativeLibraryPath=",
        "ecos check java must pass the native library path into Gradle"
    );
}

} // namespace ecosystem_test_support
