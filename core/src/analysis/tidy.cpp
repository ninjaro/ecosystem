#include "analysis/tidy.hpp"

#include "workspace/project.hpp"
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

}  // namespace tidy_support

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
    const fs::path compilation_database_path = build_dir / "compile_commands.json";
    report.compilation_database = compilation_database_path.string();
    report.compilation_database_available = path_exists(compilation_database_path);

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
        report.clang_tidy_skip_reason = "no source files available for clang-tidy";
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
                || (!entry.contains("command")
                    && !entry.contains("arguments"))) {
                report.clang_tidy_skip_reason
                    = "invalid compile_commands.json entry";
                return report;
            }
            configured_sources.insert(fs::weakly_canonical(directory / file));
        }
        for (const auto& source : sources) {
            if (!configured_sources.contains(
                    fs::weakly_canonical(source.path)
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

    report.analysis = analyze_project_sources(
        selected, project_root, std::nullopt, include_tests, include_benchmarks
    );

    std::vector<std::string> command {
        clang_tidy.path,
        "--quiet",
        "-p",
        build_dir.string(),
        "--warnings-as-errors=*",
    };
    for (const cxx_analysis_source& source : sources) {
        command.push_back(source.path.string());
    }

    const captured_command result = capture_command_result(command, project_root);
    report.clang_tidy_used = true;
    report.clang_tidy_exit_code = result.exit_code;
    report.clang_tidy_output = split_nonempty_lines(result.output);
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
    report["clang_tidy_exit_code"] = value.clang_tidy_exit_code;
    report["clang_tidy_output"] = value.clang_tidy_output;
    report["clang_tidy_skip_reason"] = value.clang_tidy_skip_reason;
    return report;
}

}  // namespace ecosystem
