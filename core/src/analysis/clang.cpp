#include "analysis/clang.hpp"

#include "clang_internal.hpp"
#include "workspace/project.hpp"
#include "workspace/tooling.hpp"

#include <clang-c/CXCompilationDatabase.h>
#include <clang-c/Index.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace ecosystem {
namespace analysis_support {

std::string to_string(const CXString value) {
    const char* const text = clang_getCString(value);
    const std::string result = text == nullptr ? std::string() : std::string(text);
    clang_disposeString(value);
    return result;
}

std::string severity_name(const CXDiagnosticSeverity severity) {
    switch (severity) {
    case CXDiagnostic_Ignored:
        return "ignored";
    case CXDiagnostic_Note:
        return "note";
    case CXDiagnostic_Warning:
        return "warning";
    case CXDiagnostic_Error:
        return "error";
    case CXDiagnostic_Fatal:
        return "fatal";
    }
    return "unknown";
}

std::string trim_line(const std::string& line) {
    const std::size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = line.find_last_not_of(" \t\r\n");
    return line.substr(first, last - first + 1U);
}

bool is_ignorable_driver_warning(const std::string& message) {
    return message.find("-Wunused-command-line-argument")
        != std::string::npos;
}

std::vector<std::string> fallback_args(
    const manifest& manifest_value, const std::vector<fs::path>& include_dirs
) {
    std::vector<std::string> args;
    args.push_back("-xc++");
    args.push_back("-std=c++" + std::to_string(manifest_value.cpp_standard));
    args.push_back("-fsyntax-only");
    for (const fs::path& include_dir : include_dirs) {
        args.push_back("-I" + include_dir.string());
    }
    return args;
}

std::vector<std::string> detected_system_include_args() {
    const tool_status clang_tool = probe_tool("clang++");
    if (!clang_tool.available) {
        return {};
    }

    const std::string output = capture_command({ clang_tool.path, "-E", "-x", "c++", "/dev/null", "-v" });
    std::vector<std::string> args;
    bool in_search_list = false;
    std::stringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find("#include <...> search starts here:") != std::string::npos) {
            in_search_list = true;
            continue;
        }
        if (!in_search_list) {
            continue;
        }
        if (line.find("End of search list.") != std::string::npos) {
            break;
        }
        const std::string trimmed = trim_line(line);
        if (trimmed.empty()) {
            continue;
        }
        args.push_back("-isystem");
        args.push_back(trimmed);
    }
    return args;
}

std::vector<std::string> compilation_database_args(
    CXCompilationDatabase database,
    const fs::path& file
) {
    std::vector<std::string> args;
    if (database == nullptr) {
        return args;
    }

    CXCompileCommands commands = clang_CompilationDatabase_getCompileCommands(database, file.c_str());
    const unsigned count = clang_CompileCommands_getSize(commands);
    if (count == 0U) {
        clang_CompileCommands_dispose(commands);
        return args;
    }

    const CXCompileCommand command = clang_CompileCommands_getCommand(commands, 0U);
    args.push_back("-working-directory=" + to_string(clang_CompileCommand_getDirectory(command)));

    bool skip_next = false;
    for (unsigned index = 1U; index < clang_CompileCommand_getNumArgs(command); ++index) {
        if (skip_next) {
            skip_next = false;
            continue;
        }
        const std::string arg = to_string(clang_CompileCommand_getArg(command, index));
        if (arg == file.string() || arg == file.filename().string() || arg == "-c") {
            continue;
        }
        if (arg == "-o" || arg == "-MF" || arg == "-MT" || arg == "-MQ") {
            skip_next = true;
            continue;
        }
        args.push_back(arg);
    }

    clang_CompileCommands_dispose(commands);
    return args;
}

struct cursor_visit_data {
    std::string primary_file;
    source_analysis* analysis = nullptr;
};

