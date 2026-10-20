#include "test_cases.hpp"
#include "test_support.hpp"

namespace ecosystem_test_support {

namespace {

    void
    validate_optional_test_fixture(bool library_facade, bool multiple_owners) {
        temp_dir root;
        const auto project = root.path() / "project";
        json authored
            = { { "id", "optional_checks" },
                { "description",
                  "Optional dependency scopes and feature parity" },
                { "version", "1.2.3" },
                { "facade", library_facade ? "core:lib" : "cli:cli" },
                { "artifacts",
                  json::array({ { { "id", "core:lib" },
                                  { "kind", "static_lib" },
                                  { "owns", { "answer" } },
                                  { "packages",
                                    { { "opencv", { "optional" } },
                                      { "eigen", { "optional" } } } },
                                  { "tests", { { "gtest", true } } } },
                                { { "id", "cli:cli" },
                                  { "kind", "exe" },
                                  { "name", "optional_checks" },
                                  { "owns", { "src/main.cpp" } },
                                  { "entry", "src/main.cpp" },
                                  { "dependencies", { "core:lib" } } } }) } };
        if (multiple_owners) {
            authored["artifacts"].push_back(
                { { "id", "extra:lib" },
                  { "kind", "static_lib" },
                  { "owns", { "extra" } },
                  { "packages",
                    { { "opencv", { "optional" } },
                      { "eigen", { "optional" } } } },
                  { "tests", { { "gtest", true } } } }
            );
            authored["artifacts"][1]["dependencies"].push_back("extra:lib");
        }
        write_text(project / "manifest.json", authored.dump(2));
        for (const auto* name : { "answer", "extra" }) {
            if (std::string(name) == "extra" && !multiple_owners)
                continue;
            write_text(
                project / "include" / (std::string(name) + ".hpp"),
                "#pragma once\nconst char* " + std::string(name) + "();\n"
            );
            write_text(
                project / "src" / (std::string(name) + ".cpp"),
                "#include <" + std::string(name)
                    + ".hpp>\n#ifdef OPTIONAL_CHECKS_OPENCV\n#include "
                      "<opencv2/core.hpp>\n#endif\n"
                      "const char* "
                    + name
                    + "() {\n#ifdef OPTIONAL_CHECKS_OPENCV\n"
                      "cv::Mat value(1, 1, CV_8UC1, "
                      "cv::Scalar(7));\nif(value.at<unsigned char>(0, 0) != 7) "
                      "return \"broken\";\n"
                      "return \"present:\" "
                      "ECOSYSTEM_PROJECT_VERSION;\n#else\nreturn \"absent:\" "
                      "ECOSYSTEM_PROJECT_VERSION;\n#endif\n}\n"
            );
            write_text(
                project / "tests" / (std::string(name) + "_tests.cpp"),
                "#include <gtest/gtest.h>\n#include <cstdlib>\n#include "
                "<fstream>\n#include <string>\n#include <"
                    + std::string(name)
                    + ".hpp>\n"
                      "TEST(FeatureParity, "
                    + name
                    + ") { std::ifstream "
                      "input(std::string(ECOS_TEST_BUILD_DIR) + "
                      "\"/expected.txt\");\n"
                      "if (!input) { const std::string file = "
                      "std::string(ECOS_TEST_BUILD_DIR) + "
                      "\"/runtime-expected.txt\"; "
                      "const std::string command = \"\\\"\" + "
                      "std::string(ECOS_TEST_BUILD_DIR) + "
                      "\"/optional_checks\\\" > \\\"\" + file + \"\\\"\"; "
                      "ASSERT_EQ(std::system(command.c_str()), 0); "
                      "input.clear(); input.open(file); }\n"
                      "std::string expected; std::getline(input, expected); "
                      "ASSERT_FALSE(expected.empty()); EXPECT_EQ("
                    + name + "(), expected); }\n"
            );
        }
        write_text(
            project / "src/main.cpp",
            "#include <answer.hpp>\n#include <fstream>\n#include "
            "<iostream>\n#include <string>\n"
            "int main(int argc, char** argv) { if(argc == 1) { std::cout << "
            "answer() << '\\n'; return 0; } if(argc != 2) return 2; "
            "std::ifstream input(argv[1]); std::string expected; "
            "std::getline(input, expected); std::cout << answer() << '\\n'; "
            "return answer() == expected ? 0 : 1; }\n"
        );
        const auto loaded = ecosystem::load_manifest(project / "manifest.json");
        require_true(loaded.errors.empty(), join_lines(loaded.errors));
        require_true(
            ecosystem::has_tests_enabled(*loaded.value),
            "automatic tests must establish capability"
        );
        require_sync_success(project, "optional test fixture must sync");
        const auto developer = root.path() / "developer";
        const auto generated
            = ecosystem::generate_developer_cmakelists(*loaded.value, project);
        require_true(
            generated
                == ecosystem::generate_developer_cmakelists(
                    *loaded.value, project
                ),
            "optional test generation must be deterministic"
        );
        // Observe the generated graph without changing it. The expected runtime
        // result comes from package discovery, independently of each source's
        // preprocessor branch.
        std::string observer = R"cmake(
if(OpenCV_FOUND)
    file(WRITE "${CMAKE_BINARY_DIR}/expected.txt" "present:1.2.3\n")
else()
    file(WRITE "${CMAKE_BINARY_DIR}/expected.txt" "absent:1.2.3\n")
endif()
if(EXPECT_OPENCV AND NOT OpenCV_FOUND)
    message(FATAL_ERROR "The positive regression requires a real OpenCV installation")
endif()
foreach(support IN ITEMS ecosystem_optional_opencv ecosystem_optional_eigen)
    get_target_property(imported ${support} IMPORTED)
    if(NOT "${imported}" STREQUAL "${EXPECT_IMPORTED}")
        message(FATAL_ERROR "Incorrect local/imported support-target semantics for ${support}")
    endif()
    foreach(property IN ITEMS INTERFACE_LINK_LIBRARIES INTERFACE_INCLUDE_DIRECTORIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
        get_target_property(values ${support} ${property})
        if(values)
            set(unique "${values}")
            list(REMOVE_DUPLICATES unique)
            if(NOT "${unique}" STREQUAL "${values}")
                message(FATAL_ERROR "Duplicate ${property} on ${support}: ${values}")
            endif()
        endif()
    endforeach()
endforeach()
if(OpenCV_FOUND)
    get_target_property(links ecosystem_optional_opencv INTERFACE_LINK_LIBRARIES)
    set(expected_links "${OpenCV_LIBS}")
    list(REMOVE_DUPLICATES expected_links)
    if(NOT "${links}" STREQUAL "${expected_links}")
        message(FATAL_ERROR "OpenCV requirements were lost")
    endif()
endif()
)cmake";
        {
            const auto support_observer = observer;
            observer = "foreach(target IN ITEMS core__lib";
            if (multiple_owners)
                observer += " extra__lib";
            observer += ")\n";
            observer += R"cmake(
    set(targets ${target})
    if(ECOSYSTEM_BUILD_TESTS)
        list(APPEND targets ${target}__tests)
    endif()
    foreach(compiled IN LISTS targets)
        get_target_property(definitions ${compiled} COMPILE_DEFINITIONS)
        if(OpenCV_FOUND AND NOT "OPTIONAL_CHECKS_OPENCV" IN_LIST definitions)
            message(FATAL_ERROR "Missing available feature on ${compiled}: ${definitions}")
        elseif(NOT OpenCV_FOUND AND "OPTIONAL_CHECKS_OPENCV" IN_LIST definitions)
            message(FATAL_ERROR "Unavailable feature enabled on ${compiled}")
        endif()
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/${compiled}-definitions.txt" CONTENT "$<TARGET_PROPERTY:${compiled},COMPILE_DEFINITIONS>\n")
    endforeach()
endforeach()
)cmake";
            observer += support_observer;
        }
        write_text(developer / "CMakeLists.txt", generated + observer);
        auto run = [&](const std::vector<std::string>& args) {
            require_true(
                ecosystem::run_command(args, root.path()) == 0,
                "optional fixture command must succeed"
            );
        };
        for (const auto* profile : { "Debug", "Release" }) {
            for (const bool available : { true, false }) {
                const auto build = root.path()
                    / (std::string(profile)
                       + (available ? "-present" : "-absent"));
                run({ "cmake", "-S", developer.string(), "-B", build.string(),
                      "-G", "Ninja",
                      "-DECOSYSTEM_PROJECT_ROOT=" + project.string(),
                      "-DCMAKE_BUILD_TYPE=" + std::string(profile),
                      "-DECOSYSTEM_BUILD_TESTS=ON",
                      available ? "-DEXPECT_OPENCV=ON"
                                : "-DCMAKE_DISABLE_FIND_PACKAGE_OpenCV=TRUE",
                      std::string("-DEXPECT_IMPORTED=")
                          + (library_facade ? "TRUE" : "FALSE") });
                run({ "cmake", "--build", build.string(), "--parallel", "2" });
                run({ "ctest", "--test-dir", build.string(), "--no-tests=error",
                      "--output-on-failure" });
                run({ (build / "optional_checks").string(),
                      (build / "expected.txt").string() });
                const auto database
                    = read_text(build / "compile_commands.json");
                require_contains(
                    database, "tests/answer_tests.cpp",
                    "owned automatic test must compile"
                );
                if (multiple_owners)
                    require_contains(
                        database, "tests/extra_tests.cpp",
                        "every automatic owner must compile"
                    );
                if (available) {
                    require_contains(
                        database, "-DOPTIONAL_CHECKS_OPENCV",
                        "available feature must reach the compiler"
                    );
                } else {
                    require_not_contains(
                        database, "-DOPTIONAL_CHECKS_OPENCV",
                        "unavailable feature must not reach the compiler"
                    );
                }
                for (const auto* target : { "core__lib", "extra__lib" }) {
                    if (std::string(target) == "extra__lib" && !multiple_owners)
                        continue;
                    for (const auto& compiled :
                         { std::string(target),
                           std::string(target) + "__tests" }) {
                        const auto definitions = read_text(
                            build / (compiled + "-definitions.txt")
                        );
                        require_contains(
                            definitions, "ECOSYSTEM_PROJECT_VERSION=\"1.2.3\"",
                            "recompiled production sources must retain their "
                            "version"
                        );
                    }
                    const auto cases = run_cli_with_binary(
                        build / (std::string(target) + "__tests"), root.path(),
                        ""
                    );
                    require_true(
                        cases.exit_code == 0,
                        "available/absent feature branches must actually "
                        "execute"
                    );
                    require_contains(
                        cases.output, "[  PASSED  ] 1 test.",
                        "authored conditional behavior must pass"
                    );
                }
                const auto production = root.path()
                    / (std::string(profile)
                       + (available ? "-production-present"
                                    : "-production-absent"));
                run({ "cmake", "-S", developer.string(), "-B",
                      production.string(), "-G", "Ninja",
                      "-DECOSYSTEM_PROJECT_ROOT=" + project.string(),
                      "-DCMAKE_BUILD_TYPE=" + std::string(profile),
                      "-DECOSYSTEM_BUILD_TESTS=OFF",
                      "-DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE",
                      available ? "-DEXPECT_OPENCV=ON"
                                : "-DCMAKE_DISABLE_FIND_PACKAGE_OpenCV=TRUE",
                      std::string("-DEXPECT_IMPORTED=")
                          + (library_facade ? "TRUE" : "FALSE") });
                run({ "cmake", "--build", production.string(), "--parallel",
                      "2" });
                run({ (production / "optional_checks").string(),
                      (production / "expected.txt").string() });
                require_not_contains(
                    read_text(production / "compile_commands.json"),
                    "tests/answer_tests.cpp", "disabled tests must not compile"
                );
            }
        }
        const auto tests = run_engels_cli(project, "check tests");
        require_true(
            tests.exit_code == 0,
            "Engels must recognize automatic optional tests: " + tests.output
        );
    }

} // namespace

