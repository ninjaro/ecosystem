#include "analysis/personal.hpp"

#include "personal_internal.hpp"
#include "workspace/project.hpp"
#include "workspace/tooling.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <tuple>

namespace ecosystem::personal_support {

source_location locate(const CXSourceLocation location) {
    source_location result;
    CXFile file = nullptr;
    clang_getSpellingLocation(
        location, &file, &result.line, &result.column, &result.offset
    );
    if (file) {
        std::error_code error;
        result.file = fs::weakly_canonical(
            analysis_support::to_string(clang_getFileName(file)), error
        );
        if (error)
            result.file.clear();
    }
    return result;
}

void add_finding(inspection& state, diagnostic_finding finding) {
    const auto key = finding.artifact + "|" + finding.file + "|"
        + std::to_string(finding.line) + "|" + std::to_string(finding.column)
        + "|" + finding.rule + "|" + finding.entity;
    if (state.seen.insert(key).second)
        state.report.findings.push_back(std::move(finding));
}

void inspect_name(
    inspection& state, const source_file& file, const std::string& name,
    const std::string& entity_kind, const unsigned line, const unsigned column,
    const bool test
) {
    if (name.empty()
        || (entity_kind == "symbol" && state.allowlist.contains(name)))
        return;
    auto finding = [&](const std::string& rule, const std::string& message,
                       const json& measurements = json::object(),
                       const json& thresholds = json::object()) {
        add_finding(
            state,
            { rule, file.artifact, file.relative.generic_string(), line, column,
              entity_kind, name, message, measurements, thresholds }
        );
    };
    const auto words = 1 + std::count(name.begin(), name.end(), '_');
    const int limit = test ? 7 : 4;
    if (words > limit)
        finding(
            "naming.words", "name exceeds the word-count bound",
            { { "words", words } }, { { "words", limit } }
        );
    // The manifesto has not calibrated a character bound for test symbols.
    if (!test && name.size() > 25U)
        finding(
            "naming.characters", "name exceeds the character-count bound",
            { { "characters", name.size() } }, { { "characters", 25 } }
        );
    if (name.front() == '_')
        finding("naming.leading_underscore", "name starts with an underscore");
    if (name.back() == '_')
        finding("naming.trailing_underscore", "name ends with an underscore");
    if (name.starts_with("m_"))
        finding("naming.scope_prefix", "name uses the m_ scope prefix");
    const bool snake = std::all_of(
                           name.begin(), name.end(),
                           [](const unsigned char character) {
                               return (character >= 'a' && character <= 'z')
                                   || (character >= '0' && character <= '9')
                                   || character == '_';
                           }
                       )
        && name.find("__") == std::string::npos;
    if (!snake)
        finding("naming.snake_case", "name does not follow snake_case");
}

void load_allowlist(inspection& state, const fs::path& root) {
    for (const auto& path : { root / "docs/naming_allowlist.txt",
                              root / "naming_allowlist.txt" }) {
        std::error_code error;
        const bool exists = fs::exists(path, error);
        if (!exists && !error)
            continue;
        if (error) {
            state.report.errors.push_back(
                path.string() + ": " + error.message()
            );
            continue;
        }
        if (!fs::is_regular_file(path, error) || error) {
            state.report.errors.push_back(
                "unable to read naming exceptions: " + path.string()
            );
            continue;
        }
        std::string read_error;
        std::istringstream input(read_text_file(path, &read_error));
        if (!read_error.empty()) {
            state.report.errors.push_back(read_error);
            continue;
        }
        std::string line;
        while (std::getline(input, line)) {
            line = line.substr(0, line.find('#'));
            const auto begin = line.find_first_not_of(" \t\r");
            if (begin != std::string::npos)
                state.allowlist.insert(line.substr(
                    begin, line.find_last_not_of(" \t\r") - begin + 1
                ));
        }
    }
}

void collect_files(
    inspection& state, const manifest& value, const fs::path& root,
    const std::optional<artifact_ref>& requested
) {
    for (const auto& owner : value.components) {
        if (requested
            && (owner.id != requested->component_id
                || !find_artifact(owner, requested->artifact_id)))
            continue;
        std::vector<fs::path> files;
        for (const auto& list : { component_module_headers(root, owner),
                                  component_module_sources(root, owner),
                                  component_source_only_files(root, owner),
                                  component_header_only_files(root, owner),
                                  component_c_header_only_files(root, owner),
                                  component_template_impl_files(root, owner),
                                  component_c_header_pair_headers(root, owner),
                                  component_c_header_pair_sources(root, owner),
                                  component_test_sources(root, owner),
                                  component_benchmark_sources(root, owner) })
            files.insert(files.end(), list.begin(), list.end());
        for (const auto& path : files) {
            const auto errors = validate_project_paths(
                root, { path.lexically_relative(root) }
            );
            if (!errors.empty()) {
                state.report.errors.insert(
                    state.report.errors.end(), errors.begin(), errors.end()
                );
                continue;
            }
            std::error_code error;
            const auto canonical = fs::weakly_canonical(path, error);
            if (error) {
                state.report.errors.push_back(
                    path.string() + ": " + error.message()
                );
                continue;
            }
            if (state.files.contains(canonical))
                continue;
            if (!fs::is_regular_file(canonical, error) || error) {
                state.report.errors.push_back(
                    "unable to read selected file: " + path.string()
                );
                continue;
            }
            std::string read_error;
            const auto contents = read_text_file(path, &read_error);
            if (!read_error.empty()) {
                state.report.errors.push_back(read_error);
                continue;
            }
            const auto relative
                = path.lexically_relative(root).lexically_normal();
            const auto local = relative.lexically_relative(owner.root);
            const bool test
                = component_is_test_only(owner) || *local.begin() == "tests";
            const bool entry
                = owner.ownership && local == fs::path(owner.ownership->entry);
            source_file file {
                canonical,
                relative,
                owner.root,
                format_artifact_ref({ owner.id, owner.artifacts.front().id }),
                contents,
                test,
                entry
            };
            state.files.emplace(canonical, file);
            state.report.files.push_back(
                { { "file", relative.generic_string() },
                  { "artifact", file.artifact },
                  { "test", test } }
            );
            inspect_name(
                state, file, relative.stem().string(), "file", 1, 1, false
            );
            fs::path directory;
            for (const auto& part : relative.parent_path()) {
                directory /= part;
                auto directory_file = file;
                directory_file.relative = directory;
                inspect_name(
                    state, directory_file, part.string(), "directory", 1, 1,
                    false
                );
            }
        }
    }
}

bool named_declaration(const CXCursorKind kind) {
    switch (kind) {
    case CXCursor_Namespace:
    case CXCursor_StructDecl:
    case CXCursor_UnionDecl:
    case CXCursor_ClassDecl:
    case CXCursor_EnumDecl:
    case CXCursor_FieldDecl:
    case CXCursor_EnumConstantDecl:
    case CXCursor_FunctionDecl:
    case CXCursor_VarDecl:
    case CXCursor_ParmDecl:
    case CXCursor_TypedefDecl:
    case CXCursor_CXXMethod:
    case CXCursor_TemplateTypeParameter:
    case CXCursor_NonTypeTemplateParameter:
    case CXCursor_TemplateTemplateParameter:
    case CXCursor_FunctionTemplate:
    case CXCursor_ClassTemplate:
    case CXCursor_TypeAliasDecl:
    case CXCursor_TypeAliasTemplateDecl:
        return true;
    default:
        return false;
    }
}

CXChildVisitResult
inspect_cursor(CXCursor cursor, CXCursor, CXClientData data) {
    auto& state = *static_cast<inspection*>(data);
    const auto location = locate(clang_getCursorLocation(cursor));
    const auto found = state.files.find(location.file);
    if (found == state.files.end()) {
        const auto kind = clang_getCursorKind(cursor);
        return kind == CXCursor_Namespace || kind == CXCursor_LinkageSpec
            ? CXChildVisit_Recurse
            : CXChildVisit_Continue;
    }
    const auto name
        = analysis_support::to_string(clang_getCursorSpelling(cursor));
    const bool operator_name = name.starts_with("operator") && name.size() > 8
        && !std::isalnum(static_cast<unsigned char>(name[8])) && name[8] != '_';
    if (named_declaration(clang_getCursorKind(cursor)) && !operator_name)
        inspect_name(
            state, found->second, name, "symbol", location.line,
            location.column, found->second.test
        );
    if (state.report.profile == "style")
        inspect_structure(state, cursor, found->second);
    return CXChildVisit_Recurse;
}

struct parser {
    CXIndex index = clang_createIndex(0, 0);
    CXCompilationDatabase database = nullptr;

