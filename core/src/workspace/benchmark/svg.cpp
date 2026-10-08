#include "workspace/benchmark.hpp"
#include "workspace/template_text.hpp"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>

namespace ecosystem {
namespace benchmark_support {

    std::string svg_escape(const std::string& value) {
        std::string escaped;
        escaped.reserve(value.size());
        for (const char character : value) {
            switch (character) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&apos;";
                break;
            default:
                escaped.push_back(character);
                break;
            }
        }
        return escaped;
    }

    std::string format_double(const double value, const int precision) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(precision) << value;
        return stream.str();
    }

    std::string render_required_benchmark_template(
        const std::filesystem::path& relative_path,
        const template_bindings& bindings, std::string* error_message
    ) {
        return render_text_template(relative_path, bindings, error_message);
    }

    std::string join_rendered_blocks(const std::vector<std::string>& blocks) {
        std::ostringstream rendered;
        for (const std::string& block : blocks) {
            rendered << block;
        }
        return rendered.str();
    }

    bool append_rendered_block(
        std::vector<std::string>* blocks,
        const std::filesystem::path& relative_path,
        const template_bindings& bindings, std::string* error_message
    ) {
        std::string local_error_message;
        std::string* active_error_message
            = error_message == nullptr ? &local_error_message : error_message;
        const std::string rendered = render_required_benchmark_template(
            relative_path, bindings, active_error_message
        );
        if (!active_error_message->empty()) {
            return false;
        }
        blocks->push_back(rendered);
        return true;
    }

    double x_position(
        const int n, const int min_n, const int max_n, const double left,
        const double width
    ) {
        if (max_n <= min_n) {
            return left + width / 2.0;
        }
        return left
            + (static_cast<double>(n - min_n)
               / static_cast<double>(max_n - min_n))
            * width;
    }

    double y_position(
        const double gflops, const double max_gflops, const double top,
        const double height
    ) {
        if (max_gflops <= 0.0) {
            return top + height;
        }
        return top + height - (gflops / max_gflops) * height;
    }

    std::vector<int> x_ticks(const benchmark_summary& summary) {
        std::set<int> ticks;
        for (const benchmark_series& series : summary.series) {
            for (const benchmark_point& point : series.points) {
                ticks.insert(point.n);
            }
        }

        std::vector<int> values(ticks.begin(), ticks.end());
        if (values.size() <= 8U) {
            return values;
        }

        std::vector<int> reduced;
        for (std::size_t index = 0U; index < values.size(); ++index) {
            const bool keep = index == 0U || index + 1U == values.size()
                || index % ((values.size() - 1U) / 5U + 1U) == 0U;
            if (keep) {
                reduced.push_back(values[index]);
            }
        }
        return reduced;
    }

} // namespace benchmark_support

using namespace benchmark_support;