void test_optional_support_targets_are_reentrant() {
    validate_optional_test_fixture(false, false);
    validate_optional_test_fixture(false, true);
}

void test_automatic_tests_preserve_optional_features() {
    validate_optional_test_fixture(true, false);
    validate_optional_test_fixture(true, true);
}

void test_check_support_paths_stay_out_of_production() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_text(project / "manifest.json", R"({
        "id":"check_paths", "description":"Check support isolation",
        "facade":"core:lib", "install_artifacts":["core:lib"],
        "artifacts":[
          {"id":"core:lib", "kind":"static_lib", "owns":["include", "src", "tests"],
           "packages":{"qt":["Core"]}, "tests":{"qttest":true}},
          {"id":"bench:run", "kind":"exe", "owns":["benchmarks"],
           "entry":"benchmarks/main.cpp", "dependencies":["core:lib"]}
        ]
    })");
    write_text(
        project / "include/tests_are_data.hpp",
        "#pragma once\nint current_answer();\n"
    );
    write_text(
        project / "src/answer.cpp",
        "#include <tests_are_data.hpp>\nint current_answer() { return 42; }\n"
    );
    write_text(
        project / "tests/include/check_fixture.hpp",
        "#pragma once\n#include <QtTest>\n#include <tests_are_data.hpp>\n"
        "class check_fixture : public QObject {\nQ_OBJECT\nprivate slots:\n"
        "void current() { QCOMPARE(current_answer(), 42); }\n};\n"
    );
    write_text(
        project / "tests/check_tests.cpp",
        "#include <check_fixture.hpp>\nQTEST_APPLESS_MAIN(check_fixture)\n"
    );
    write_text(
        project / "benchmarks/include/cost_input.hpp",
        "#pragma once\ninline constexpr int expected_answer = 42;\n"
    );
    write_text(
        project / "benchmarks/main.cpp",
        "#include <cost_input.hpp>\n#include <tests_are_data.hpp>\n"
        "int main() { return current_answer() == expected_answer ? 0 : 1; }\n"
    );
    const auto loaded = ecosystem::load_manifest(project / "manifest.json");
    require_true(loaded.errors.empty(), join_lines(loaded.errors));
    const auto observer = R"cmake(
get_target_property(production_includes core__lib INCLUDE_DIRECTORIES)
foreach(path IN LISTS production_includes)
    if(path MATCHES "/(tests|benchmarks)/include")
        message(FATAL_ERROR "Check-only include leaked into production: ${path}")
    endif()
