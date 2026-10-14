#include "../test_cases.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

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
        read_text(report_root / "summary.json"), R"("algorithm": "native")",
        "ecos benchmark must emit a machine-readable benchmark summary"
    );
    require_contains(
        read_text(report_root / "summary.json"), R"("algorithm": "transpose")",
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
    const auto presentation
        = json::parse(read_text(report_root / "presentation.json"));
    require_true(
        presentation.at("schema") == 1 && presentation.at("kind") == "flops"
            && presentation.at("artifact") == "benchmarks:bench"
            && presentation.at("files").size() == 4,
        "optional presentation must bind its owner and all four inputs"
    );
    for (const auto* name :
         { "result.json", "bench_log.txt", "summary.json", "bench_plot.svg" }) {
        const auto& record = presentation.at("files").at(name);
        require_true(
            record.at("bytes") == read_text(report_root / name).size()
                && record.at("checksum")
                       .get<std::string>()
                       .starts_with("fnv1a64:"),
            "presentation records must describe the persisted bytes"
        );
    }
}

} // namespace ecosystem_test_support
