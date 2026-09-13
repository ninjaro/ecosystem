#include "personal_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace ecosystem::personal_support {

namespace structure_support {

    struct token_list {
        CXTranslationUnit unit;
        CXToken* values = nullptr;
        unsigned count = 0;

        token_list(CXTranslationUnit translation, CXSourceRange range)
            : unit(translation) {
            clang_tokenize(unit, range, &values, &count);
        }

        ~token_list() {
            if (values)
                clang_disposeTokens(unit, values, count);
        }
    };

    std::vector<CXCursor> children(const CXCursor cursor) {
        std::vector<CXCursor> result;
        clang_visitChildren(
            cursor,
            [](CXCursor child, CXCursor, CXClientData data) {
                static_cast<std::vector<CXCursor>*>(data)->push_back(child);
                return CXChildVisit_Continue;
            },
            &result
        );
        return result;
    }

    source_location start(const CXCursor cursor) {
        return locate(clang_getRangeStart(clang_getCursorExtent(cursor)));
    }

    source_location end(const CXCursor cursor) {
        return locate(clang_getRangeEnd(clang_getCursorExtent(cursor)));
    }

    bool has_token_between(
        CXCursor cursor, const std::string& spelling, unsigned begin,
        unsigned finish
    ) {
        token_list tokens(
            clang_Cursor_getTranslationUnit(cursor),
            clang_getCursorExtent(cursor)
        );
        for (unsigned index = 0; index < tokens.count; ++index) {
            const auto token = tokens.values[index];
            const auto location
                = locate(clang_getTokenLocation(tokens.unit, token));
            if (location.offset >= begin && location.offset <= finish
                && clang_getTokenKind(token) != CXToken_Comment
                && analysis_support::to_string(
                       clang_getTokenSpelling(tokens.unit, token)
                   ) == spelling)
                return true;
        }
        return false;
    }

    bool control(const CXCursorKind kind) {
        return kind == CXCursor_IfStmt || kind == CXCursor_ForStmt
            || kind == CXCursor_WhileStmt || kind == CXCursor_DoStmt
            || kind == CXCursor_CXXForRangeStmt || kind == CXCursor_SwitchStmt
            || kind == CXCursor_ConditionalOperator
            || kind == CXCursor_CXXTryStmt || kind == CXCursor_CXXCatchStmt;
    }

    struct complexity {
        unsigned branches = 1;
        unsigned nesting = 0;
    };

    void
    measure_complexity(CXCursor cursor, unsigned depth, complexity& result) {
        const auto kind = clang_getCursorKind(cursor);
        if (kind == CXCursor_LambdaExpr || kind == CXCursor_CXXMethod
            || kind == CXCursor_FunctionDecl
            || kind == CXCursor_FunctionTemplate || kind == CXCursor_ClassDecl
            || kind == CXCursor_StructDecl)
            return; // Nested unnamed behaviour receives its own scope
                    // measurement.
        const auto nested = children(cursor);
        if (control(kind)) {
            ++depth;
            result.nesting = std::max(result.nesting, depth);
            if (kind != CXCursor_SwitchStmt && kind != CXCursor_CXXTryStmt)
                ++result.branches;
        } else if (kind == CXCursor_CaseStmt) {
            ++result.branches;
        } else if (kind == CXCursor_BinaryOperator && nested.size() == 2) {
            if (has_token_between(
                    cursor, "&&", end(nested[0]).offset, start(nested[1]).offset
                )
                || has_token_between(
                    cursor, "||", end(nested[0]).offset, start(nested[1]).offset
                ))
                ++result.branches;
        }
        for (const auto child : nested)
            measure_complexity(child, depth, result);
    }

    bool ordinary_function(CXCursorKind kind) {
        return kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod
            || kind == CXCursor_FunctionTemplate || kind == CXCursor_Constructor
            || kind == CXCursor_Destructor
            || kind == CXCursor_ConversionFunction;
    }