endforeach()
get_target_property(production_sources core__lib SOURCES)
foreach(path IN LISTS production_sources)
    if(path MATCHES "/(tests|benchmarks)/")
        message(FATAL_ERROR "Check-only header leaked into production: ${path}")
    endif()
endforeach()
)cmake";
    for (const bool developer : { false, true }) {
        const auto source = root.path() / (developer ? "developer" : "visitor");
        const auto generated = developer
            ? ecosystem::generate_developer_cmakelists(*loaded.value, project)
            : ecosystem::generate_cmakelists(*loaded.value, project);
        write_text(source / "CMakeLists.txt", generated + observer);
        if (!developer) {
            fs::copy(
                project / "include", source / "include",
                fs::copy_options::recursive
            );
            fs::copy(
                project / "src", source / "src", fs::copy_options::recursive
            );
            // A visitor does not even need the developer check directories.
        }
        for (const auto* profile : { "Debug", "Release" }) {
            const auto build = source / profile;
            auto run = [&](const ecosystem::string_list& command) {
                const auto result
                    = ecosystem::capture_command_result(command, root.path());
                require_true(result.exit_code == 0, result.output);
            };
            run({ "cmake", "-S", source.string(), "-B", build.string(),
                  "-DECOSYSTEM_PROJECT_ROOT=" + project.string(),
                  "-DCMAKE_BUILD_TYPE=" + std::string(profile),
                  std::string("-DECOSYSTEM_BUILD_TESTS=")
                      + (developer ? "ON" : "OFF"),
                  std::string("-DECOSYSTEM_BUILD_BENCHMARKS=")
                      + (developer ? "ON" : "OFF") });
            run({ "cmake", "--build", build.string(), "--parallel", "2" });
            if (developer) {
                run({ "ctest", "--test-dir", build.string(), "--no-tests=error",
                      "--output-on-failure" });
                run({ (build / "run").string() });
            }
        }
    }
}

void test_owned_check_artifacts_compile_and_execute_all_sources() {
    temp_dir root;
    const auto project = root.path() / "project";
    const json authored = {
        { "id", "owned_checks" },
        { "description", "Owned runnable checks" },
        { "facade", "core:lib" },
        { "artifacts",
          json::array({ { { "id", "core:lib" },
                          { "kind", "static_lib" },
                          { "owns", { "answer" } } },
                        { { "id", "suite:tests" },
                          { "kind", "exe" },
                          { "name", "owned_suite_tests" },
                          { "owns",
                            { "tests/suite", "benchmarks/test_support.cpp" } },
                          { "entry", "tests/suite/main.cpp" },
                          { "dependencies", { "core:lib" } } },
                        { { "id", "bench:bench" },
                          { "kind", "exe" },
                          { "name", "owned_benchmarks" },
                          { "owns", { "benchmarks/suite" } },
                          { "entry", "benchmarks/suite/main.cpp" } } }) }
    };
    write_text(project / "manifest.json", authored.dump(2));
    write_text(project / "include/answer.hpp", "#pragma once\nint answer();\n");
    write_text(
        project / "src/answer.cpp",
        "#include <answer.hpp>\nint answer() { return 42; }\n"
    );
    write_text(
        project / "tests/suite/main.cpp",
        "#include <iostream>\nint first(); int second(); int owned_support();\n"
        "int main() { if(first() != 42 || second() != 43 || owned_support() != "
        "44) return 1; "
        "std::cout << \"both companions executed\\n\"; return 0; }\n"
    );
    write_text(
        project / "tests/suite/first.cpp",
        "#include <answer.hpp>\nint first() { return answer(); }\n"
    );
    write_text(
        project / "tests/suite/second.cpp", "int second() { return 43; }\n"
    );
    write_text(
        project / "benchmarks/test_support.cpp",
        "int owned_support() { return 44; }\n"
    );
    write_text(
        project / "tests/unowned.cpp",
        "#error unowned tests must stay excluded\n"
    );
    write_text(
        project / "benchmarks/suite/main.cpp",
        "int benchmark_companion();\nint main() { return benchmark_companion() "
        "== 42 ? 0 : 1; }\n"
    );
    write_text(
        project / "benchmarks/suite/companion.cpp",
        "int benchmark_companion() { return 42; }\n"
    );
    const auto loaded = ecosystem::load_manifest(project / "manifest.json");
    require_true(loaded.errors.empty(), join_lines(loaded.errors));
    require_true(
        ecosystem::has_tests_enabled(*loaded.value),
        "explicit framework-free tests establish capability"
    );
    require_true(
        ecosystem::has_benchmarks_enabled(*loaded.value),
        "explicit framework-free benchmark establishes capability"
    );
    require_sync_success(project, "owned checks must sync");
    const auto developer = root.path() / "developer";
    const auto generated
        = ecosystem::generate_developer_cmakelists(*loaded.value, project);
    require_true(
        generated
            == ecosystem::generate_developer_cmakelists(*loaded.value, project),
        "check generation must be reproducible"
    );
    write_text(developer / "CMakeLists.txt", generated);
    auto run = [&](const std::vector<std::string>& args) {
        require_true(
            ecosystem::run_command(args, root.path()) == 0,
            "owned check command must succeed"
        );
    };
    for (const auto* profile : { "Debug", "Release" }) {
        const auto build = root.path() / profile;
        run({ "cmake", "-S", developer.string(), "-B", build.string(),
              "-DECOSYSTEM_PROJECT_ROOT=" + project.string(),
              "-DCMAKE_BUILD_TYPE=" + std::string(profile),
              "-DECOSYSTEM_BUILD_TESTS=ON", "-DECOSYSTEM_BUILD_BENCHMARKS=ON",
              "-DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE" });
        run({ "cmake", "--build", build.string(), "--parallel", "2" });
        run({ "ctest", "--test-dir", build.string(), "--no-tests=error",
              "--output-on-failure" });
        run({ (build / "owned_suite_tests").string() });
        run({ (build / "owned_benchmarks").string() });
        const auto database = read_text(build / "compile_commands.json");
        for (const auto* source :
             { "tests/suite/main.cpp", "tests/suite/first.cpp",
               "tests/suite/second.cpp", "benchmarks/test_support.cpp",
               "benchmarks/suite/companion.cpp" })
            require_contains(
                database, source, "every owned translation unit must compile"
            );
        require_not_contains(
            database, "tests/unowned.cpp", "unowned tests must not compile"
        );
    }
    const auto tests = run_engels_cli(project, "check tests");
    require_true(
        tests.exit_code == 0,
        "Engels must run framework-free tests: " + tests.output
    );
    const auto benchmark = run_marx_cli(project, "benchmark");
    require_true(
        benchmark.exit_code == 0,
        "Marx must run the declared framework-free benchmark: "
            + benchmark.output
    );
    const auto disabled = root.path() / "disabled";
    run({ "cmake", "-S", developer.string(), "-B", disabled.string(),
          "-DECOSYSTEM_PROJECT_ROOT=" + project.string(),
          "-DECOSYSTEM_BUILD_TESTS=OFF", "-DECOSYSTEM_BUILD_BENCHMARKS=OFF" });
    require_not_contains(
        read_text(disabled / "CTestTestfile.cmake"), "add_test(",
        "disabled check components must not register tests"
    );
}

