#pragma once

#include "workspace/project.hpp"

#include <string>
#include <vector>

namespace ecosystem {

struct benchmark_point {
    int n = 0;
    double gflops = 0.0;
};

struct benchmark_series {
    std::string algorithm;
    std::vector<benchmark_point> points;
};

struct benchmark_summary {
    std::vector<benchmark_series> series;
};

bool parse_benchmark_log(
    const std::string& log,
    benchmark_summary* summary,
    std::string* error_message
);
json to_json(const benchmark_summary& summary);
std::string render_benchmark_svg(
    const benchmark_summary& summary,
    const std::string& title,
    std::string* error_message
);
std::filesystem::path benchmark_output_subdir(const artifact_ref& ref);

}  // namespace ecosystem
