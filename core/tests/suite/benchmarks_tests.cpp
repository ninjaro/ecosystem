#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

void test_cli_benchmark_accepts_generic_programs_and_preserves_results() {
    temp_dir root;
    const json authored = { { "id", "generic_bench" },
                            { "description", "Generic benchmarks" },
                            { "facade", "app:app" },
                            { "artifacts",
                              json::array(
                                  { { { "id", "app:app" },
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
                                      { "entry", "benchmarks/second.cpp" } } }
                              ) } };
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
    const char* env = std::getenv("MANIFESTO_TEST_BENCHMARK_MODE");
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
        scoped_env env("MANIFESTO_TEST_BENCHMARK_MODE", mode);
        return run_marx_cli(root.path(), "benchmark " + args);
    };
    auto result = run("", "a_b:c -- 'two words' --then marker");
    require_true(
        result.exit_code == 0,
        "a generic benchmark must accept only user arguments:\n" + result.output
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
        !fs::exists(report / "summary.json"),
        "generic results must not require FLOPs summaries"
    );
    for (const std::string mode : { "malformed", "fail" }) {
        require_true(
            run("flops", "a_b:c").exit_code == 0
                && fs::exists(report / "bench_plot.svg"),
            "recognized counters must retain optional plots"
        );
        result = run(mode, "a_b:c");
        metadata = json::parse(read_text(report / "result.json"));
        require_true(
            !fs::exists(report / "summary.json")
                && !fs::exists(report / "bench_plot.svg"),
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
        scoped_env env("MANIFESTO_TEMPLATE_ROOT", overrides.string());
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
                && !fs::exists(report / "bench_plot.svg"),
            "failed optional reports must not leave partial results"
        );
    }
    const auto first_metadata = read_text(report / "result.json");
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
}

void test_cli_benchmark_builds_release_benchmarks_and_writes_reports() {
    temp_dir root;
    write_sample_dependency_project(root.path());

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path cmake_log = fake_root / "cmake.log";
    fs::create_directories(fake_bin);

    write_executable_script(
        fake_bin / "cmake",
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "printf '%s\\n' \"$*\" >> \"$FAKE_CMAKE_LOG\"\n"
        "if [ \"${1:-}\" = \"--version\" ]; then\n"
        "  printf 'cmake version 3.30.0\\n'\n"
        "  exit 0\n"
        "fi\n"
        "if [ \"${1:-}\" = \"--build\" ]; then\n"
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
        "  if [ -n \"$target\" ]; then\n"
        "    output_name=\"${target##*__}\"\n"
        "    cat > \"$build_dir/$output_name\" <<'EOF'\n"
        "#!/usr/bin/env bash\n"
        "cat <<'REPORT'\n"
        "bench_dense_matrix/native/n:32_mean 1 ns 1 ns 1 FLOPs=1.5G/s\n"
        "bench_dense_matrix/native/n:64_mean 1 ns 1 ns 1 FLOPs=3.0G/s\n"
        "bench_dense_matrix/transpose/n:32_mean 1 ns 1 ns 1 FLOPs=1.2G/s\n"
        "bench_dense_matrix/transpose/n:64_mean 1 ns 1 ns 1 FLOPs=2.4G/s\n"
        "REPORT\n"
        "EOF\n"
        "    chmod +x \"$build_dir/$output_name\"\n"
        "  fi\n"
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
        "if [ -n \"$build_dir\" ]; then\n"
        "  mkdir -p \"$build_dir\"\n"
        "fi\n"
        "exit 0\n"
    );
    write_executable_script(
        fake_bin / "clang++", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_executable_script(
        fake_bin / "clang", "#!/usr/bin/env bash\nexit 0\n"
    );

    scoped_env path_env("PATH", fake_bin.string() + ":" + current_path_env());
    scoped_env cmake_log_env("FAKE_CMAKE_LOG", cmake_log.string());

    const cli_result result = run_cli(root.path(), "benchmark");
    require_true(
        result.exit_code == 0,
        "ecos benchmark must succeed for a sample benchmark artifact"
    );
    require_contains(
        result.output, "benchmarked benchmarks:bench",
        "ecos benchmark must report the selected benchmark artifact"
    );
    require_contains(
        result.output,
        "benchmark plot: "
        ".ecosystem/reports/benchmark/benchmarks/bench/bench_plot.svg",
        "ecos benchmark must report the generated plot path"
    );
    require_contains(
        read_text(cmake_log), "-DCMAKE_BUILD_TYPE=Release",
        "ecos benchmark must configure CMake in Release mode"
    );
    require_contains(
        read_text(cmake_log), "-DECOSYSTEM_BUILD_BENCHMARKS=ON",
        "ecos benchmark must enable benchmark components in the "
        "developer surface"
    );

    const fs::path report_root = root.path() / ".ecosystem" / "reports"
        / "benchmark" / "benchmarks" / "bench";
    require_contains(
        read_text(report_root / "bench_log.txt"),
        "bench_dense_matrix/native/n:32_mean",
        "ecos benchmark must persist the raw benchmark log"
    );
    require_contains(
        read_text(report_root / "summary.json"), "\"algorithm\": \"native\"",
        "ecos benchmark must emit a machine-readable benchmark summary"
    );
    require_contains(
        read_text(report_root / "summary.json"), "\"algorithm\": \"transpose\"",
        "ecos benchmark must include each parsed benchmark series "
        "in the summary"
    );
    require_contains(
        read_text(report_root / "bench_plot.svg"), "<svg",
        "ecos benchmark must render an SVG plot without external "
        "plotting scripts"
    );
    require_contains(
        read_text(report_root / "bench_plot.svg"), "GFLOPs/s",
        "ecos benchmark plot must label the throughput axis"
    );
}

} // namespace ecosystem_test_support