void test_framework_dependencies_are_scoped_and_relocatable() {
    temp_dir root;
    const auto project = root.path() / "project";
    json authored = {
        { "id", "scoped_checks" },
        { "description", "Framework dependency isolation" },
        { "version", "1.2.3" },
        { "facade", "core:lib" },
        { "artifacts",
          json::array({ { { "id", "core:lib" },
                          { "kind", "static_lib" },
                          { "owns", { "answer", "tests/auto" } },
                          { "tests", { { "gtest", true } } } },
                        { { "id", "google:tests" },
                          { "kind", "exe" },
                          { "owns", { "tests/google" } },
                          { "entry", "tests/google/first.cpp" },
                          { "dependencies", { "core:lib" } },
                          { "tests", { { "gtest", true } } } },
                        { { "id", "custom:tests" },
                          { "kind", "exe" },
                          { "owns", { "tests/custom" } },
                          { "entry", "tests/custom/main.cpp" },
                          { "dependencies", { "core:lib" } },
                          { "tests",
                            { { "gtest", true }, { "gtest_main", false } } } },
                        { { "id", "qt:tests" },
                          { "kind", "exe" },
                          { "owns", { "tests/qt" } },
                          { "entry", "tests/qt/main.cpp" },
                          { "tests", { { "qttest", true } } },
                          { "packages", { { "qt", { "Core" } } } } } }) }
    };
    write_text(project / "manifest.json", authored.dump(2));
    write_text(project / "include/answer.hpp", "#pragma once\nint answer();\n");
    write_text(
        project / "src/answer.cpp",
        "#include <answer.hpp>\nint answer() { return 42; }\n"
    );
    for (const auto* directory : { "auto", "google", "custom" }) {
        for (const auto* name : { "first", "second" })
            write_text(
                project / "tests" / directory / (std::string(name) + ".cpp"),
                "#include <gtest/gtest.h>\n#include <answer.hpp>\nTEST("
                    + std::string(directory) + ", " + name
                    + ") { EXPECT_EQ(answer(), 42); }\n"
            );
    }
    write_text(
        project / "tests/custom/main.cpp",
        "#include <gtest/gtest.h>\nint main(int argc, char** argv) { "
        "::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS(); }\n"
    );
    write_text(
        project / "tests/qt/main.cpp",
        "#include <QtTest>\nint run_companion(int, char**);\n"
        "class Entry : public QObject { Q_OBJECT private slots: void first() { "
        "QCOMPARE(42, 42); } };\n"
        "int main(int argc, char** argv) { Entry entry; const int result = "
        "QTest::qExec(&entry, argc, argv); return result | run_companion(argc, "
        "argv); }\n"
        "#include \"main.moc\"\n"
    );
    write_text(
        project / "tests/qt/companion.cpp",
        "#include <QtTest>\nclass Companion : public QObject { Q_OBJECT "
        "private slots: void second() { QCOMPARE(43, 43); } };\n"
        "int run_companion(int argc, char** argv) { Companion value; return "
        "QTest::qExec(&value, argc, argv); }\n#include \"companion.moc\"\n"
    );
    const auto loaded = ecosystem::load_manifest(project / "manifest.json");
    require_true(loaded.errors.empty(), join_lines(loaded.errors));
    require_sync_success(project, "framework checks must sync");
    const auto developer = root.path() / "developer";
    const auto generated
        = ecosystem::generate_developer_cmakelists(*loaded.value, project);
    write_text(developer / "CMakeLists.txt", generated);
    auto run = [&](const std::vector<std::string>& args) {
        require_true(
            ecosystem::run_command(args, root.path()) == 0,
            "framework isolation command must succeed"
        );
    };
    for (const auto* profile : { "Debug", "Release" }) {
        const auto production
            = root.path() / (std::string(profile) + "-production");
        run({ "cmake", "-S", developer.string(), "-B", production.string(),
              "-DECOSYSTEM_PROJECT_ROOT=" + project.string(),
              "-DCMAKE_BUILD_TYPE=" + std::string(profile),
              "-DECOSYSTEM_BUILD_TESTS=OFF",
              "-DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE",
              "-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE" });
        run({ "cmake", "--build", production.string(), "--parallel", "2" });
        const auto build = root.path() / profile;
        run({ "cmake", "-S", developer.string(), "-B", build.string(),
              "-DECOSYSTEM_PROJECT_ROOT=" + project.string(),
              "-DCMAKE_BUILD_TYPE=" + std::string(profile),
              "-DECOSYSTEM_BUILD_TESTS=ON" });
        run({ "cmake", "--build", build.string(), "--parallel", "2" });
        run({ "ctest", "--test-dir", build.string(), "--no-tests=error",
              "--output-on-failure" });
        for (const auto* target :
             { "core__lib__tests", "google_tests", "custom_tests" }) {
            const auto cases
                = run_cli_with_binary(build / target, root.path(), "");
            require_true(
                cases.exit_code == 0, "all authored GoogleTest cases must run"
            );
            require_contains(
                cases.output, "[  PASSED  ] 2 tests.",
                "both GoogleTest translation units must execute"
            );
        }
        const auto qt_cases
            = run_cli_with_binary(build / "qt_tests", root.path(), "");
        require_true(
            qt_cases.exit_code == 0, "both QtTest objects must execute"
        );
        require_contains(
            qt_cases.output, "Entry::first()", "QtTest entry case must run"
        );
        require_contains(
            qt_cases.output, "Companion::second()",
            "MOC companion case must run"
        );
        const auto custom_link
            = build / "CMakeFiles/custom__tests.dir/link.txt";
        if (fs::exists(custom_link)) {
            require_contains(
                read_text(custom_link), "libgtest",
                "authored-main executable must link real GoogleTest"
            );
            require_not_contains(
                read_text(custom_link), "libgtest_main",
                "authored main must not link GoogleTest main"
            );
            require_not_contains(
                read_text(build / "CMakeFiles/core__lib.dir/link.txt"), "gtest",
                "production archive link must exclude GoogleTest"
            );
        }
        const auto prefix = root.path() / (std::string(profile) + "-installed");
        run({ "cmake", "--install", build.string(), "--prefix",
              prefix.string() });
        const auto relocated
            = root.path() / (std::string(profile) + "-relocated");
        fs::rename(prefix, relocated);
        for (const auto& file : fs::recursive_directory_iterator(relocated))
            if (file.path().extension() == ".cmake") {
                const auto contents = read_text(file.path());
                require_not_contains(
                    contents, "GTest",
                    "test-enabled installation must export no GoogleTest "
                    "requirements"
                );
                require_not_contains(
                    contents, "Qt6::Test",
                    "test-enabled installation must export no QtTest "
                    "requirements"
                );
                require_not_contains(
                    contents, project.string(),
                    "export must not depend on source paths"
                );
            }
        const auto consumer
            = root.path() / (std::string(profile) + "-consumer");
        write_text(
            consumer / "CMakeLists.txt",
            "cmake_minimum_required(VERSION 3.20)\nproject(consumer LANGUAGES "
            "CXX)\nfind_package(scoped_checks ${REQUESTED_VERSION} CONFIG "
            "REQUIRED)\n"
            "get_target_property(links scoped_checks::core__lib "
            "INTERFACE_LINK_LIBRARIES)\n"
            "if(links MATCHES \"GTest|Qt6::Test\")\nmessage(FATAL_ERROR \"test "
            "dependency leaked\")\nendif()\n"
            "add_executable(consumer main.cpp)\ntarget_link_libraries(consumer "
            "PRIVATE scoped_checks::core__lib)\n"
        );
        write_text(
            consumer / "main.cpp",
            "#include <answer.hpp>\nint main() { return answer() == 42 ? 0 : "
            "1; }\n"
        );
        run({ "cmake", "-S", consumer.string(), "-B",
              (consumer / "build").string(),
              "-DCMAKE_PREFIX_PATH=" + relocated.string(),
              "-DCMAKE_BUILD_TYPE=" + std::string(profile),
              "-DREQUESTED_VERSION=1.2.3",
              "-DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE",
              "-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE" });
        run({ "cmake", "--build", (consumer / "build").string(), "--parallel",
              "2" });
        run({ (consumer / "build/consumer").string() });
        for (const auto* incompatible : { "1.2.2", "1.2.4" }) {
            const auto rejected = ecosystem::capture_command_result(
                { "cmake", "-S", consumer.string(), "-B",
                  (consumer / (std::string("rejected-") + incompatible))
                      .string(),
                  "-DCMAKE_PREFIX_PATH=" + relocated.string(),
                  "-DREQUESTED_VERSION=" + std::string(incompatible),
                  "-DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE" }
            );
            require_true(
                rejected.exit_code != 0,
                "installed packages must reject noncurrent requested versions"
            );
            require_contains(
                rejected.output, "requested version",
                "rejection must identify the actual package-version mismatch"
            );
        }
    }
}

