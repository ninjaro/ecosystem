#include "workspace/benchmark.hpp"

#include <charconv>
#include <cmath>
#include <map>
#include <numeric>
#include <regex>
#include <sstream>

namespace ecosystem {
namespace benchmark_support {

    struct benchmark_samples {
        std::vector<double> means;
        std::vector<double> samples;
    };

    using sample_table
        = std::map<std::string, std::map<int, benchmark_samples>>;

    bool ends_with(const std::string& value, const std::string& suffix) {
        return value.size() >= suffix.size()
            && value.compare(
                   value.size() - suffix.size(), suffix.size(), suffix
               )
            == 0;
    }

    std::string trim_copy(const std::string& value) {
        const std::size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }
        const std::size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1U);
    }

    std::string parse_algorithm(const std::string& benchmark_name) {
        const std::size_t separator = benchmark_name.find('/');
        const std::string head = separator == std::string::npos
            ? benchmark_name
            : benchmark_name.substr(0U, separator);
        if (head == "bench_dense_matrix" && separator != std::string::npos) {
            const std::size_t next_separator
                = benchmark_name.find('/', separator + 1U);
            return benchmark_name.substr(
                separator + 1U,
                next_separator == std::string::npos
                    ? std::string::npos
                    : next_separator - separator - 1U
            );
        }
        if (head.rfind("bench_eigen", 0U) == 0U) {
            return "eigen";
        }
        return head;
    }

    std::optional<int> parse_problem_size(const std::string& benchmark_name) {
        static const std::regex size_regex("/n:(\\d+)");
        std::smatch match;
        if (!std::regex_search(benchmark_name, match, size_regex)
            || match.size() < 2U) {
            return std::nullopt;
        }
        const std::string digits = match[1].str();
        int size = 0;
        const auto parsed = std::from_chars(
            digits.data(), digits.data() + digits.size(), size
        );
        if (parsed.ec != std::errc {}
            || parsed.ptr != digits.data() + digits.size()) {
            return std::nullopt;
        }
        return size;
    }

    std::optional<double> average_samples(const std::vector<double>& values) {
        if (values.empty()) {
            return std::nullopt;
        }
        const long double total
            = std::accumulate(values.begin(), values.end(), 0.0L);
        return static_cast<double>(total / values.size());
    }

} // namespace benchmark_support

using namespace benchmark_support;

bool parse_benchmark_log(
    const std::string& log, benchmark_summary* summary,
    std::string* error_message
) {
    if (summary == nullptr) {
        if (error_message != nullptr) {
            *error_message = "benchmark summary output is null";
        }
        return false;
    }

    benchmark_summary parsed;
    sample_table samples;
    static const std::regex line_regex(R"(^(\S+).*?\bFLOPs=([\deE+\-\.]+)G/s\b)"
    );

    std::stringstream stream(log);
    std::string raw_line;
    while (std::getline(stream, raw_line)) {
        const std::string line = trim_copy(raw_line);
        if (line.empty()) {
            continue;
        }

        std::smatch match;
        if (!std::regex_search(line, match, line_regex) || match.size() < 3U) {
            continue;
        }

        std::string benchmark_name = match[1].str();
        const bool is_mean = ends_with(benchmark_name, "_mean");
        if (ends_with(benchmark_name, "_stddev")
            || ends_with(benchmark_name, "_cv")
            || ends_with(benchmark_name, "_median")) {
            continue;
        }
        if (is_mean) {
            benchmark_name
                = benchmark_name.substr(0U, benchmark_name.size() - 5U);
        }

        const std::optional<int> n = parse_problem_size(benchmark_name);
        if (!n.has_value()) {
            continue;
        }

        double gflops = 0;
        try {
            const std::string counter = match[2].str();
            std::size_t consumed = 0;
            gflops = std::stod(counter, &consumed);
            if (consumed != counter.size() || !std::isfinite(gflops)
                || gflops < 0) {
                continue;
            }
        } catch (const std::exception&) {
            continue;
        }
        benchmark_samples& bucket
            = samples[parse_algorithm(benchmark_name)][*n];
        if (is_mean) {
            bucket.means.push_back(gflops);
        } else {
            bucket.samples.push_back(gflops);
        }
    }

    for (const auto& [algorithm, by_size] : samples) {
        benchmark_series series;
        series.algorithm = algorithm;
        for (const auto& [n, values] : by_size) {
            const std::optional<double> gflops = !values.means.empty()
                ? average_samples(values.means)
                : average_samples(values.samples);
            if (!gflops.has_value()) {
                continue;
            }
            series.points.push_back(benchmark_point { n, *gflops });
        }

        if (!series.points.empty()) {
            parsed.series.push_back(std::move(series));
        }
    }

    *summary = std::move(parsed);
    if (summary->series.empty() && error_message != nullptr) {
        *error_message = "benchmark log did not contain any FLOPs series";
    }
    return true;
}

json to_json(const benchmark_summary& summary) {
    json report = json::object();
    json series = json::array();
    for (const benchmark_series& entry : summary.series) {
        json points = json::array();
        for (const benchmark_point& point : entry.points) {
            points.push_back(json::object({
                { "n", point.n },
                { "gflops", point.gflops },
            }));
        }
        series.push_back(json::object({
            { "algorithm", entry.algorithm },
            { "points", points },
        }));
    }
    report["series"] = series;
    return report;
}

} // namespace ecosystem