std::string render_benchmark_svg(
    const benchmark_summary& summary, const std::string& title,
    std::string* error_message
) {
    std::string local_error_message;
    std::string* active_error_message
        = error_message == nullptr ? &local_error_message : error_message;
    active_error_message->clear();

    constexpr double svg_width = 960.0;
    constexpr double svg_height = 540.0;
    constexpr double margin_left = 80.0;
    constexpr double margin_right = 220.0;
    constexpr double margin_top = 50.0;
    constexpr double margin_bottom = 70.0;
    const double plot_width = svg_width - margin_left - margin_right;
    const double plot_height = svg_height - margin_top - margin_bottom;
    const double plot_right = margin_left + plot_width;
    const double plot_bottom = margin_top + plot_height;

    int min_n = 0;
    int max_n = 0;
    double max_gflops = 0.0;
    bool initialized = false;
    for (const benchmark_series& series : summary.series) {
        for (const benchmark_point& point : series.points) {
            if (!initialized) {
                min_n = point.n;
                max_n = point.n;
                max_gflops = point.gflops;
                initialized = true;
                continue;
            }
            min_n = std::min(min_n, point.n);
            max_n = std::max(max_n, point.n);
            max_gflops = std::max(max_gflops, point.gflops);
        }
    }
    if (!initialized) {
        max_gflops = 1.0;
    } else if (max_gflops <= 0.0) {
        max_gflops = 1.0;
    }

    const std::vector<std::string> colors {
        "#0f4c81", "#cf5c36", "#3a7d44", "#8b5fbf", "#9a3412", "#0f766e",
    };

    std::vector<std::string> y_tick_blocks;

    for (int tick = 0; tick <= 5; ++tick) {
        const double y_value = (max_gflops / 5.0) * static_cast<double>(tick);
        const double y
            = y_position(y_value, max_gflops, margin_top, plot_height);
        if (!append_rendered_block(
                &y_tick_blocks, "benchmark/y_tick.tpl",
                {
                    { "margin_left", format_double(margin_left, 2) },
                    { "y", format_double(y, 2) },
                    { "plot_right", format_double(plot_right, 2) },
                    { "label_x", format_double(margin_left - 12.0, 2) },
                    { "label_y", format_double(y + 5.0, 2) },
                    { "label", format_double(y_value, 1) },
                },
                active_error_message
            )) {
            return {};
        }
    }

    std::vector<std::string> x_tick_blocks;
    for (const int tick_n : x_ticks(summary)) {
        const double x
            = x_position(tick_n, min_n, max_n, margin_left, plot_width);
        if (!append_rendered_block(
                &x_tick_blocks, "benchmark/x_tick.tpl",
                {
                    { "x", format_double(x, 2) },
                    { "margin_top", format_double(margin_top, 2) },
                    { "plot_bottom", format_double(plot_bottom, 2) },
                    { "label_y", format_double(plot_bottom + 22.0, 2) },
                    { "label", std::to_string(tick_n) },
                },
                active_error_message
            )) {
            return {};
        }
    }

    std::vector<std::string> series_blocks;
    for (std::size_t index = 0U; index < summary.series.size(); ++index) {
        const benchmark_series& series = summary.series[index];
        const std::string& color = colors[index % colors.size()];
        std::ostringstream polyline;
        bool first = true;
        std::vector<std::string> point_blocks;
        for (const benchmark_point& point : series.points) {
            const double x
                = x_position(point.n, min_n, max_n, margin_left, plot_width);
            const double y
                = y_position(point.gflops, max_gflops, margin_top, plot_height);
            if (!first) {
                polyline << " ";
            }
            polyline << format_double(x, 2) << "," << format_double(y, 2);
            first = false;
            if (!append_rendered_block(
                    &point_blocks, "benchmark/point.tpl",
                    {
                        { "x", format_double(x, 2) },
                        { "y", format_double(y, 2) },
                        { "color", color },
                    },
                    active_error_message
                )) {
                return {};
            }
        }

        const double legend_y
            = margin_top + 20.0 + static_cast<double>(index) * 26.0;
        const double legend_x = plot_right + 24.0;
        if (!append_rendered_block(
                &series_blocks, "benchmark/series_layer.tpl",
                {
                    { "color", color },
                    { "polyline_points", polyline.str() },
                    { "point_markers", join_rendered_blocks(point_blocks) },
                    { "legend_x", format_double(legend_x, 2) },
                    { "legend_y", format_double(legend_y, 2) },
                    { "legend_line_x2", format_double(legend_x + 22.0, 2) },
                    { "legend_text_x", format_double(legend_x + 30.0, 2) },
                    { "legend_text_y", format_double(legend_y + 5.0, 2) },
                    { "algorithm", svg_escape(series.algorithm) },
                },
                active_error_message
            )) {
            return {};
        }
    }

    return render_required_benchmark_template(
        "benchmark/plot.svg.tpl",
        {
            { "svg_width", format_double(svg_width, 0) },
            { "svg_height", format_double(svg_height, 0) },
            { "margin_left", format_double(margin_left, 2) },
            { "margin_top", format_double(margin_top, 2) },
            { "plot_width", format_double(plot_width, 2) },
            { "plot_height", format_double(plot_height, 2) },
            { "title", svg_escape(title) },
            { "x_axis_label_y", format_double(svg_height - 18.0, 2) },
            { "y_axis_label_y",
              format_double(margin_top + plot_height / 2.0, 2) },
            { "y_ticks", join_rendered_blocks(y_tick_blocks) },
            { "x_ticks", join_rendered_blocks(x_tick_blocks) },
            { "series_layers", join_rendered_blocks(series_blocks) },
        },
        active_error_message
    );
}

} // namespace ecosystem