void test_installed_libraries_are_relocatable() {
    for (const auto* kind : { "static_lib", "shared_lib" }) {
        temp_dir root;
        const auto provider = root.path() / "provider";
        json authored = {
            { "id", "portable" },
            { "description", "Relocatable library package" },
            { "version", "1.2.3" },
            { "facade", "core:lib" },
            { "artifacts",
              json::array(
                  { { { "id", "core:headers" },
                      { "kind", "interface_lib" },
                      { "owns", { "api" } },
                      { "packages", { { "cxxopts", { "cxxopts" } } } } },
                    { { "id", "core:base" },
                      { "kind", "static_lib" },
                      { "owns", { "base" } } },
                    { { "id", "core:lib" },
                      { "kind", kind },
                      { "owns", { "wrapper" } },
                      { "dependencies", { "core:base", "core:headers" } } } }
              ) }
        };
        authored["artifacts"][1]["tests"] = { { "gtest", true } };
        if (std::string(kind) == "shared_lib") {
            authored["facade"] = "app:app";
            authored["install_artifacts"] = { "core:lib" };
            authored["artifacts"].push_back({ { "id", "app:app" },
                                              { "kind", "exe" },
                                              { "owns", json::array() },
                                              { "entry", "src/main.cpp" } });
            write_text(provider / "src/main.cpp", "int main() { return 0; }\n");
        }
        write_text(provider / "manifest.json", authored.dump(2));
        write_text(
            provider / "include/api.hpp",
            "#pragma once\n#include <span>\n#include <cxxopts.hpp>\ninline int "
            "size(std::span<int> values) { return "
            "static_cast<int>(values.size()); }\n"
        );
        write_text(
            provider / "include/base.hpp", "#pragma once\nint base();\n"
        );
        write_text(provider / "src/base.cpp", "int base() { return 41; }\n");
        write_text(
            provider / "include/wrapper.hpp",
            "#pragma once\n#include \"api.hpp\"\nint answer();\n"
            "const char* provider_version();\n"
        );
        write_text(
            provider / "src/wrapper.cpp",
            "#include \"wrapper.hpp\"\n#include \"base.hpp\"\nint answer() { "
            "return base() + 1; }\n"
            "const char* provider_version() { return "
            "ECOSYSTEM_PROJECT_VERSION; }\n"
        );
        write_text(
            provider / "include/unowned.hpp",
            "#error not part of the public package\n"
        );
        const auto loaded
            = ecosystem::load_manifest(provider / "manifest.json");
        require_true(loaded.errors.empty(), join_lines(loaded.errors));
        const auto synced = ecosystem::sync_project(provider, *loaded.value);
        require_true(synced.errors.empty(), join_lines(synced.errors));
        const auto build = root.path() / "provider-build";
        const auto prefix = root.path() / "installed";
        auto run = [&](const std::vector<std::string>& args) {
            require_true(
                ecosystem::run_command(args, root.path()) == 0,
                "independent install/consumer command must succeed"
            );
        };
        run({ "cmake", "-S", provider.string(), "-B", build.string(),
              "-DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE",
              "-DCMAKE_INSTALL_INCLUDEDIR=include/portable",
              "-DCMAKE_INSTALL_LIBDIR=lib64" });
        run({ "cmake", "--build", build.string(), "--parallel", "2" });
        run({ "cmake", "--install", build.string(), "--prefix",
              prefix.string() });
        require_true(
            !fs::exists(prefix / "include/portable/unowned.hpp"),
            "export must retain explicit header ownership"
        );
        const auto relocated = root.path() / "relocated";
        fs::rename(prefix, relocated);
        fs::rename(provider, root.path() / "hidden-source");
        fs::rename(build, root.path() / "hidden-build");
        for (const auto& file :
             fs::directory_iterator(relocated / "lib64/cmake/portable")) {
            const auto text = read_text(file.path());
            require_not_contains(
                text, "ECOSYSTEM_PROJECT_VERSION",
                "installed libraries, including interface dependencies, must "
                "not export the generic project-version macro"
            );
            require_not_contains(
                text, "GTest", "production export must not need GoogleTest"
            );
            for (const auto& forbidden : { provider, build, prefix })
                require_not_contains(
                    text, forbidden.string(),
                    "installed metadata must not retain source/build/install "
                    "locations"
                );
        }
        const auto consumer = root.path() / "consumer";
        write_text(
            consumer / "CMakeLists.txt", R"(cmake_minimum_required(VERSION 3.20)
project(consumer VERSION 9.8.7 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(portable 1.2.3 EXACT CONFIG REQUIRED)
find_package(portable 1.2.3 EXACT CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_compile_definitions(consumer PRIVATE ECOSYSTEM_PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(consumer PRIVATE portable::core__lib)
)"
        );
        write_text(
            consumer / "main.cpp",
            "#include <wrapper.hpp>\n#include <string_view>\n"
            "static_assert(std::string_view(ECOSYSTEM_PROJECT_VERSION) == "
            "\"9.8.7\");\n"
            "int main() { int data[2]{}; "
            "cxxopts::Options options(\"consumer\"); return answer() == 42 && "
            "size(data) == 2 && std::string_view(provider_version()) == "
            "\"1.2.3\" ? 0 : 1; }\n"
        );
        run({ "cmake", "-S", consumer.string(), "-B",
              (root.path() / "consumer-build").string(),
              "-DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE",
              "-Dportable_DIR="
                  + (relocated / "lib64/cmake/portable").string() });
        run({ "cmake", "--build", (root.path() / "consumer-build").string(),
              "--parallel", "2" });
        run({ (root.path() / "consumer-build/consumer").string() });
        // Current-version-only exports must reject partial/older/newer
        // requests, while the real relocated package and its public
        // dependencies work.
        for (const auto* version : { "1.2", "1.2.2", "2.0.0" }) {
            const auto incompatible = root.path() / version;
            write_text(
                incompatible / "CMakeLists.txt",
                "cmake_minimum_required(VERSION 3.20)\n"
                "project(incompatible LANGUAGES CXX)\n"
                "find_package(portable "
                    + std::string(version) + " CONFIG REQUIRED)\n"
            );
            const auto rejected = ecosystem::capture_command_result(
                { "cmake", "-S", incompatible.string(), "-B",
                  (incompatible / "build").string(),
                  "-Dportable_DIR="
                      + (relocated / "lib64/cmake/portable").string() },
                root.path()
            );
            require_true(
                rejected.exit_code != 0,
                "a noncurrent package request must be rejected"
            );
            require_contains(
                rejected.output, "version: 1.2.3",
                "rejection must identify the available current package"
            );
        }
    }

    temp_dir root;
    auto external = sample_external_project_manifest();
    external.components.back().artifacts.front().kind = "static_lib";
    external.components.back().file_units.clear();
    external.components.back().modules = { "wrapper" };
    const auto exported = ecosystem::sync_project(root.path(), external);
    require_true(
        exported.errors.empty(),
        "libraries linked to installed providers must export"
    );
    const auto cmake = read_text(root.path() / "CMakeLists.txt");
    require_contains(
        cmake, "find_dependency(packing CONFIG)",
        "consumer package metadata must rediscover the installed provider"
    );
    require_contains(
        cmake, "install(EXPORT ",
        "normal consumer package export must remain enabled"
    );
    require_not_contains(
        cmake, "IMPORTED_LOCATION",
        "installed dependencies must not expose a private build path"
    );
}

void test_install_artifacts_build_and_install_independent_executables() {
    temp_dir root;
    write_sample_dual_run_project(root.path());
    auto value = sample_dual_run_manifest();
    value.install_artifacts = { "app:app", "tool:cli" };
    std::string error;
    require_true(
        ecosystem::save_manifest(root.path() / "manifest.json", value, &error),
        error
    );
    const auto loaded = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        loaded.errors.empty()
            && loaded.value->install_artifacts == value.install_artifacts,
        "install intent must round-trip"
    );
    require_true(
        ecosystem::sync_project(root.path(), value).errors.empty(),
        "multi-artifact facade must sync"
    );
    const fs::path build = root.path() / "build";
    const fs::path prefix = root.path() / "install";
    std::string generation_error;
    require_true(
        ecosystem::ensure_local_developer_surface(
            root.path(), value, &generation_error
        ) == ecosystem::command_error::ok,
        generation_error
    );
    require_true(
        ecosystem::run_command(
            { "cmake", "-S",
              ecosystem::local_developer_source_dir(root.path()).string(), "-B",
              build.string() },
            root.path()
        ) == 0,
        "independent executable targets must configure without executable link "
        "dependencies"
    );
    require_true(
        ecosystem::run_command(
            { "cmake", "--build", build.string(), "--parallel", "2" },
            root.path()
        ) == 0,
        "both install artifacts must build"
    );
    require_true(
        ecosystem::run_command(
            { "cmake", "--install", build.string(), "--prefix",
              prefix.string() },
            root.path()
        ) == 0,
        "both install artifacts must install"
    );
    std::set<std::string> installed;
    for (const auto& entry : fs::directory_iterator(prefix / "bin")) {
        installed.insert(entry.path().filename().string());
    }
    require_true(
        installed == std::set<std::string> { "app", "cli" },
        "install must contain exactly the declared executable outputs"
    );
    const auto selected = run_marx_cli(root.path(), "build debug tool:cli");
    require_true(
        selected.exit_code == 0,
        "selected companion must build: " + selected.output
    );
    require_not_contains(
        read_text(ecosystem::local_developer_cmakelists_path(root.path())),
        "add_executable(app__app",
        "selected companion graph must exclude the facade"
    );
    const auto restored = run_marx_cli(root.path(), "build debug");
    require_true(
        restored.exit_code == 0,
        "unscoped build must restore SDK install intent: " + restored.output
    );
    const auto actor_prefix = root.path() / "actor-install";
    require_true(
        ecosystem::run_command(
            { "cmake", "--install",
              ecosystem::local_build_dir(root.path(), "debug").string(),
              "--prefix", actor_prefix.string() },
            root.path()
        ) == 0,
        "restored actor SDK must install all built declared roots"
    );
    require_true(
        fs::is_regular_file(actor_prefix / "bin/app")
            && fs::is_regular_file(actor_prefix / "bin/cli"),
        "switching selected/full graphs must preserve both declared SDK outputs"
    );
    value.install_artifacts = { "unknown:missing", "unknown:missing" };
    const auto errors = join_lines(ecosystem::validate_manifest(value));
    require_contains(
        errors, "does not resolve",
        "unresolved install intent must fail validation"
    );
    require_contains(
        errors, "duplicate", "duplicate install intent must fail validation"
    );
}

void test_visitor_facade_tracks_selected_artifact() {
    temp_dir root;
    const auto project = root.path() / "visitor project";
    write_sample_dual_run_project(project);
    write_text(
        project / "src/cli_main.cpp",
        "#include <iostream>\nint main(int argc, char** argv) {\n"
        "std::cout << \"worker\";\n"
        "for (int i = 1; i < argc; ++i) std::cout << ' ' << argv[i];\n"
        "std::cout << '\\n'; return argc > 1 ? 7 : 0; }\n"
    );
    auto value = sample_dual_run_manifest();
    value.install_artifacts = { "app:app", "tool:cli" };
    value.components[0].artifacts[0].name = "editor";
    value.components[1].artifacts[0].name = "worker";
    const auto build = project / "build";
    auto configure = [&]() {
        std::string error;
        require_true(
            ecosystem::save_manifest(project / "manifest.json", value, &error),
            error
        );
        require_true(
            ecosystem::sync_project(project, value).errors.empty(),
            "visitor surface must sync"
        );
        require_true(
            ecosystem::run_command(
                { "cmake", "-S", ".", "-B", "build" }, project
            ) == 0,
            "visitor configure must need no actor or internal artifact name"
        );
    };
    auto compile = [&]() {
        require_true(
            ecosystem::run_command({ "cmake", "--build", "build" }, project)
                == 0,
            "ordinary visitor build must materialize the facade"
        );
    };
    configure();
    compile();
    const auto cmake = read_text(project / "CMakeLists.txt");
    require_contains(
        cmake, "add_executable(app__app",
        "facade must preserve the canonical target"
    );
    require_contains(
        cmake, "OUTPUT_NAME editor",
        "facade must preserve the canonical output name"
    );
    require_not_contains(
        cmake, "add_executable(mvp", "facade must not duplicate the application"
    );
    require_true(
        fs::is_symlink(build / "mvp")
            && fs::equivalent(build / "mvp", build / "editor"),
        "mvp must point at the selected real executable"
    );
    require_true(
        !fs::read_symlink(build / "mvp").is_absolute(),
        "facade links must survive build-directory relocation"
    );
    const auto first = run_cli_with_binary(build / "mvp", project, "");
    require_true(
        first.exit_code == 0, "the three-command visitor smoke must succeed"
    );
    require_contains(
        first.output, "app-run", "only the selected executable must run"
    );

    const auto editor_time = fs::last_write_time(build / "editor");
    const auto worker_time = fs::last_write_time(build / "worker");
    value.facade_entry_artifact = "tool:cli";
    configure();
    compile();
    require_true(
        fs::equivalent(build / "mvp", build / "worker"),
        "selection changes must refresh the link without relinking"
    );
    fs::rename(build / "mvp", build / "removed-facade");
    compile();
    require_true(
        fs::is_symlink(build / "mvp"),
        "ordinary rebuild must restore a removed facade"
    );
    require_true(
        fs::last_write_time(build / "editor") == editor_time
            && fs::last_write_time(build / "worker") == worker_time,
        "facade maintenance must not rebuild unchanged executables"
    );
    const auto arguments
        = run_cli_with_binary(build / "mvp", project, "'two words'");
    require_true(
        arguments.exit_code == 7,
        "facade must preserve the real executable exit status"
    );
    require_contains(
        arguments.output, "worker two words",
        "facade must preserve application arguments"
    );
    const auto moved = project / "relocated build";
    fs::rename(build, moved);
    require_true(
        run_cli_with_binary(moved / "mvp", project, "").exit_code == 0,
        "relative facade must run after moving the build directory"
    );
    fs::rename(moved, build);

    // Switching from an alias to a real output named mvp must not leave a link
    // through which the linker can overwrite the previously selected output.
    value.components[1].artifacts[0].name = "mvp";
    configure();
    compile();
    require_true(
        !fs::is_symlink(build / "mvp"),
        "a canonical mvp output must not link to itself or overwrite its "
        "predecessor"
    );
    const auto native_time = fs::last_write_time(build / "mvp");
    configure();
    compile();
    require_true(
        fs::last_write_time(build / "mvp") == native_time,
        "reconfigure must preserve a canonical mvp executable"
    );
    require_true(
        fs::last_write_time(build / "worker") == worker_time,
        "changing facade output must preserve the previous executable"
    );

    value.components[1].artifacts[0].name = "worker";
    configure();
    compile();
    const auto api = sample_interface_library_manifest();
    value.components.push_back(api.components.front());
    value.facade_entry_artifact = api.facade_entry_artifact;
    write_text(project / "include/sample/api.hpp", "#pragma once\n");
    configure();
    compile();
    require_true(
        !fs::exists(build / "mvp") && !fs::is_symlink(build / "mvp"),
        "a library facade must retire the prior visitor link without inventing "
        "an app"
    );
}

void test_visitor_facade_handles_runtime_output_layouts() {
    for (const bool multi : { false, true }) {
        if (multi && !ecosystem::probe_tool("ninja").available)
            continue;
        temp_dir root;
        write_sample_build_project(root.path(), "sample");
        auto value = sample_build_manifest("sample");
        value.components.front().artifacts.front().name = "mvp";
        require_true(
            ecosystem::sync_project(root.path(), value).errors.empty(),
            "layout fixture must sync"
        );
        const auto build = root.path() / "build";
        std::vector<std::string> args {
            "cmake",
            "-S",
            ".",
            "-B",
            "build",
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY="
                + (build / "runtime outputs").string()
        };
        if (multi)
            args.insert(args.end(), { "-G", "Ninja Multi-Config" });
        require_true(
            ecosystem::run_command(args, root.path()) == 0,
            "custom runtime layout must configure"
        );
        for (const std::string& config : multi
                 ? std::vector<std::string> { "Debug", "Release" }
                 : std::vector<std::string> { "Debug" }) {
            require_true(
                ecosystem::run_command(
                    { "cmake", "--build", "build", "--config", config },
                    root.path()
                ) == 0,
                "configured facade must build"
            );
            const auto actual
                = build / "runtime outputs" / (multi ? config : "") / "mvp";
            require_true(
                fs::is_symlink(build / "mvp")
                    && fs::equivalent(build / "mvp", actual),
                "facade must follow the target file in the selected "
                "configuration"
            );
            require_true(
                run_cli_with_binary(build / "mvp", root.path(), "").exit_code
                    == 0,
                "custom-layout facade must run"
            );
        }
        require_true(
            ecosystem::run_command(
                { "cmake", "--build", "build", "--target", "clean" },
                root.path()
            ) == 0,
            "clean must succeed"
        );
        require_true(
            !fs::is_symlink(build / "mvp"),
            "clean must remove generated facade files"
        );
    }
}

void test_visitor_facade_rejects_conflicting_output_names() {
    for (const std::string name : { "mvp", "mvp.exe" }) {
        auto value = sample_dual_run_manifest();
        value.components[1].artifacts[0].name = name;
        const auto errors = join_lines(ecosystem::validate_manifest(value));
        require_contains(
            errors, "visitor facade output collision",
            "other artifacts must not overwrite the selected facade"
        );
        require_contains(
            errors, "tool:cli",
            "facade collision must name the conflicting artifact"
        );
        require_contains(
            errors, "app:app",
            "facade collision must identify the selected artifact"
        );
        value.facade_entry_artifact = "tool:cli";
        require_true(
            ecosystem::validate_manifest(value).empty(),
            "the selected artifact may already have the canonical mvp filename"
        );
    }
}

void test_library_facade_installs_library_target_and_headers() {
    temp_dir root;
    const ecosystem::manifest manifest_value = sample_library_manifest();
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake, "include(GNUInstallDirs)",
        "generated facade must use standard install-directory variables"
    );
    require_contains(
        generated_cmake, "install(TARGETS core__lib",
        "library-first facade must install the facade library target"
    );
    require_contains(
        generated_cmake, "ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}",
        "library-first facade must install archives under the standard lib dir"
    );
    require_contains(
        generated_cmake,
        "install(DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/include/\" "
        "DESTINATION "
        "${CMAKE_INSTALL_INCLUDEDIR})",
        "library-first facade must install its public headers"
    );
}

