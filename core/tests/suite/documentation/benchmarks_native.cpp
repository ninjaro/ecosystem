#include "../documentation.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_native_benchmarks(const fs::path& project) {
    // The documentation fixture's second library also needs a translation unit
    // when the benchmark path configures every artifact.
    write_text(
        project / "second/src/api.cpp",
        "#include \"api.hpp\"\nint unrelated_implementation() { return 1; }\n"
    );
    auto manifest = json::parse(read_text(project / "manifest.json"));
    manifest["artifacts"].push_back({ { "id", "bench:speed" },
                                      { "kind", "exe" },
                                      { "root", "perf" },
                                      { "owns", json::array() },
                                      { "entry", "benchmarks/main.cpp" } });
    write_text(project / "manifest.json", manifest.dump(2));
    write_text(project / "perf/benchmarks/main.cpp", R"cpp(#include <iostream>
#include <string>
int main(int argc, char** argv) {
    if (argc > 1) {
        if (std::string(argv[1]) == "fail") return 23;
        std::cout << "latency_ns=3\n";
    } else {
        std::cout << "bench_dense_matrix/native/n:32 FLOPs=7G/s\n"
                     "bench_dense_matrix/native/n:32_mean FLOPs=1.5G/s\n"
                     "bench_dense_matrix/native/n:64_mean FLOPs=3G/s\n"
                     "bench_dense_matrix/other/n:32_mean FLOPs=1.2G/s\n";
    }
}
)cpp");
    auto result = run_marx_cli(project, "benchmark bench:speed");
    require_true(result.exit_code == 0, result.output);
    const auto report = project / ".ecosystem/reports/benchmark/bench/speed";
    const auto original = read_text(report / "bench_plot.svg");
    result = run_engels_cli(project, "check sphinx --theme alabaster");
    require_true(result.exit_code == 0, result.output);
    const auto site = ecosystem::local_sphinx_dir(project) / "html/_ecosystem";
    const auto page = read_text(site / "index.html");
    require_contains(
        page, "GFLOPs/s versus n for bench:speed",
        "native plot must appear in themed Pages"
    );
    require_contains(
        page, "Raw plotted measurements",
        "native series must have an accessible data table"
    );
    require_contains(
        page, "<td>32</td><td>1.5</td>",
        "Pages must retain the native measurement without rounding"
    );
    for (const auto* name : { "result.json", "bench_log.txt", "summary.json",
                              "bench_plot.svg", "presentation.json" })
        require_true(
            read_text(site / "benchmarks/bench/speed" / name)
                == read_text(report / name),
            "native chart and raw downloads must preserve the bound snapshot "
            "bytes"
        );
    write_text(report / "bench_plot.svg", original + "\n");
    result = run_engels_cli(project, "check sphinx --theme alabaster");
    require_true(
        result.exit_code == 0
            && !fs::exists(site / "benchmarks/bench/speed/bench_plot.svg")
            && fs::is_regular_file(
                site / "benchmarks/bench/speed/bench_log.txt"
            ),
        "stale optional charts must retire without preventing raw benchmark "
        "presentation"
    );
    require_contains(
        read_text(site / "index.html"), "stale benchmark plot input",
        "stale plots must explain their omission"
    );
    result = run_marx_cli(project, "benchmark bench:speed -- generic");
    require_true(result.exit_code == 0, result.output);
    result = run_engels_cli(project, "check sphinx --theme alabaster");
    require_true(
        result.exit_code == 0
            && !fs::exists(site / "benchmarks/bench/speed/summary.json")
            && !fs::exists(site / "benchmarks/bench/speed/bench_plot.svg"),
        "a later generic run must retire the earlier specialized chart and "
        "table data"
    );
    require_contains(
        read_text(site / "index.html"), "latency_ns=3",
        "generic benchmark output remains readable"
    );
    result = run_marx_cli(project, "benchmark bench:speed -- fail");
    require_true(
        result.exit_code == 5,
        "real benchmark failures retain their operation status"
    );
    result = run_engels_cli(project, "check sphinx --theme alabaster");
    require_true(
        result.exit_code == 0 && !fs::exists(site / "benchmarks/bench/speed"),
        "failed runs must not publish successful benchmark artifacts"
    );
}

} // namespace ecosystem_test_support