    ~parser() {
        if (database)
            clang_CompilationDatabase_dispose(database);
        if (index)
            clang_disposeIndex(index);
    }
};

struct translation_unit {
    CXTranslationUnit value = nullptr;

    ~translation_unit() {
        if (value)
            clang_disposeTranslationUnit(value);
    }
};

void inspect_sources(
    inspection& state, const manifest& value, const fs::path& root
) {
    parser context;
    if (!context.index) {
        state.report.errors.push_back(
            "unable to create the Clang analysis index"
        );
        return;
    }
    const auto database_dir = local_build_dir(root, "debug");
    if (fs::exists(database_dir / "compile_commands.json")) {
        CXCompilationDatabase_Error error;
        context.database = clang_CompilationDatabase_fromDirectory(
            database_dir.c_str(), &error
        );
        if (error != CXCompilationDatabase_NoError) {
            state.report.errors.push_back(
                "unable to read "
                + (database_dir / "compile_commands.json").string()
            );
            return;
        }
    }
    const auto includes
        = manifest_include_dirs(value, root, std::nullopt, true, true);
    const auto system_includes
        = analysis_support::detected_system_include_args();
    std::vector<const source_file*> ordered;
    for (const auto& [path, file] : state.files) {
        static_cast<void>(path);
        ordered.push_back(&file);
    }
    const auto is_source = [](const source_file* file) {
        const auto extension = file->path.extension();
        return extension == ".c" || extension == ".cpp" || extension == ".cc"
            || extension == ".cxx";
    };
    std::stable_sort(
        ordered.begin(), ordered.end(),
        [&](const auto* first, const auto* second) {
            return is_source(first) > is_source(second);
        }
    );
    for (const auto* file : ordered) {
        if (state.included.contains(file->path))
            continue;
        auto args = analysis_support::compilation_database_args(
            context.database, file->path
        );
        if (args.empty()) {
            args = analysis_support::fallback_args(value, includes);
            if (file->path.extension() == ".c") {
                args[0] = "-xc";
                args.erase(args.begin() + 1);
            }
        }
        args.insert(args.end(), system_includes.begin(), system_includes.end());
        args.push_back("-Wno-error");
        std::vector<const char*> raw;
        for (const auto& arg : args)
            raw.push_back(arg.c_str());
        translation_unit unit;
        const auto status = clang_parseTranslationUnit2(
            context.index, file->path.c_str(), raw.data(),
            static_cast<int>(raw.size()), nullptr, 0,
            CXTranslationUnit_KeepGoing, &unit.value
        );
        if (status != CXError_Success || !unit.value) {
            state.report.errors.push_back(
                file->relative.generic_string()
                + ": unable to parse translation unit"
            );
            continue;
        }
        for (unsigned index = 0; index < clang_getNumDiagnostics(unit.value);
             ++index) {
            const auto diagnostic = clang_getDiagnostic(unit.value, index);
            if (clang_getDiagnosticSeverity(diagnostic) >= CXDiagnostic_Error)
                state.report.errors.push_back(
                    analysis_support::to_string(clang_formatDiagnostic(
                        diagnostic, clang_defaultDiagnosticDisplayOptions()
                    ))
                );
            clang_disposeDiagnostic(diagnostic);
        }
        clang_getInclusions(
            unit.value,
            [](CXFile included, CXSourceLocation*, unsigned,
               CXClientData data) {
                auto& target = *static_cast<inspection*>(data);
                std::error_code error;
                const auto path = fs::weakly_canonical(
                    analysis_support::to_string(clang_getFileName(included)),
                    error
                );
                if (!error)
                    target.included.insert(path);
            },
            &state
        );
        if (state.report.profile == "style")
            inspect_file_volume(state, unit.value);
        clang_visitChildren(
            clang_getTranslationUnitCursor(unit.value), inspect_cursor, &state
        );
    }
}

} // namespace ecosystem::personal_support