void test_interface_library_facade_installs_headers_without_target() {
    temp_dir root;
    const ecosystem::manifest manifest_value
        = sample_interface_library_manifest();
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake,
        "install(DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/include/\" "
        "DESTINATION "
        "${CMAKE_INSTALL_INCLUDEDIR})",
        "header-only facade must still install its public headers"
    );
    require_contains(
        generated_cmake, "install(TARGETS api__api EXPORT ",
        "header-only facade must export its interface usage requirements"
    );
}

void test_runnable_facade_skips_linked_static_library_install_surface() {
    temp_dir root;
    const ecosystem::manifest manifest_value = sample_dependency_manifest();
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake, "install(TARGETS app__app",
        "runnable facade must install its runnable entry target"
    );
    require_not_contains(
        generated_cmake, "install(TARGETS core__lib",
        "runnable facade must not install linked static libraries"
    );
    require_not_contains(
        generated_cmake,
        "install(DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/include/\" "
        "DESTINATION "
        "${CMAKE_INSTALL_INCLUDEDIR})",
        "runnable facade must not install headers for skipped static libraries"
    );
}

void test_runnable_facade_installs_linked_shared_library_closure() {
    temp_dir root;
    const ecosystem::manifest manifest_value = sample_shared_runtime_manifest();
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake, "install(TARGETS app__app",
        "runnable facade must install its runnable entry target"
    );
    require_contains(
        generated_cmake, "install(TARGETS core__lib",
        "runnable facade must install linked shared-library closure targets"
    );
    require_not_contains(
        generated_cmake, "install(TARGETS spare__helper",
        "runnable facade must not install unrelated shared libraries"
    );
}