CXChildVisitResult visitor(CXCursor cursor, CXCursor /*parent*/, CXClientData client_data) {
    auto* const visit_data = static_cast<cursor_visit_data*>(client_data);
    CXSourceLocation location = clang_getCursorLocation(cursor);
    if (clang_Location_isFromMainFile(location) == 0) {
        return CXChildVisit_Recurse;
    }

    const CXCursorKind kind = clang_getCursorKind(cursor);
    switch (kind) {
    case CXCursor_FunctionDecl:
    case CXCursor_FunctionTemplate:
    case CXCursor_CXXMethod:
    case CXCursor_Constructor:
    case CXCursor_Destructor:
        ++visit_data->analysis->function_count;
        break;
    case CXCursor_ClassDecl:
    case CXCursor_StructDecl:
    case CXCursor_ClassTemplate:
        ++visit_data->analysis->class_count;
        break;
    case CXCursor_Namespace:
        ++visit_data->analysis->namespace_count;
        break;
    default:
        break;
    }
    return CXChildVisit_Recurse;
}

source_analysis analyze_single_file(
    CXIndex index,
    const manifest& manifest_value,
    const fs::path& project_root,
    CXCompilationDatabase compilation_database,
    const std::vector<fs::path>& include_dirs,
    const std::vector<std::string>& system_include_args,
    const cxx_analysis_source& source
) {
    source_analysis analysis;
    analysis.file = source.path.lexically_relative(project_root).generic_string();
    analysis.component_id = source.component_id;
    analysis.category = source.category;
    analysis.artifact = source.artifact;

    std::vector<std::string> owned_args
        = compilation_database_args(compilation_database, source.path);
    if (owned_args.empty()) {
        owned_args = fallback_args(manifest_value, include_dirs);
    }
    owned_args.insert(owned_args.end(), system_include_args.begin(), system_include_args.end());

    std::vector<const char*> args;
    args.reserve(owned_args.size());
    for (const std::string& argument : owned_args) {
        args.push_back(argument.c_str());
    }

    CXTranslationUnit translation_unit = nullptr;
    const CXErrorCode error_code = clang_parseTranslationUnit2(
        index,
        source.path.c_str(),
        args.data(),
        static_cast<int>(args.size()),
        nullptr,
        0,
        CXTranslationUnit_None,
        &translation_unit
    );

    if (error_code != CXError_Success || translation_unit == nullptr) {
        analysis.error_count = 1;
        analysis.diagnostics.push_back("fatal: unable to parse translation unit");
        if (translation_unit != nullptr)
            clang_disposeTranslationUnit(translation_unit);
        return analysis;
    }

    const unsigned diagnostic_count = clang_getNumDiagnostics(translation_unit);
    for (unsigned index_value = 0; index_value < diagnostic_count; ++index_value) {
        const CXDiagnostic diagnostic = clang_getDiagnostic(translation_unit, index_value);
        const CXDiagnosticSeverity severity = clang_getDiagnosticSeverity(diagnostic);
        const std::string formatted = to_string(
            clang_formatDiagnostic(
                diagnostic,
                clang_defaultDiagnosticDisplayOptions()
            )
        );
        if (severity == CXDiagnostic_Warning
            && is_ignorable_driver_warning(formatted)) {
            clang_disposeDiagnostic(diagnostic);
            continue;
        }
        const std::string message
            = severity_name(severity) + ": " + formatted;
        if (severity == CXDiagnostic_Warning) {
            ++analysis.warning_count;
        } else if (severity == CXDiagnostic_Error || severity == CXDiagnostic_Fatal) {
            ++analysis.error_count;
        }
        if (severity >= CXDiagnostic_Warning) {
            analysis.diagnostics.push_back(message);
            diagnostic_finding finding;
            finding.rule
                = to_string(clang_getDiagnosticOption(diagnostic, nullptr));
            if (finding.rule.empty())
                finding.rule = "clang.diagnostic."
                    + std::to_string(clang_getDiagnosticCategory(diagnostic));
            finding.category
                = to_string(clang_getDiagnosticCategoryText(diagnostic));
            finding.severity = severity_name(severity);
            finding.authority = "external";
            finding.enforcement = "required";
            finding.artifact = source.artifact;
            finding.file = analysis.file;
            CXFile location_file = nullptr;
            clang_getExpansionLocation(
                clang_getDiagnosticLocation(diagnostic), &location_file,
                &finding.line, &finding.column, nullptr
            );
            if (location_file != nullptr) {
                const fs::path path
                    = to_string(clang_getFileName(location_file));
                const auto relative = path.lexically_relative(project_root);
                finding.file = relative.empty() ? path.generic_string()
                                                : relative.generic_string();
            }
            finding.entity_kind = "translation_unit";
            finding.entity = analysis.file;
            finding.message
                = to_string(clang_getDiagnosticSpelling(diagnostic));
            analysis.findings.push_back(std::move(finding));
        }
        clang_disposeDiagnostic(diagnostic);
    }

    cursor_visit_data visit_data {
        analysis.file,
        &analysis,
    };
    clang_visitChildren(clang_getTranslationUnitCursor(translation_unit), visitor, &visit_data);
    clang_disposeTranslationUnit(translation_unit);
    return analysis;
}

}  // namespace analysis_support