    void inspect_braces(
        inspection& state, CXCursor cursor, const source_file& file
    ) {
        const auto kind = clang_getCursorKind(cursor);
        const auto nested = children(cursor);
        if (nested.empty())
            return;
        std::vector<CXCursor> bodies;
        if (kind == CXCursor_IfStmt) {
            bodies.push_back(nested.back());
            if (nested.size() >= 2
                && has_token_between(
                    cursor, "else", end(nested[nested.size() - 2]).offset,
                    start(nested.back()).offset
                ))
                bodies.push_back(nested[nested.size() - 2]);
        } else if (kind == CXCursor_DoStmt) {
            bodies.push_back(nested.front());
        } else if (
            kind == CXCursor_ForStmt || kind == CXCursor_WhileStmt
            || kind == CXCursor_CXXForRangeStmt || kind == CXCursor_SwitchStmt
        ) {
            bodies.push_back(nested.back());
        }
        for (const auto body : bodies) {
            const auto location = start(body);
            if (clang_getCursorKind(body) != CXCursor_CompoundStmt
                && location.file == file.path)
                add_finding(
                    state,
                    { "control.braces",
                      file.artifact,
                      file.relative.generic_string(),
                      location.line,
                      location.column,
                      "control_body",
                      analysis_support::to_string(
                          clang_getCursorKindSpelling(kind)
                      ),
                      "control-flow body has no braces",
                      { { "braced", false } },
                      { { "braced", true } } }
                );
        }
    }

    std::size_t
    effective_span(const source_file& file, unsigned begin, unsigned finish) {
        return static_cast<std::size_t>(std::distance(
            file.effective_lines.lower_bound(begin),
            file.effective_lines.upper_bound(finish)
        ));
    }

    struct referent {
        std::string id;
        fs::path root;
        fs::path stem;
        std::string artifact;
        fs::path header;
        json files = json::array();
        std::size_t physical = 0;
        std::size_t effective = 0;
    };

    fs::path within_role(const source_file& file) {
        const auto local = file.relative.lexically_relative(file.owner_root);
        auto part = local.begin();
        if (part == local.end())
            return {};
        fs::path result;
        for (++part; part != local.end(); ++part)
            result /= *part;
        return result;
    }

    std::string file_role(const source_file& file) {
        const auto local = file.relative.lexically_relative(file.owner_root);
        return local.empty() ? "" : local.begin()->string();
    }

    fs::path companion_stem(const source_file& file) {
        auto stem = within_role(file);
        stem.replace_extension();
        auto name = stem.filename().string();
        for (const auto& suffix : { "_tests", "_benchmarks", "_bench" }) {
            if ((file_role(file) == "tests" || file_role(file) == "benchmarks")
                && name.ends_with(suffix)) {
                name.resize(
                    name.size() - std::char_traits<char>::length(suffix)
                );
                break;
            }
        }
        return stem.parent_path() / name;
    }

    std::string referent_id(const source_file& file) {
        return (file.owner_root / companion_stem(file))
            .lexically_normal()
            .generic_string();
    }

    struct module {
        fs::path root;
        fs::path directory;
        std::map<std::string, std::vector<const referent*>> children;
        unsigned depth = 0;
    };

    double imbalance(const std::vector<double>& weights) {
        const double total
            = std::accumulate(weights.begin(), weights.end(), 0.0);
        if (weights.size() <= 1 || total == 0)
            return 0;
        double result = 1;
        for (const auto weight : weights) {
            if (weight > 0) {
                const auto proportion = weight / total;
                result += proportion * std::log(proportion)
                    / std::log(static_cast<double>(weights.size()));
            }
        }
        return std::clamp(result, 0.0, 1.0);
    }

} // namespace structure_support

using namespace structure_support;

void inspect_file_volume(inspection& state, CXTranslationUnit unit) {
    for (auto& [path, file] : state.files) {
        if (state.measured.contains(path) || !state.included.contains(path))
            continue;
        const auto handle = clang_getFile(unit, path.c_str());
        if (!handle)
            continue;
        if (file.contents.size() > std::numeric_limits<unsigned>::max()) {
            state.report.errors.push_back(
                file.relative.generic_string()
                + ": file is too large for Clang source offsets"
            );
            continue;
        }
        state.measured.insert(path);
        file.physical_lines = static_cast<unsigned>(
            std::count(file.contents.begin(), file.contents.end(), '\n')
        );
        if (!file.contents.empty() && file.contents.back() != '\n')
            ++file.physical_lines;
        token_list tokens(
            unit,
            clang_getRange(
                clang_getLocationForOffset(unit, handle, 0),
                clang_getLocationForOffset(
                    unit, handle, static_cast<unsigned>(file.contents.size())
                )
            )
        );
        for (unsigned index = 0; index < tokens.count; ++index) {
            if (clang_getTokenKind(tokens.values[index]) == CXToken_Comment)
                continue;
            const auto extent
                = clang_getTokenExtent(unit, tokens.values[index]);
            const auto begin = locate(clang_getRangeStart(extent));
            auto finish = locate(clang_getRangeEnd(extent));
            if (finish.column == 1 && finish.line > begin.line)
                --finish.line;
            for (unsigned line = begin.line; line > 0 && line <= finish.line;
                 ++line)
                file.effective_lines.insert(line);
        }
    }
}