void test_project_assets_are_staged_and_installed() {
    temp_dir root;
    std::error_code error;
    fs::create_directories(root.path() / "assets", error);
    require_true(!error, "asset fixture directory must be created");

    ecosystem::manifest manifest_value = sample_dependency_manifest();
    manifest_value.id = "sample_assets";
    manifest_value.install_assets = true;
    manifest_value.components.at(1).stack
        = json::object({ { "qt", json::array({ "Core", "Widgets" }) } });
    manifest_value.components.at(1).artifacts.front().kind = "qt_app";
    const std::string generated_cmake
        = ecosystem::generate_developer_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake, "ecosystem_stage_assets(app__app)",
        "runnable facade must stage project assets beside the build output"
    );
    require_contains(
        generated_cmake, "ecosystem_embed_assets(app__app)",
        "Qt applications must embed opted-in assets for packaged runtimes"
    );
    require_not_contains(
        generated_cmake, "ecosystem_embed_assets(benchmarks__bench)",
        "non-Qt executables must not receive Qt-generated resource objects"
    );
    require_not_contains(
        generated_cmake, "ecosystem_embed_assets(tests__tests)",
        "non-Qt test executables must not receive Qt-generated resource objects"
    );
    require_contains(
        generated_cmake,
        "DESTINATION \"${CMAKE_INSTALL_DATADIR}/sample_assets/assets",
        "facade install must preserve project assets under its data directory"
    );
    require_contains(
        generated_cmake, "qt_add_resources(${target_name}",
        "the generated Qt-only embedding helper must use qt_add_resources"
    );
    require_contains(
        generated_cmake, "PREFIX \"/sample_assets\"",
        "embedded assets must use a project-owned resource prefix"
    );
}