using namespace analysis_support;

cxx_analysis_report analyze_project_sources(
    const manifest& value, const fs::path& project_root,
    const std::optional<artifact_ref>& requested_artifact,
    const bool include_tests, const bool include_benchmarks
) {
    cxx_analysis_report report;
    report.project_id = value.id;
    report.requested_artifact = requested_artifact;
    report.cpp_standard = value.cpp_standard;
    report.tests_included = include_tests;
    report.benchmarks_included = include_benchmarks;

    if (requested_artifact && !resolve_artifact(value, requested_artifact)) {
        report.errors.push_back(
            "unknown artifact request: "
            + format_artifact_ref(*requested_artifact)
        );
        return report;
    }
    manifest selected = value;
    if (requested_artifact)
        std::erase_if(selected.components, [&](const component& candidate) {
            return candidate.id != requested_artifact->component_id
                || !find_artifact(candidate, requested_artifact->artifact_id);
        });

    const std::vector<fs::path> include_dirs = manifest_include_dirs(
        value, project_root, std::nullopt, include_tests, include_benchmarks
    );
    const std::vector<cxx_analysis_source> source_files = cxx_analysis_sources(
        selected, project_root, std::nullopt, include_tests, include_benchmarks
    );

    if (source_files.empty()) {
        report.errors.push_back("no source files available for Clang analysis");
        return report;
    }

    CXCompilationDatabase compilation_database = nullptr;
    const fs::path compile_database_dir = local_build_dir(project_root, "debug");
    if (std::error_code error; fs::exists(compile_database_dir / "compile_commands.json", error) && !error) {
        CXCompilationDatabase_Error database_error = CXCompilationDatabase_NoError;
        compilation_database = clang_CompilationDatabase_fromDirectory(
            compile_database_dir.c_str(),
            &database_error
        );
        if (database_error != CXCompilationDatabase_NoError) {
            report.errors.push_back(
                "unable to load compile_commands.json for Clang analysis"
            );
            return report;
        }
    }

    const std::vector<std::string> system_include_args = detected_system_include_args();
    CXIndex index = clang_createIndex(0, 0);
    if (index == nullptr) {
        report.errors.push_back("unable to initialize Clang analysis");
        if (compilation_database != nullptr)
            clang_CompilationDatabase_dispose(compilation_database);
        return report;
    }
    for (const cxx_analysis_source& source_file : source_files) {
        std::error_code error;
        if (!fs::is_regular_file(source_file.path, error)) {
            report.errors.push_back(
                "missing source file: "
                + source_file.path.lexically_relative(project_root)
                      .generic_string()
            );
            continue;
        }
        source_analysis analysis = analyze_single_file(
            index,
            value,
            project_root,
            compilation_database,
            include_dirs,
            system_include_args,
            source_file
        );
        if (analysis.error_count > 0)
            report.errors.push_back(
                analysis.file
                + ": C++ analysis incomplete: " + analysis.diagnostics.front()
            );
        if (analysis.category == "tests") {
            ++report.test_files;
        } else if (analysis.category == "benchmarks") {
            ++report.benchmark_files;
        } else if (analysis.category == "runtime") {
            ++report.runtime_files;
        } else {
            ++report.core_files;
        }
        report.total_warnings += analysis.warning_count;
        report.total_errors += analysis.error_count;
        report.total_functions += analysis.function_count;
        report.total_classes += analysis.class_count;
        report.total_namespaces += analysis.namespace_count;

        component_analysis_summary* component_summary = nullptr;
        for (component_analysis_summary& summary : report.component_summaries) {
            if (summary.component_id == analysis.component_id) {
                component_summary = &summary;
                break;
            }
        }
        if (component_summary == nullptr) {
            report.component_summaries.push_back(
                component_analysis_summary { analysis.component_id }
            );
            component_summary = &report.component_summaries.back();
        }
        ++component_summary->files_analyzed;
        if (analysis.category == "tests") {
            ++component_summary->test_files;
        } else if (analysis.category == "benchmarks") {
            ++component_summary->benchmark_files;
        } else if (analysis.category == "runtime") {
            ++component_summary->runtime_files;
        } else {
            ++component_summary->core_files;
        }
        component_summary->total_warnings += analysis.warning_count;
        component_summary->total_errors += analysis.error_count;
        component_summary->function_count += analysis.function_count;
        component_summary->class_count += analysis.class_count;
        component_summary->namespace_count += analysis.namespace_count;
        report.sources.push_back(std::move(analysis));
    }
    clang_disposeIndex(index);
    if (compilation_database != nullptr) {
        clang_CompilationDatabase_dispose(compilation_database);
    }

    report.files_analyzed = static_cast<int>(report.sources.size());
    return report;
}