namespace ecosystem {

personal_report analyze_personal_checks(
    const manifest& value, const std::filesystem::path& root,
    const std::optional<artifact_ref>& requested, const std::string& profile
) {
    personal_report report;
    report.project = value.id;
    report.profile = profile;
    report.requested_artifact = requested;
    if (profile != "naming" && profile != "style") {
        report.errors.push_back(
            "unknown personal analysis profile: " + profile
        );
        return report;
    }
    if (requested && !resolve_artifact(value, requested)) {
        report.errors.push_back(
            "unknown artifact request: " + format_artifact_ref(*requested)
        );
        return report;
    }
    personal_support::inspection state { report, {}, {}, {}, {} };
    personal_support::load_allowlist(state, root);
    personal_support::collect_files(state, value, root, requested);
    personal_support::inspect_sources(state, value, root);
    if (profile == "style")
        personal_support::finish_structure(state);
    for (auto& finding : report.findings)
        for (const auto& file : report.files)
            if (file.at("file") == finding.file && file.contains("referent")
                && !file.at("referent").is_null()) {
                finding.referent = file.at("referent").get<std::string>();
                break;
            }
    std::sort(
        report.findings.begin(), report.findings.end(),
        [](const auto& first, const auto& second) {
            return std::tie(
                       first.file, first.line, first.column, first.rule,
                       first.entity
                   )
                < std::tie(
                       second.file, second.line, second.column, second.rule,
                       second.entity
                );
        }
    );
    std::sort(report.errors.begin(), report.errors.end());
    report.errors.erase(
        std::unique(report.errors.begin(), report.errors.end()),
        report.errors.end()
    );
    return report;
}

json to_json(const personal_report& report) {
    json findings = json::array();
    for (const auto& finding : report.findings)
        findings.push_back(to_json(finding));
    json result { { "project", report.project },
                  { "profile", report.profile },
                  { "status",
                    !report.errors.empty()        ? "failed"
                        : report.findings.empty() ? "clean"
                                                  : "findings" },
                  { "findings", findings },
                  { "errors", report.errors },
                  { "files", report.files },
                  { "scopes", report.scopes },
                  { "referents", report.referents },
                  { "modules", report.modules } };
    if (report.requested_artifact)
        result["artifact"] = format_artifact_ref(*report.requested_artifact);
    return result;
}

void render_personal_report(const personal_report& report, std::ostream& out) {
    render_diagnostic_report(to_json(report), out);
}

} // namespace ecosystem
