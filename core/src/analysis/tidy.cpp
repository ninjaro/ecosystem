#include "analysis/tidy.hpp"
#include "verification_internal.hpp"

#include "workspace/project.hpp"
#include "workspace/template_text.hpp"
#include "workspace/tooling.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace ecosystem {
namespace tidy_support {

    bool path_exists(const fs::path& path) {
        std::error_code error;
        return fs::exists(path, error) && !error;
    }

    string_list split_nonempty_lines(const std::string& value) {
        string_list lines;
        std::stringstream stream(value);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
        return lines;
    }

    bool has_configuration_error(const string_list& lines) {
        return std::any_of(
            lines.begin(), lines.end(),
            [](const std::string& line) {
                return (line.find(".clang-tidy:") != std::string::npos
                        && line.find("error:") != std::string::npos)
                    || (line.find("Error parsing ") != std::string::npos
                        && line.find(".clang-tidy") != std::string::npos);
            }
        );
    }

} // namespace tidy_support

using namespace tidy_support;

tidy_check_report run_tidy_check(
    const manifest& value, const fs::path& project_root,
    const std::optional<artifact_ref>& requested_artifact,
    const bool include_tests, const bool include_benchmarks,
    const std::string& profile
) {
    tidy_check_report report;
    report.analysis.project_id = value.id;
    report.analysis.cpp_standard = value.cpp_standard;
    report.analysis.tests_included = include_tests;
    report.analysis.benchmarks_included = include_benchmarks;

    const fs::path build_dir = local_build_dir(project_root, profile);
    const fs::path compilation_database_path
        = build_dir / "compile_commands.json";
    report.compilation_database = compilation_database_path.string();
    report.compilation_database_available
        = path_exists(compilation_database_path);

    const tool_status clang_tidy = probe_tool("clang-tidy");
    report.clang_tidy_available = clang_tidy.available;
    report.clang_tidy_path = clang_tidy.path;
    if (!report.clang_tidy_available) {
        report.clang_tidy_skip_reason = "clang-tidy is not available";
        return report;
    }
    if (!report.compilation_database_available) {
        report.clang_tidy_skip_reason
            = "compile_commands.json is not available";
        return report;
    }

    manifest selected = value;
    if (requested_artifact) {
        std::erase_if(selected.components, [&](const component& candidate) {
            return candidate.id != requested_artifact->component_id
                || !find_artifact(candidate, requested_artifact->artifact_id);
        });
    }
    const std::vector<cxx_analysis_source> sources = cxx_analysis_sources(
        selected, project_root, std::nullopt, include_tests, include_benchmarks
    );
    if (sources.empty()) {
        report.clang_tidy_skip_reason
            = "no source files available for clang-tidy";
        return report;
    }

    // clang-tidy can fall back to guessed compiler arguments when a selected
    // file has no database entry. A required check must use configured inputs.
    try {
        std::ifstream input(compilation_database_path);
        const json database = json::parse(input);
        if (!database.is_array()) {
            report.clang_tidy_skip_reason
                = "compile_commands.json must contain an array";
            return report;
        }
        std::set<fs::path> configured_sources;
        for (const auto& entry : database) {
            const fs::path directory = entry.at("directory").get<std::string>();
            const fs::path file = entry.at("file").get<std::string>();
            if (!directory.is_absolute()
                || (!entry.contains("command") && !entry.contains("arguments")
                )) {
                report.clang_tidy_skip_reason
                    = "invalid compile_commands.json entry";
                return report;
            }
            configured_sources.insert(fs::weakly_canonical(directory / file));
        }
        for (const auto& source : sources) {
            if (!configured_sources.contains(fs::weakly_canonical(source.path)
                )) {
                report.clang_tidy_skip_reason
                    = "compile_commands.json has no entry for "
                    + source.path.lexically_relative(project_root)
                          .generic_string();
                return report;
            }
        }
    } catch (const json::exception& error) {
        report.clang_tidy_skip_reason
            = "invalid compile_commands.json: " + std::string(error.what());
        return report;
    } catch (const fs::filesystem_error& error) {
        report.clang_tidy_skip_reason = error.what();
        return report;
    }

    // Optional local configuration must not make the required check inherit an
    // unrelated parent's defaults or silently lose the canonical diagnostics.
    fs::path configuration = project_root / ".clang-tidy";
    if (!path_exists(configuration)) {
        configuration = locate_template_path({ "tooling/clang-tidy.tpl" });
    }
    if (configuration.empty()) {
        report.clang_tidy_configuration_error = true;
        report.clang_tidy_output
            = { "canonical clang-tidy configuration is unavailable" };
        return report;
    }
    const std::string configuration_argument
        = "--config-file=" + configuration.string();
    const captured_command verification = capture_command_result(
        { clang_tidy.path, "--verify-config", configuration_argument },
        project_root
    );
    if (verification.exit_code != 0) {
        report.clang_tidy_configuration_error = true;
        report.clang_tidy_exit_code = verification.exit_code;
        report.clang_tidy_output = split_nonempty_lines(verification.output);
        return report;
    }

    verification_detail::input_batch batch_inputs(
        project_root, profile == "debug"
    );
    report.analysis = analyze_project_sources(
        selected, project_root, std::nullopt, include_tests, include_benchmarks
    );

    std::vector<std::string> command {
        clang_tidy.path,
        "--quiet",
        "-p",
        build_dir.string(),
        "--warnings-as-errors=*",
        configuration_argument,
    };
    report.clang_tidy_used = true;
    const auto keys = verification_detail::input_keys(project_root);
    const auto check = [&](const cxx_analysis_source& source) {
        const auto start = std::chrono::steady_clock::now();
        const auto cache = verification_detail::cache_file(
            project_root, keys, source.path, "tidy"
        );
        const auto saved = verification_detail::read_success(cache);
        const bool reused = saved == json { { "exit_code", 0 } };
        captured_command result { 0, {} };
        if (!reused) {
            auto invocation = command;
            invocation.push_back(source.path.string());
            result = capture_command_result(invocation, project_root);
        }
        return std::pair {
            result,
            json { { "file",
                     source.path.lexically_relative(project_root)
                         .generic_string() },
                   { "status", reused ? "reused" : "executed" },
                   { "seconds", verification_detail::elapsed(start) },
                   { "exit_code", result.exit_code },
                   { "reason",
                     reused              ? "matching verified compiler inputs"
                         : cache.empty() ? "unknown inputs; execution required"
                                         : "fresh verification required or no "
                                           "clean evidence" } }
        };
    };
    for (std::size_t batch = 0; batch < sources.size(); batch += 2) {
        std::vector<std::future<std::pair<captured_command, json>>> pending;
        pending.reserve(2);
        for (std::size_t index = batch;
             index < std::min(batch + 2, sources.size()); ++index)
            pending.emplace_back(
                std::async(std::launch::async, check, sources[index])
            );
        for (auto& operation : pending) {
            auto [result, execution] = operation.get();
            report.execution.push_back(execution);
            if (result.exit_code != 0)
                report.clang_tidy_exit_code = result.exit_code;
            const auto lines = split_nonempty_lines(result.output);
            report.clang_tidy_output.insert(
                report.clang_tidy_output.end(), lines.begin(), lines.end()
            );
            if (std::getenv("ECOSYSTEM_CI_PROGRESS") != nullptr)
                std::clog << "[tidy] " << execution.dump() << std::endl;
        }
    }
    const auto final_keys = profile == "debug"
        ? verification_detail::fresh_input_keys(project_root)
        : json::object();
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const auto before = verification_detail::cache_file(
            project_root, keys, sources[index].path, "tidy"
        );
        const auto after = verification_detail::cache_file(
            project_root, final_keys, sources[index].path, "tidy"
        );
        report.execution[index]["cache_file"]
            = before.lexically_relative(project_root).generic_string();
        auto invocation = command;
        invocation.push_back(sources[index].path.string());
        report.execution[index]["command"]
            = report.execution[index]["status"] == "executed" ? json(invocation)
                                                              : json(nullptr);
        if (!before.empty() && before != after) {
            report.clang_tidy_exit_code = 1;
            report.clang_tidy_output.emplace_back(
                "inputs changed during verification: "
                + sources[index].path.string()
            );
        } else if (!before.empty()
                   && report.execution[index]["exit_code"] == 0) {
            verification_detail::write_success(
                before, json { { "exit_code", 0 } }
            );
        }
    }
    report.clang_tidy_configuration_error
        = has_configuration_error(report.clang_tidy_output);
    batch_inputs.publish(final_keys);
    return report;
}

json to_json(const tidy_check_report& value) {
    json report = to_json(value.analysis);
    report["compilation_database_available"]
        = value.compilation_database_available;
    report["compilation_database"] = value.compilation_database;
    report["clang_tidy_available"] = value.clang_tidy_available;
    report["clang_tidy_path"] = value.clang_tidy_path;
    report["clang_tidy_used"] = value.clang_tidy_used;
    report["clang_tidy_configuration_error"]
        = value.clang_tidy_configuration_error;
    report["clang_tidy_exit_code"] = value.clang_tidy_exit_code;
    report["clang_tidy_output"] = value.clang_tidy_output;
    report["clang_tidy_skip_reason"] = value.clang_tidy_skip_reason;
    report["tidy_execution"] = value.execution;
    return report;
}

} // namespace ecosystem