void test_installed_actors_use_their_template_bundle() {
    temp_dir root;
    const auto install = root.path() / "install";
    require_true(
        ecosystem::run_command(
            { "cmake", "--install", test_cli_build_dir().string(), "--prefix",
              install.string() },
            root.path()
        ) == 0,
        "tooling install must include both actors and runtime templates"
    );
    const auto prefix = root.path() / "relocated";
    fs::rename(install, prefix);
    const auto bundle = prefix / "share/ecosystem/templates";
    require_true(
        fs::is_regular_file(bundle / "tracked/.github/workflows/tests.yml.tpl"),
        "install must include hidden tracked template directories"
    );
    for (const auto* name :
         { "inputs.py", "reports.py", "benchmarks.py", "benchmark_data.py",
           "diagnostics.py", "diagnostics.js", "diagnostics.css", "assets.py",
           "readme.py", "previews.py", "carousel.css", "carousel.js" })
        require_true(
            fs::is_regular_file(bundle / "tooling/sphinx_presentation" / name),
            "installed tooling must carry its presentation companions"
        );
    const auto surface = bundle / "cmake/surface_prefix.tpl";
    write_text(surface, "# installed bundle\n" + read_text(surface));
    write_text(
        bundle / "mutation/header.hpp.tpl",
        "// installed header\n#pragma once\n"
    );
    const auto project = root.path() / "project";
    write_sample_build_project(project, "sample");
    write_text(
        root.path() / "assets/templates/cmake/surface_prefix.tpl",
        "{{wrong_workspace_version}}\n"
    );
    fs::create_directories(root.path() / "launchers");
    const auto marx = root.path() / "launchers/marx";
    fs::create_symlink(prefix / "bin/marx", marx);
    scoped_env no_override("ECOSYSTEM_TEMPLATE_ROOT", "");
    scoped_env no_legacy_override("ECOSYSTEM_TEMPLATE_ROOT", "");
    auto run = [&](const fs::path& binary, const std::string& args) {
        const auto result = run_cli_with_binary(binary, project, args);
        require_true(
            result.exit_code == 0,
            "installed command must succeed:\n" + result.output
        );
    };
    run(marx, "sync");
    require_contains(
        read_text(project / "CMakeLists.txt"), "# installed bundle",
        "installed actors must select their relocated bundle through a symlink "
        "launcher"
    );
    run(prefix / "bin/engels", "check repo");
    run(marx, "mutate add module sample:app helper");
    require_contains(
        read_text(project / "include/helper.hpp"), "installed header",
        "installed mutation must use the same bundle"
    );
    const auto overrides = root.path() / "overrides";
    write_text(
        overrides / "cmake/surface_prefix.tpl",
        "# explicit override\n" + read_text(surface)
    );
    {
        scoped_env override("ECOSYSTEM_TEMPLATE_ROOT", overrides.string());
        run(marx, "sync");
        require_contains(
            read_text(project / "CMakeLists.txt"), "# explicit override",
            "deliberate partial overrides must precede installed data"
        );
    }
    const auto previous = read_text(project / "CMakeLists.txt");
    fs::rename(surface, bundle / "cmake/surface_prefix.saved");
    auto failed = run_cli_with_binary(marx, project, "sync");
    require_true(
        failed.exit_code != 0,
        "missing bundle members must fail instead of borrowing source templates"
    );
    require_contains(
        failed.output, "cmake/surface_prefix.tpl",
        "missing template diagnostics must name the member"
    );
    require_contains(
        failed.output, bundle.string(),
        "missing template diagnostics must identify the selected bundle"
    );
    require_true(
        read_text(project / "CMakeLists.txt") == previous,
        "failed rendering must preserve tracked state"
    );
    fs::rename(bundle, prefix / "missing-bundle");
    failed = run_cli_with_binary(marx, project, "sync");
    require_true(
        failed.exit_code != 0,
        "a missing installation must not use an incidental checkout"
    );
    {
        scoped_env override(
            "ECOSYSTEM_TEMPLATE_ROOT",
            (fs::path(ECOS_TEST_SOURCE_DIR) / "assets/templates").string()
        );
        run(marx, "sync");
    }
}

void test_build_tree_layout_nests_project_and_probe_profiles() {
    temp_dir root;

    const std::vector<std::pair<std::string, std::string>> project_cases {
        { "debug", ".ecosystem/build/project/desktop/debug/default" },
        { "release", ".ecosystem/build/project/desktop/release/default" },
        { "kde", ".ecosystem/build/project/desktop/debug/kde" },
        { "coverage", ".ecosystem/build/project/desktop/debug/coverage" },
        { "leaks", ".ecosystem/build/project/desktop/debug/leaks" },
        { "android", ".ecosystem/build/project/android/debug/default" },
    };

    for (const auto& [profile, expected_relative_path] : project_cases) {
        const fs::path expected_path = root.path() / expected_relative_path;
        require_true(
            ecosystem::local_build_dir(root.path(), profile) == expected_path,
            "project build layout must map profile `" + profile
                + "` to the nested reusable tree"
        );
        require_true(
            ecosystem::local_build_cache_path(root.path(), profile)
                == expected_path / "CMakeCache.txt",
            "build cache path must stay inside the nested build tree for `"
                + profile + "`"
        );
    }

    require_true(
        ecosystem::local_doctor_dir(root.path())
            == root.path() / ".ecosystem/build/probes",
        "doctor probe scope must live under the nested build root"
    );
}

} // namespace ecosystem_test_support