void inspect_structure(
    inspection& state, CXCursor cursor, const source_file& file
) {
    const auto begin = start(cursor);
    const auto finish = end(cursor);
    if (begin.file != file.path || finish.file != file.path
        || finish.line < begin.line)
        return;
    // Macro bodies can have definition and expansion locations in different
    // scopes.
    CXFile expansion = nullptr;
    unsigned expansion_line = 0, expansion_column = 0;
    clang_getExpansionLocation(
        clang_getCursorLocation(cursor), &expansion, &expansion_line,
        &expansion_column, nullptr
    );
    const auto spelling = locate(clang_getCursorLocation(cursor));
    if (expansion_line != spelling.line || expansion_column != spelling.column)
        return;
    const auto kind = clang_getCursorKind(cursor);
    inspect_braces(state, cursor, file);
    const auto span = finish.line - begin.line + 1;
    const auto effective = effective_span(file, begin.line, finish.line);
    const auto name
        = analysis_support::to_string(clang_getCursorSpelling(cursor));
    const auto nested = children(cursor);
    if (kind == CXCursor_Namespace && name.empty()) {
        const auto declarations
            = std::count_if(nested.begin(), nested.end(), [](const auto child) {
                  return clang_isDeclaration(clang_getCursorKind(child));
              });
        add_finding(
            state,
            { "namespace.anonymous",
              file.artifact,
              file.relative.generic_string(),
              begin.line,
              begin.column,
              "namespace",
              "anonymous namespace",
              "anonymous namespace adds hidden translation-unit-local "
              "ownership",
              { { "anonymous", 1 },
                { "declarations", declarations },
                { "physical_lines", span },
                { "effective_lines", effective } },
              { { "anonymous", 0 } } }
        );
    }
    const bool lambda = kind == CXCursor_LambdaExpr;
    if (!lambda
        && (!ordinary_function(kind) || !clang_isCursorDefinition(cursor)))
        return;
    const auto body
        = std::find_if(nested.rbegin(), nested.rend(), [](const auto child) {
              return clang_getCursorKind(child) == CXCursor_CompoundStmt;
          });
    if (body == nested.rend())
        return;
    const auto key = file.artifact + "|" + file.relative.generic_string()
        + "|scope|" + std::to_string(begin.offset);
    if (!state.seen.insert(key).second)
        return;
    complexity metrics;
    measure_complexity(*body, 0, metrics);
    const auto entity = lambda ? "lambda" : name;
    const json measurements { { "physical_lines", span },
                              { "effective_lines", effective },
                              { "estimated_complexity", metrics.branches },
                              { "nesting", metrics.nesting } };
    state.report.scopes.push_back(
        { { "file", file.relative.generic_string() },
          { "artifact", file.artifact },
          { "line", begin.line },
          { "column", begin.column },
          { "entity", entity },
          { "kind", lambda ? "lambda" : "function" },
          { "measurements", measurements } }
    );
    const auto add = [&](const std::string& rule, const std::string& message,
                         const json& targets) {
        add_finding(
            state,
            { rule, file.artifact, file.relative.generic_string(), begin.line,
              begin.column, lambda ? "lambda" : "function", entity, message,
              measurements, targets }
        );
    };
    if (span > (lambda ? 5U : 60U))
        add(lambda ? "lambda.span" : "function.span",
            "definition exceeds the physical span target",
            { { "physical_lines", lambda ? 5 : 60 } });
    if (metrics.branches > (lambda ? 2U : 10U))
        add(lambda ? "lambda.complexity" : "function.complexity",
            "definition exceeds the estimated complexity target",
            { { "estimated_complexity", lambda ? 2 : 10 } });
    if (lambda && metrics.nesting > 1)
        add("lambda.nesting", "lambda exceeds the control-flow nesting target",
            { { "nesting", 1 } });
}

