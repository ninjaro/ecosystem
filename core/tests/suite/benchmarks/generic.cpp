#include "../test_cases.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_cli_benchmark_accepts_generic_programs_and_preserves_results() {
    temp_dir root;
    json authored
        = { { "id", "generic_bench" },
            { "description", "Generic benchmarks" },
            { "facade", "app:app" },
            { "artifacts",
              json::array({ { { "id", "app:app" },
                              { "kind", "exe" },
                              { "owns", json::array() },
                              { "entry", "src/main.cpp" } },
                            { { "id", "a_b:c" },
                              { "kind", "exe" },
                              { "name", "first" },
                              { "owns", json::array() },
                              { "entry", "benchmarks/first.cpp" } },
                            { { "id", "a:b_c" },
                              { "kind", "exe" },
                              { "name", "second" },
                              { "owns", json::array() },
                              { "entry", "benchmarks/second.cpp" } } }) } };
    authored["artifacts"][0]["packages"]["qt"] = json::array({ "Core" });
    const auto missing = (root.path() / "unused-provider").string();
    authored["artifacts"].push_back(
        { { "id", "unused:provider" },
          { "kind", "static_lib" },
          { "owns", json::array() },
          { "packages",
            { { "external_project",
                { { "repository", missing },
                  { "revision", "main" },
                  { "package", "unused" },
                  { "artifact", "core:lib" } } } } } }
    );
    const auto tools = root.path() / "tools";
    const auto real_cmake = ecosystem::find_command_path("cmake");
    write_executable_script(
        tools / "cmake",
        "#!/bin/sh\nif [ \"$1\" = -S ]; then\n"
        "exec \"$ECOSYSTEM_TEST_REAL_CMAKE\" \"$@\" "
        "-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON\n"
        "fi\nexec \"$ECOSYSTEM_TEST_REAL_CMAKE\" \"$@\"\n"
    );
    scoped_env cmake("ECOSYSTEM_TEST_REAL_CMAKE", real_cmake);
    scoped_env path("PATH", tools.string() + ":" + current_path_env());
    write_text(root.path() / "manifest.json", authored.dump(2));
    write_text(root.path() / "src/main.cpp", "int main() { return 0; }\n");
    const std::string source = R"cpp(#include <cstdlib>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]).starts_with("--benchmark_")) return 91;
        std::cout << '[' << argv[i] << "]\n";
    }
    const char* env = std::getenv("ECOSYSTEM_TEST_BENCHMARK_MODE");
    const std::string mode = env ? env : "";
    if (mode == "flops") std::cout << "generic/n:32_mean 1 ns FLOPs=1.5G/s\n";
    else if (mode == "malformed") std::cout
        << "generic/n:999999999999999999999_mean FLOPs=2G/s\n"
        << "generic/n:32_mean FLOPs=1e9999G/s\n"
        << "generic/n:64_mean FLOPs=1e+G/s\n"
        << "generic/n:128_mean FLOPs=-3G/s\n";
    else if (mode == "fail") { std::cerr << "intentional benchmark failure\n"; return 23; }
    else std::cout << "latency_ns=3\n";
    return 0;
}
)cpp";
    write_text(root.path() / "benchmarks/first.cpp", source);
    write_text(root.path() / "benchmarks/second.cpp", source);
    const auto reports = root.path() / ".ecosystem/reports/benchmark";
    const auto report = reports / "a_b/c";
    auto run = [&](const std::string& mode, const std::string& args) {
        scoped_env env("ECOSYSTEM_TEST_BENCHMARK_MODE", mode);
        return run_marx_cli(root.path(), "benchmark " + args);
    };
    auto result = run("", "a_b:c -- 'two words' --then marker");
    require_true(
        result.exit_code == 0,
        "a generic benchmark must accept only user arguments:\n" + result.output
    );
    const auto generated
        = read_text(root.path() / ".ecosystem/source/CMakeLists.txt");
    require_not_contains(
        generated, "find_package(Qt6",
        "benchmark must not configure the unrelated Qt facade"
    );
    require_not_contains(
        generated, "find_package(unused",
        "benchmark must not acquire an unselected provider"
    );
    require_not_contains(
        generated, "add_executable(a__b_c",
        "benchmark must not configure another benchmark executable"
    );
    require_contains(
        read_text(report / "bench_log.txt"), "[two words]\n[--then]\n[marker]",
        "benchmark argv must preserve spaces and batch separators"
    );
    auto metadata = json::parse(read_text(report / "result.json"));
    require_true(
        metadata.at("artifact") == "a_b:c" && metadata.at("status") == "passed"
            && metadata.at("exit_code") == 0,
        "generic results must record artifact and process status"
    );
    const auto argv = metadata.at("command").get<std::vector<std::string>>();
    require_true(
        argv.size() == 4 && argv[1] == "two words" && argv[2] == "--then"
            && argv[3] == "marker",
        "result metadata must preserve the exact command"
    );
    require_true(
        !fs::exists(report / "summary.json")
            && !fs::exists(report / "presentation.json"),
        "generic results must not require FLOPs summaries"
    );
    for (const std::string mode : { "malformed", "fail" }) {
        require_true(
            run("flops", "a_b:c").exit_code == 0
                && fs::exists(report / "bench_plot.svg")
                && fs::exists(report / "presentation.json"),
            "recognized counters must retain optional plots"
        );
        result = run(mode, "a_b:c");
        metadata = json::parse(read_text(report / "result.json"));
        require_true(
            !fs::exists(report / "summary.json")
                && !fs::exists(report / "bench_plot.svg")
                && !fs::exists(report / "presentation.json"),
            "new executions must retire stale optional reports"
        );
        if (mode == "malformed") {
            require_true(
                result.exit_code == 0 && metadata.at("status") == "passed",
                "malformed optional counters must not fail successful execution"
            );
        } else {
            require_true(
                result.exit_code != 0 && metadata.at("status") == "failed"
                    && metadata.at("exit_code") == 23,
                "process failures must be preserved in generic results"
            );
            require_contains(
                result.output, "a_b:c (exit 23)",
                "execution failure must identify the artifact and exit status"
            );
            require_contains(
                result.output, "benchmark log:",
                "execution failure must expose its captured log path"
            );
            require_contains(
                read_text(report / "bench_log.txt"),
                "intentional benchmark failure",
                "failed process output must remain available"
            );
        }
    }
    const auto overrides = root.path() / "templates";
    write_text(
        overrides / "benchmark/plot.svg.tpl", "{{missing_optional_binding}}\n"
    );
    {
        scoped_env env("ECOSYSTEM_TEMPLATE_ROOT", overrides.string());
        result = run("flops", "a_b:c");
        require_true(
            result.exit_code == 0,
            "optional plot errors must not fail benchmark execution"
        );
        require_contains(
            result.output, "warning: optional benchmark report skipped:",
            "optional report failures must be visible"
        );
        require_contains(
            result.output, "benchmark/plot.svg.tpl",
            "optional report warning must name its template"
        );
        require_true(
            !fs::exists(report / "summary.json")
                && !fs::exists(report / "bench_plot.svg")
                && !fs::exists(report / "presentation.json"),
            "failed optional reports must not leave partial results"
        );
    }
    const auto first_metadata = read_text(report / "result.json");
    write_text(report / "presentation.json/obstruction", "keep\n");
    result = run("flops", "a_b:c");
    require_true(
        result.exit_code == 0
            && read_text(report / "presentation.json/obstruction") == "keep\n"
            && !fs::exists(report / "summary.json")
            && !fs::exists(report / "bench_plot.svg"),
        "presentation cleanup failures preserve core results and unexpected "
        "directories"
    );
    fs::remove_all(report / "presentation.json");
    require_true(
        run("", "a:b_c").exit_code == 0, "second generic benchmark must run"
    );
    require_true(
        read_text(report / "result.json") == first_metadata
            && fs::exists(reports / "a/b_c/result.json"),
        "artifact identities with underscores must keep separate results"
    );
    write_text(report / "bench_plot.svg/obstruction", "keep\n");
    result = run("flops", "a_b:c");
    require_true(
        result.exit_code == 0,
        "optional cleanup errors must not block execution"
    );
    require_contains(
        result.output, "warning: unable to retire optional benchmark report",
        "optional cleanup failures must be visible"
    );
    require_true(
        read_text(report / "bench_plot.svg/obstruction") == "keep\n",
        "optional cleanup must not recursively remove an unexpected directory"
    );
    fs::rename(report / "result.json", report / "previous-result.json");
    fs::create_directory(report / "result.json");
    result = run("", "a_b:c");
    require_true(
        result.exit_code != 0,
        "core result persistence errors must fail the operation"
    );
    require_contains(
        result.output, "result.json",
        "result-write errors must identify the path"
    );
    require_true(
        run("", "").exit_code != 0, "ambiguous benchmark selection must fail"
    );
    require_true(
        run("", "app:app").exit_code != 0,
        "ordinary applications must not silently become benchmarks"
    );
    authored["artifacts"][1]["dependencies"]
        = json::array({ "unused:provider" });
    write_text(root.path() / "manifest.json", authored.dump(2));
    const auto selected_missing = run("", "a_b:c");
    require_true(
        selected_missing.exit_code == 5,
        "a required benchmark provider must not be silently omitted"
    );
    require_contains(
        selected_missing.output, missing,
        "required benchmark acquisition must report its source failure"
    );
}

} // namespace ecosystem_test_support
