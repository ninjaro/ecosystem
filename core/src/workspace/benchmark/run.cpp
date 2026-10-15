#include "../command_internal.hpp"
#include "workspace/write_commands.hpp"

#include <cstdint>
#include <iomanip>

namespace ecosystem {

namespace {

    /// A local change checksum, without benchmark or CI authentication
    /// semantics.
    json file_stamp(const std::string& contents) {
        std::uint64_t checksum = 14695981039346656037ULL;
        for (const char value : contents) {
            checksum ^= static_cast<unsigned char>(value);
            checksum *= 1099511628211ULL;
        }
        std::ostringstream text;
        text << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16)
             << checksum;
        return { { "bytes", contents.size() }, { "checksum", text.str() } };
    }

} // namespace

command_error run_benchmark(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    const std::optional<artifact_ref>& requested_artifact,
    const string_list& passthrough_args, std::ostream& out, std::ostream& err
) {
    resolved_artifact resolved;
    command_error status = command_support::resolve_benchmark_artifact(
        manifest_value, requested_artifact, &resolved, err
    );
    if (status != command_error::ok) {
        return status;
    }

    status = command_support::run_configure_build_tree(
        project_root, manifest_value, "release", false, false, true, err
    );
    if (status != command_error::ok) {
        return status;
    }
    status = command_support::build_target(
        project_root, "release", cmake_target_name(resolved.ref), err
    );
    if (status != command_error::ok) {
        return status;
    }

    const std::optional<std::filesystem::path> binary_path
        = command_support::runnable_binary_path(
            project_root, manifest_value, "release", resolved
        );
    if (!binary_path.has_value()) {
        command_support::print_error(
            err, command_error::task_failed,
            "built benchmark executable is missing"
        );
        return command_error::task_failed;
    }

    const std::filesystem::path output_dir = local_benchmark_dir(project_root)
        / benchmark_output_subdir(resolved.ref);
    const auto log_path = output_dir / "bench_log.txt";
    const auto result_path = output_dir / "result.json";
    const auto summary_path = output_dir / "summary.json";
    const auto plot_path = output_dir / "bench_plot.svg";
    const auto presentation_path = output_dir / "presentation.json";
    const auto path_errors = validate_project_paths(
        project_root,
        { log_path.lexically_relative(project_root),
          result_path.lexically_relative(project_root) }
    );
    if (!path_errors.empty()) {
        command_support::print_error(
            err, command_error::task_failed, path_errors.front()
        );
        return command_error::task_failed;
    }
    std::error_code fs_error;
    std::filesystem::create_directories(output_dir, fs_error);
    if (fs_error) {
        command_support::print_error(
            err, command_error::task_failed,
            output_dir.string() + ": " + fs_error.message()
        );
        return command_error::task_failed;
    }
    // A new execution must not leave charts from an earlier run beside its log.
    bool optional_reports_available = true;
    for (const auto& path : { summary_path, plot_path, presentation_path }) {
        std::filesystem::remove(path, fs_error);
        if (fs_error) {
            err << "warning: unable to retire optional benchmark report "
                << path.string() << ": " << fs_error.message() << "\n";
            optional_reports_available = false;
        }
    }

    std::vector<std::string> command { binary_path->string() };
    command.insert(
        command.end(), passthrough_args.begin(), passthrough_args.end()
    );
    const auto working_directory = local_build_dir(project_root, "release");
    const captured_command benchmark_result
        = capture_command_result(command, working_directory);

    std::string error_message;
    const json result {
        { "artifact", format_artifact_ref(resolved.ref) },
        { "profile", "release" },
        { "command", command },
        { "working_directory", working_directory.generic_string() },
        { "exit_code", benchmark_result.exit_code },
        { "status", benchmark_result.exit_code == 0 ? "passed" : "failed" },
        { "log", log_path.filename().generic_string() }
    };
    const auto result_text = result.dump(2) + "\n";
    if (!write_text_file(log_path, benchmark_result.output, &error_message)
        || !write_text_file(result_path, result_text, &error_message)) {
        command_support::print_error(
            err, command_error::task_failed, error_message
        );
        return command_error::task_failed;
    }
    out << "benchmark log: "
        << log_path.lexically_relative(project_root).generic_string() << "\n";
    out << "benchmark result: "
        << result_path.lexically_relative(project_root).generic_string()
        << "\n";
    if (benchmark_result.exit_code != 0) {
        command_support::print_error(
            err, command_error::task_failed,
            "benchmark executable failed: " + format_artifact_ref(resolved.ref)
                + " (exit " + std::to_string(benchmark_result.exit_code) + ")"
        );
        return command_error::task_failed;
    }
    out << "benchmarked " << format_artifact_ref(resolved.ref) << "\n";

    if (!optional_reports_available) {
        return command_error::ok;
    }
    benchmark_summary summary;
    parse_benchmark_log(benchmark_result.output, &summary, &error_message);
    if (summary.series.empty()) {
        out << "benchmark plot: skipped (no FLOPs series recognized)\n";
        return command_error::ok;
    }
    error_message.clear();
    const std::string title
        = format_artifact_ref(resolved.ref) + " GFLOPs/s vs n";
    const std::string plot_contents
        = render_benchmark_svg(summary, title, &error_message);
    const auto summary_text = to_json(summary).dump(2) + "\n";
    const json presentation {
        { "kind", "flops" },
        { "artifact", format_artifact_ref(resolved.ref) },
        { "files",
          { { "result.json", file_stamp(result_text) },
            { "bench_log.txt", file_stamp(benchmark_result.output) },
            { "summary.json", file_stamp(summary_text) },
            { "bench_plot.svg", file_stamp(plot_contents) } } }
    };
    if (!error_message.empty()
        || !write_text_file(summary_path, summary_text, &error_message)
        || !write_text_file(plot_path, plot_contents, &error_message)
        || !write_text_file(
            presentation_path, presentation.dump(2) + "\n", &error_message
        )) {
        err << "warning: optional benchmark report skipped: " << error_message
            << "\n";
        for (const auto& path :
             { summary_path, plot_path, presentation_path }) {
            std::filesystem::remove(path, fs_error);
            if (fs_error)
                err << "warning: unable to remove " << path.string() << ": "
                    << fs_error.message() << "\n";
        }
        return command_error::ok;
    }
    out << "benchmark summary: "
        << summary_path.lexically_relative(project_root).generic_string()
        << "\n";
    out << "benchmark plot: "
        << plot_path.lexically_relative(project_root).generic_string() << "\n";
    return command_error::ok;
}

std::filesystem::path benchmark_output_subdir(const artifact_ref& ref) {
    return std::filesystem::path(ref.component_id) / ref.artifact_id;
}

} // namespace ecosystem