void finish_structure(inspection& state) {
    std::map<std::string, referent> referents;
    for (const auto& [path, file] : state.files) {
        static_cast<void>(path);
        const auto extension = file.relative.extension();
        if (file_role(file) == "include"
            && (extension == ".hpp" || extension == ".h" || extension == ".hh"
                || extension == ".hxx")) {
            const auto id = referent_id(file);
            referents.try_emplace(
                id,
                referent { id, file.owner_root, companion_stem(file),
                           file.artifact, file.relative }
            );
        }
    }
    for (auto& item : state.report.files) {
        const auto found = std::find_if(
            state.files.begin(), state.files.end(), [&](const auto& pair) {
                return pair.second.relative.generic_string()
                    == item.at("file").get<std::string>();
            }
        );
        if (found == state.files.end())
            continue;
        const auto& file = found->second;
        item["physical_lines"] = file.physical_lines;
        item["effective_lines"] = file.effective_lines.size();
        item["referent"] = nullptr;
        if (!state.measured.contains(file.path))
            state.report.errors.push_back(
                file.relative.generic_string()
                + ": source volume analysis did not run"
            );
        const auto referent_match = referents.find(referent_id(file));
        if (file.entry || referent_match == referents.end())
            continue;
        auto& ref = referent_match->second;
        item["referent"] = ref.id;
        const bool production = !file.test && file_role(file) != "tests"
            && file_role(file) != "benchmarks";
        ref.files.push_back(
            { { "file", file.relative.generic_string() },
              { "artifact", file.artifact },
              { "production", production } }
        );
        if (production) {
            ref.physical += file.physical_lines;
            ref.effective += file.effective_lines.size();
        }
    }
    std::map<std::string, module> modules;
    for (const auto& [id, ref] : referents) {
        state.report.referents.push_back(
            { { "id", id },
              { "root", ref.root.generic_string() },
              { "header", ref.header.generic_string() },
              { "files", ref.files },
              { "physical_lines", ref.physical },
              { "effective_lines", ref.effective } }
        );
        std::vector<fs::path> parts(ref.stem.begin(), ref.stem.end());
        fs::path directory;
        for (std::size_t index = 0; index < parts.size(); ++index) {
            const auto module_id
                = (ref.root / directory).lexically_normal().generic_string();
            auto [position, inserted] = modules.try_emplace(
                module_id, module { ref.root, directory, {}, 0 }
            );
            static_cast<void>(inserted);
            auto& node = position->second;
            node.children
                [(index + 1 == parts.size() ? "referent:" : "module:")
                 + parts[index].string()]
                    .push_back(&ref);
            node.depth = std::max(
                node.depth, static_cast<unsigned>(parts.size() - index)
            );
            directory /= parts[index];
        }
    }
    for (const auto& [id, node] : modules) {
        std::vector<double> counts, volumes;
        std::set<std::string> artifacts;
        for (const auto& [child, refs] : node.children) {
            static_cast<void>(child);
            counts.push_back(static_cast<double>(refs.size()));
            std::size_t effective = 0;
            for (const auto* ref : refs) {
                effective += ref->effective;
                artifacts.insert(ref->artifact);
            }
            volumes.push_back(static_cast<double>(effective));
        }
        const auto count = static_cast<std::size_t>(
            std::accumulate(counts.begin(), counts.end(), 0.0)
        );
        const auto breadth = counts.size();
        const auto base = std::max<std::size_t>(2, breadth);
        unsigned ideal = 1;
        for (std::size_t capacity = base; capacity < count; ++ideal)
            capacity = capacity > count / base ? count : capacity * base;
        const auto soft_width = static_cast<std::size_t>(
            std::ceil(std::sqrt(static_cast<double>(count)))
        );
        const double depth_excess = node.depth > ideal
            ? static_cast<double>(node.depth - ideal) / ideal
            : 0;
        const double width_excess = breadth > soft_width
            ? static_cast<double>(breadth - soft_width)
                / static_cast<double>(soft_width)
            : 0;
        const auto count_imbalance = imbalance(counts);
        const json measurements { { "referents", count },
                                  { "children", breadth },
                                  { "depth", node.depth },
                                  { "count_imbalance", count_imbalance },
                                  { "volume_imbalance", imbalance(volumes) },
                                  { "depth_excess", depth_excess },
                                  { "width_excess", width_excess },
                                  { "local_hint",
                                    count_imbalance / 2 + depth_excess / 3
                                        + width_excess / 6 } };
        const json targets { { "depth", ideal }, { "children", soft_width } };
        state.report.modules.push_back(
            { { "module", id },
              { "measurements", measurements },
              { "targets", targets },
              { "artifacts", artifacts } }
        );
        const auto path = (node.root / "include" / node.directory)
                              .lexically_normal()
                              .generic_string();
        const std::string artifact
            = artifacts.size() == 1 ? *artifacts.begin() : "";
        if (depth_excess > 0)
            add_finding(
                state,
                { "tree.depth", artifact, path, 1, 1, "module", id,
                  "referent-tree depth exceeds the soft target", measurements,
                  targets }
            );
        if (width_excess > 0)
            add_finding(
                state,
                { "tree.width", artifact, path, 1, 1, "module", id,
                  "referent-tree breadth exceeds the soft target", measurements,
                  targets }
            );
    }
}

} // namespace ecosystem::personal_support