json to_json(const cxx_analysis_report& value) {
    json report = json::object();
    report["project"] = value.project_id;
    report["profile"] = "cxx";
    report["status"] = !value.errors.empty()                   ? "failed"
        : (value.total_warnings > 0 || value.total_errors > 0) ? "findings"
                                                               : "clean";
    if (value.requested_artifact)
        report["artifact"] = format_artifact_ref(*value.requested_artifact);
    report["cpp_standard"] = value.cpp_standard;
    report["tests_included"] = value.tests_included;
    report["benchmarks_included"] = value.benchmarks_included;
    report["files_analyzed"] = value.files_analyzed;
    report["core_files"] = value.core_files;
    report["runtime_files"] = value.runtime_files;
    report["test_files"] = value.test_files;
    report["benchmark_files"] = value.benchmark_files;
    report["total_warnings"] = value.total_warnings;
    report["total_errors"] = value.total_errors;
    report["total_functions"] = value.total_functions;
    report["total_classes"] = value.total_classes;
    report["total_namespaces"] = value.total_namespaces;

    json sources = json::array();
    json findings = json::array();
    for (const source_analysis& source : value.sources) {
        for (const auto& finding : source.findings)
            findings.push_back(to_json(finding));
        json source_json = json::object();
        source_json["file"] = source.file;
        source_json["component"] = source.component_id;
        source_json["category"] = source.category;
        source_json["artifact"] = source.artifact;
        source_json["warning_count"] = source.warning_count;
        source_json["error_count"] = source.error_count;
        source_json["function_count"] = source.function_count;
        source_json["class_count"] = source.class_count;
        source_json["namespace_count"] = source.namespace_count;
        source_json["diagnostics"] = source.diagnostics;
        sources.push_back(source_json);
    }
    report["sources"] = sources;
    report["findings"] = findings;

    json components = json::array();
    for (const component_analysis_summary& component : value.component_summaries) {
        json component_json = json::object();
        component_json["component"] = component.component_id;
        component_json["files_analyzed"] = component.files_analyzed;
        component_json["core_files"] = component.core_files;
        component_json["runtime_files"] = component.runtime_files;
        component_json["test_files"] = component.test_files;
        component_json["benchmark_files"] = component.benchmark_files;
        component_json["total_warnings"] = component.total_warnings;
        component_json["total_errors"] = component.total_errors;
        component_json["function_count"] = component.function_count;
        component_json["class_count"] = component.class_count;
        component_json["namespace_count"] = component.namespace_count;
        components.push_back(component_json);
    }
    report["components"] = components;
    report["errors"] = value.errors;
    return report;
}

}  // namespace ecosystem
