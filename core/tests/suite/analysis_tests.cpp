#include "test_support.hpp"
#include "test_cases.hpp"

namespace ecosystem_test_support {

void test_cli_check_naming_reports_identifier_overflow_and_honors_allowlist() {
    temp_dir root;
    write_sample_build_project(
        root.path(), "sample",
        "int before_widget_local_display_bytes_estimated = 0;\n"
        "int after_widget_local_display_bytes_estimated = 0;\n"
        "int this_identifier_is_far_too_long_for_policy = 0;\n"
        "int main() { return this_identifier_is_far_too_long_for_policy; }\n"
    );
    write_text(
        root.path() / "docs/naming_allowlist.txt",
        "before_widget_local_display_bytes_estimated\n"
        "after_widget_local_display_bytes_estimated\n"
    );

    const cli_result result = run_cli(root.path(), "check naming");
    require_true(
        result.exit_code == 0,
        "personal naming findings must succeed as advisory diagnostics"
    );
    require_contains(
        result.output, "this_identifier_is_far_too_long_for_policy",
        "ecos check naming must report the violating identifier"
    );
    require_not_contains(
        result.output, "before_widget_local_display_bytes_estimated",
        "ecos check naming must honor entries from docs/naming_allowlist.txt"
    );
}

void test_personal_naming_uses_declarations_and_exact_policy_boundaries() {
    temp_dir root;
    write_text(
        root.path() / "manifest.json",
        json(
            { { "id", "naming_sample" },
              { "description", "Naming acceptance" },
              { "facade", "core:app" },
              { "artifacts",
                json::array(
                    { { { "id", "core:app" },
                        { "kind", "exe" },
                        { "owns",
                          json::array({ "BadDirectory", "tests/names.cpp" }) },
                        { "entry", "src/main.cpp" } },
                      { { "id", "core:other" },
                        { "kind", "exe" },
                        { "owns", json::array() },
                        { "entry", "src/other_main.cpp" } } }
                ) } }
        ).dump(2)
    );
    write_text(
        root.path() / "include/BadDirectory/a_b_c_d_e.hpp",
        "#pragma once\nstruct named_type { int m_value; };\n"
    );
    write_text(
        root.path() / "src/main.cpp",
        "#include \"BadDirectory/a_b_c_d_e.hpp\"\n"
        "// comment_with_far_too_many_words_only\n"
        "const char *text = R\"tag(string_with_far_too_many_words_only)tag\";\n"
        "int a_b_c_d = 0;\nint a_b_c_d_e = 0;\n"
        "int abcdefghijklmnopqrstuvwxy = 0;\nint abcdefghijklmnopqrstuvwxyz = "
        "0;\n"
        "int _leading = 0;\nint trailing_ = 0;\nint camelName = 0;\n"
        "int operator_helper_with_many_words() { return 0; }\n"
        "int main() { return a_b_c_d_e + a_b_c_d_e; }\n"
    );
    write_text(
        root.path() / "tests/names.cpp",
        "int a_b_c_d_e_f_g = 0;\nint a_b_c_d_e_f_g_h = 0;\n"
        "int deliberatelylongtestsymbolwithoutacharacterbound = 0;\n"
    );
    write_text(root.path() / "src/other_main.cpp", "#error unselected owner\n");
    const auto result = run_engels_cli(root.path(), "check naming core:app");
    require_true(
        result.exit_code == 0,
        "advisory naming must analyze a selected owner:\n" + result.output
    );
    const auto path = root.path() / ".ecosystem/reports/naming.json";
    const auto report = json::parse(read_text(path));
    require_true(
        report.at("status") == "findings" && report.at("errors").empty(),
        "findings must remain separate from analysis failures"
    );
    const auto count = [&](const std::string& entity, const std::string& rule,
                           const std::string& kind = "symbol") {
        return std::count_if(
            report.at("findings").begin(), report.at("findings").end(),
            [&](const auto& finding) {
                return finding.at("entity") == entity
                    && finding.at("rule") == rule
                    && finding.at("entity_kind") == kind;
            }
        );
    };
    require_true(
        count("a_b_c_d", "naming.words") == 0
            && count("a_b_c_d_e", "naming.words") == 1,
        "word limits must use >4 and report the declaration once, not "
        "references"
    );
    require_true(
        count("operator_helper_with_many_words", "naming.words") == 1,
        "ordinary names beginning with operator must not be mistaken for "
        "overloaded operators"
    );
    require_true(
        count("abcdefghijklmnopqrstuvwxy", "naming.characters") == 0
            && count("abcdefghijklmnopqrstuvwxyz", "naming.characters") == 1,
        "character limit must use >25"
    );
    require_true(
        count("a_b_c_d_e_f_g", "naming.words") == 0
            && count("a_b_c_d_e_f_g_h", "naming.words") == 1,
        "test word limit must use >7"
    );
    require_true(
        count(
            "deliberatelylongtestsymbolwithoutacharacterbound",
            "naming.characters"
        ) == 0,
        "uncalibrated test character bounds must remain disabled"
    );
    for (const auto& [entity, rule] :
         std::vector<std::pair<std::string, std::string>> {
             { "_leading", "naming.leading_underscore" },
             { "trailing_", "naming.trailing_underscore" },
             { "m_value", "naming.scope_prefix" },
             { "camelName", "naming.snake_case" } })
        require_true(
            count(entity, rule) == 1,
            "required naming rule must identify its declaration"
        );
    require_true(
        count("BadDirectory", "naming.snake_case", "directory") == 1
            && count("a_b_c_d_e", "naming.words", "file") == 1,
        "filesystem policy must include owned file and directory names"
    );
    require_not_contains(
        report.dump(), "comment_with_far_too_many_words_only",
        "comments must not become symbols"
    );
    require_not_contains(
        report.dump(), "string_with_far_too_many_words_only",
        "raw strings must not become symbols"
    );
    require_not_contains(
        report.dump(), "other_main",
        "artifact filters must not widen to a sibling owner"
    );
    for (const auto& finding : report.at("findings")) {
        require_true(
            finding.at("authority") == "manifesto"
                && finding.at("enforcement") == "advisory"
                && finding.at("artifact") == "core:app"
                && finding.at("line").get<unsigned>() > 0,
            "findings must retain policy and location attribution"
        );
    }
    write_text(root.path() / "docs/naming_allowlist.txt", "a_b_c_d_e\n");
    require_true(
        run_engels_cli(root.path(), "check naming core:app").exit_code == 0,
        "sparse symbol exceptions must work"
    );
    const auto excepted = json::parse(read_text(path));
    require_true(
        std::none_of(
            excepted.at("findings").begin(), excepted.at("findings").end(),
            [](const auto& finding) {
                return finding.at("entity") == "a_b_c_d_e"
                    && finding.at("entity_kind") == "symbol";
            }
        ),
        "allowlist must suppress only the named symbol"
    );
    require_true(
        std::any_of(
            excepted.at("findings").begin(), excepted.at("findings").end(),
            [](const auto& finding) {
                return finding.at("entity") == "a_b_c_d_e"
                    && finding.at("entity_kind") == "file";
            }
        ),
        "symbol exceptions must not waive filesystem policy"
    );
    const auto before_invalid = read_text(path);
    require_true(
        run_engels_cli(root.path(), "check naming core:absent").exit_code == 2
            && read_text(path) == before_invalid,
        "unknown scopes must fail before replacing reports"
    );
}

void test_personal_naming_reports_analysis_failures_and_owned_headers() {
    temp_dir root;
    write_sample_build_project(
        root.path(), "sample",
        "#include \"external.hpp\"\nint main() { return dependencyName(); }\n"
    );
    write_text(
        root.path() / "include/external.hpp",
        "inline int dependencyName() { return 0; }\n"
    );
    auto result = run_engels_cli(root.path(), "check naming");
    require_true(
        result.exit_code == 0,
        "unowned declarations must stay outside personal naming:\n"
            + result.output
    );
    require_not_contains(
        result.output, "dependencyName",
        "included external declarations must not receive project naming "
        "findings"
    );
    auto authored = json::parse(read_text(root.path() / "manifest.json"));
    authored["artifacts"][0]["owns"] = json::array({ "api" });
    write_text(root.path() / "manifest.json", authored.dump(2));
    write_text(
        root.path() / "include/api.hpp",
        "struct public_type { int m_field; };\n"
    );
    write_text(
        root.path() / "include/external.hpp",
        "namespace dependency {\n#include \"api.hpp\"\n}\n"
    );
    write_text(
        root.path() / "src/main.cpp",
        "#include \"external.hpp\"\nint main() { return 0; }\n"
    );
    result = run_engels_cli(root.path(), "check naming");
    require_true(
        result.exit_code == 0,
        "owned headers inside dependency namespace contexts must remain "
        "analyzable"
    );
    require_contains(
        result.output, "m_field",
        "namespace ownership must not hide declarations in an included owned "
        "file"
    );
    write_text(
        root.path() / "src/main.cpp",
        "#include \"absent.hpp\"\nint main() { return 0; }\n"
    );
    result = run_engels_cli(root.path(), "check naming");
    require_true(
        result.exit_code == 5,
        "unparseable analysis inputs must fail the operation"
    );
    const auto report = json::parse(
        read_text(root.path() / ".ecosystem/reports/naming.json")
    );
    require_true(
        report.at("status") == "failed" && !report.at("errors").empty(),
        "analysis failure must persist distinctly from findings"
    );
    require_contains(
        result.output, "absent.hpp",
        "parse errors must identify the missing input"
    );

    const auto header_project = root.path() / "headers";
    write_text(
        header_project / "manifest.json",
        json(
            { { "id", "headers" },
              { "description", "Header-only naming" },
              { "facade", "core:lib" },
              { "artifacts",
                json::array(
                    { { { "id", "core:lib" },
                        { "kind", "interface_lib" },
                        { "owns", json::array({ "api" }) } } }
                ) } }
        ).dump(2)
    );
    write_text(
        header_project / "include/api.hpp",
        "#pragma once\nstruct public_type { int m_field; };\n"
    );
    result = run_engels_cli(header_project, "check naming");
    require_true(
        result.exit_code == 0,
        "standalone owned headers must be analyzable:\n" + result.output
    );
    require_contains(
        result.output, "m_field", "header-only declarations must be covered"
    );
    const auto loaded
        = ecosystem::load_manifest(header_project / "manifest.json");
    fs::remove(header_project / "include/api.hpp");
    const auto missing = ecosystem::analyze_personal_checks(
        *loaded.value, header_project, std::nullopt
    );
    require_true(
        !missing.errors.empty(),
        "disappearing selected inputs must not silently become a clean empty "
        "scope"
    );
    fs::create_directories(header_project / "docs/naming_allowlist.txt");
    const auto invalid_exceptions = ecosystem::analyze_personal_checks(
        *loaded.value, header_project, std::nullopt
    );
    require_contains(
        join_lines(invalid_exceptions.errors),
        "unable to read naming exceptions",
        "unreadable exception inputs must not silently relax the analysis "
        "contract"
    );
}

void test_personal_style_measures_control_bodies_and_local_scopes() {
    temp_dir root;
    write_sample_build_project(
        root.path(), "sample",
        "namespace { int helper() { return 1; } int value = 0; }\n"
        "int main() {\n"
        "  int x = 0;\n"
        "  if (x) ++x; else --x;\n"
        "  while (x) --x;\n"
        "  do ++x; while (x < 0);\n"
        "  for (int y = 0; y < 1; ++y) x += y;\n"
        "  int values[] = { 1, 2 };\n"
        "  for (const auto y : values) x += y;\n"
        "  switch (x) { case 0: break; default: break; }\n"
        "  if (x) { ++x; }\n"
        "  auto small = [](int y) { if (y) { return 1; } return 0; };\n"
        "  auto large = [](int y) {\n"
        "    if (y > 0) {\n"
        "      while (y > 1) { --y; }\n"
        "    }\n"
        "    if (y < 0) { return 1; }\n"
        "    return y;\n"
        "  };\n"
        "  auto outer = []() { auto inner = [](int y) { if (y) { while (y) { "
        "--y; } } return y; }; return inner(0); };\n"
        "  const char *text = R\"tag(if (x) x++; namespace { fake_code(); "
        "})tag\";\n"
        "  return small(0) + large(0) + outer() + helper();\n"
        "}\n"
    );
    auto source = read_text(root.path() / "src/main.cpp");
    source += "int boundary() {\n" + std::string(57, '\n') + "return 0;\n}\n";
    source += "int excess() {\n" + std::string(58, '\n') + "return 0;\n}\n";
    for (const int decisions : { 9, 10 }) {
        source += "int complexity_" + std::to_string(decisions) + "(int x) {\n";
        for (int index = 0; index < decisions; ++index)
            source += "if (x == " + std::to_string(index) + ") { ++x; }\n";
        source += "return x;\n}\n";
    }
    write_text(root.path() / "src/main.cpp", source);
    const auto result = run_engels_cli(root.path(), "check style");
    require_true(
        result.exit_code == 0,
        "personal structural findings must remain advisory:\n" + result.output
    );
    const auto report
        = json::parse(read_text(root.path() / ".ecosystem/reports/style.json"));
    const auto count = [&](const std::string& rule) {
        return std::count_if(
            report.at("findings").begin(), report.at("findings").end(),
            [&](const auto& finding) { return finding.at("rule") == rule; }
        );
    };
    require_true(
        count("control.braces") == 6,
        "if/else/while/do/for/range-for bodies must be checked without "
        "treating strings as code"
    );
    require_true(
        count("namespace.anonymous") == 1,
        "each anonymous namespace must produce one locality finding"
    );
    require_true(
        count("function.span") == 1 && count("function.complexity") == 1,
        "ordinary function span and branch estimates must honor the exact "
        "60/10 boundaries"
    );
    require_true(
        count("lambda.span") == 1 && count("lambda.complexity") == 2
            && count("lambda.nesting") == 2,
        "lambda span, branch estimate and nesting must retain independent "
        "bounds"
    );
    bool saw_outer = false;
    bool saw_small = false;
    for (const auto& scope : report.at("scopes")) {
        if (scope.at("kind") == "lambda" && scope.at("line") == 20
            && scope.at("column") == 16) {
            saw_outer = true;
            require_true(
                scope.at("measurements").at("estimated_complexity") == 1
                    && scope.at("measurements").at("nesting") == 0,
                "nested lambda decisions must not inflate their parent's "
                "complexity"
            );
        }
        if (scope.at("kind") == "lambda" && scope.at("line") == 12) {
            saw_small = true;
            require_true(
                scope.at("measurements").at("estimated_complexity") == 2
                    && scope.at("measurements").at("nesting") == 1,
                "lambda boundary values must be measured without excess "
                "findings"
            );
        }
    }
    require_true(
        saw_outer && saw_small,
        "scope measurements must identify both boundary and nested lambdas"
    );
    for (const auto& finding : report.at("findings")) {
        if (finding.at("rule") == "namespace.anonymous")
            require_true(
                finding.at("measurements").at("declarations") == 2
                    && finding.at("measurements").at("effective_lines") == 1,
                "namespace locality must include declaration and source-volume "
                "evidence"
            );
    }
    require_not_contains(
        report.dump(), "function.nesting",
        "undefined ordinary-function nesting thresholds must remain disabled"
    );
    require_contains(
        result.output, "largest owned files",
        "style must expose useful volume measurements in the terminal"
    );
}

void test_personal_style_indexes_referents_and_measures_volume_without_size_gates() {
    temp_dir root;
    write_text(
        root.path() / "manifest.json",
        json(
            { { "id", "tree_sample" },
              { "description", "Referent metrics" },
              { "facade", "core:lib" },
              { "artifacts",
                json::array(
                    { { { "id", "core:lib" },
                        { "kind", "static_lib" },
                        { "owns",
                          json::array(
                              { "math", "flat", "deep", "tests", "benchmarks",
                                "src/no_referent" }
                          ) } } }
                ) } }
        ).dump(2)
    );
    write_text(
        root.path() / "include/math.hpp",
        "#pragma once\n\n// header comment\nint meaning();\n"
    );
    write_text(root.path() / "include/math.tpp", "// template companion\n\n");
    write_text(
        root.path() / "src/math.cpp",
        "#include \"math.hpp\"\n/* comment\nstill comment */\nint meaning() { "
        "return 42; }\n"
    );
    write_text(
        root.path() / "tests/math_tests.cpp",
        "// test\nint test_math() { return 0; }\n"
    );
    write_text(
        root.path() / "benchmarks/math_benchmarks.cpp",
        "int bench_math() { return 0; }\n"
    );
    for (const auto* name : { "a", "b", "c" })
        write_text(
            root.path() / (std::string("include/flat/") + name + ".hpp"),
            std::string("#pragma once\nstruct ") + name + " {};\n"
        );
    write_text(
        root.path() / "include/deep/one/two/only.hpp",
        "#pragma once\nstruct only {};\n"
    );
    write_text(
        root.path() / "src/no_referent/unrelated.cpp",
        std::string(300, '\n') + "int unrelated() { return 0; }\n"
    );
    write_text(
        root.path() / "src/no_referent/raw.cpp",
        "const char *text = R\"(first\n// string text\nlast)\";\n"
    );
    const auto result = run_engels_cli(root.path(), "check style");
    require_true(
        result.exit_code == 0,
        "referent and volume findings must succeed:\n" + result.output
    );
    const auto report
        = json::parse(read_text(root.path() / ".ecosystem/reports/style.json"));
    require_true(
        report.at("referents").size() == 5,
        "only headers under include must form the reduced referent tree"
    );
    for (const auto& file : report.at("files")) {
        if (file.at("file") == "src/no_referent/raw.cpp")
            require_true(
                file.at("physical_lines") == 3
                    && file.at("effective_lines") == 3,
                "multiline literals must count as effective source even when "
                "their content resembles comments"
            );
    }
    bool saw_math = false;
    for (const auto& ref : report.at("referents")) {
        if (ref.at("id") == "math") {
            saw_math = true;
            require_true(
                ref.at("physical_lines") == 10
                    && ref.at("effective_lines") == 4,
                "production volume must include header/source/template "
                "companions and exclude comments, tests and benchmarks"
            );
            require_true(
                ref.at("files").size() == 5,
                "the referent index must retain test and benchmark "
                "relationships"
            );
        }
    }
    require_true(
        saw_math, "conventional companion stems must identify a referent"
    );
    bool saw_flat = false, saw_deep = false, saw_root = false;
    for (const auto& module : report.at("modules")) {
        const auto& metrics = module.at("measurements");
        if (module.at("module") == "flat") {
            saw_flat = true;
            require_true(
                metrics.at("referents") == 3 && metrics.at("children") == 3
                    && module.at("targets").at("children") == 2
                    && metrics.at("width_excess") == 0.5,
                "flatness must use the manifesto square-root target"
            );
        }
        if (module.at("module") == "deep") {
            saw_deep = true;
            require_true(
                metrics.at("referents") == 1 && metrics.at("depth") == 3
                    && module.at("targets").at("depth") == 1,
                "one-child chains must retain their depth evidence"
            );
        }
        if (module.at("module") == ".") {
            saw_root = true;
            require_true(
                metrics.at("referents") == 5 && metrics.at("depth") == 4
                    && module.at("targets").at("depth") == 2,
                "root depth must derive from descendant referents"
            );
        }
        require_not_contains(
            module.dump(), "no_referent",
            "non-referent subtrees must not create artificial modules"
        );
    }
    require_true(
        saw_flat && saw_deep && saw_root,
        "the index must expose module-level structural measurements"
    );
    require_true(
        std::any_of(
            report.at("findings").begin(), report.at("findings").end(),
            [](const auto& finding) {
                return finding.at("rule") == "tree.width";
            }
        ),
        "structural excess must produce an advisory hint"
    );
    require_true(
        std::none_of(
            report.at("findings").begin(), report.at("findings").end(),
            [](const auto& finding) {
                return finding.at("rule")
                    .template get<std::string>()
                    .starts_with("volume.");
            }
        ),
        "uncalibrated file-volume bounds must not become findings or gates"
    );
}

void test_cli_personal_style_preserves_workspace_scope_and_failure_status() {
    temp_dir root;
    write_sample_dual_run_workspace_project(root.path(), "alpha");
    write_sample_dual_run_workspace_project(root.path(), "beta");
    write_sample_dual_run_workspace_project(root.path(), "gamma");
    write_sample_workspace_config(root.path());
    write_text(root.path() / "broken/manifest.json", "invalid JSON\n");
    write_text(
        root.path() / "alpha/src/main.cpp",
        "int main() { int x = 0; if (x) ++x; return x; }\n"
    );
    write_text(
        root.path() / "alpha/src/cli_main.cpp", "#error unselected artifact\n"
    );
    auto result = run_engels_cli(root.path(), "check style");
    require_true(
        result.exit_code == 2
            && !fs::exists(root.path() / "alpha/.ecosystem/reports/style.json"),
        "workspace validity must be checked before selected reports are written"
    );
    result = run_engels_cli(
        root.path(), "check style alpha/app:app beta/tool:cli"
    );
    require_true(
        result.exit_code == 0,
        "multiple scoped workspace style checks must accept advisory "
        "findings:\n"
            + result.output
    );
    require_contains(
        result.output, "control.braces",
        "selected personal findings must remain visible"
    );
    const auto report_path
        = root.path() / "alpha/.ecosystem/reports/style.json";
    require_not_contains(
        read_text(report_path), "cli_main",
        "style must not widen selected artifact ownership"
    );
    result = run_engels_cli(root.path(), "check style --group core");
    require_true(
        result.exit_code == 5,
        "analysis failures in an explicitly selected group must fail the "
        "operation"
    );
    require_contains(
        result.output, "unselected artifact",
        "group analysis errors must retain source attribution"
    );
    fs::remove(report_path);
    fs::create_directory(report_path);
    result = run_engels_cli(root.path(), "check style alpha/app:app");
    require_true(
        result.exit_code == 5,
        "failure to persist a personal report must fail the operation"
    );
}

void test_clang_analysis_reports_unusable_inputs() {
    temp_dir root;
    write_cxx_report_project(root.path(), "reports");
    const auto manifest
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(manifest.value.has_value(), "fixture manifest must load");
    const auto analyze = [&] {
        return ecosystem::analyze_project_sources(
            *manifest.value, root.path(),
            ecosystem::artifact_ref { "core", "app" }, false, false
        );
    };
    fs::remove(root.path() / "src/main.cpp");
    auto report = analyze();
    require_true(
        !report.errors.empty()
            && ecosystem::to_json(report).at("status") == "failed",
        "missing selected inputs must fail analysis"
    );
    fs::create_directory(root.path() / "src/main.cpp");
    report = analyze();
    require_true(
        !report.errors.empty() && report.files_analyzed == 0,
        "non-file source paths must fail without pretending to analyze a "
        "translation unit"
    );
    fs::remove(root.path() / "src/main.cpp");
    write_text(root.path() / "src/main.cpp", "int main() { return 0; }\n");
    write_text(
        ecosystem::local_build_dir(root.path(), "debug")
            / "compile_commands.json",
        "invalid JSON\n"
    );
    report = analyze();
    require_true(
        !report.errors.empty() && report.files_analyzed == 0,
        "unreadable configured analysis inputs must not silently use guessed "
        "compiler arguments"
    );
}

void test_clang_analysis_runs_on_sample_project() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_manifest();
    manifest_value.components.front().modules.push_back("sample/core");
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );
    write_text(
        root.path() / "include/sample/core.hpp",
        "#pragma once\nint meaning();\n"
    );
    write_text(
        root.path() / "src/sample/core.cpp",
        "#include \"sample/core.hpp\"\nint meaning() { return 42; }\n"
    );
    write_text(root.path() / "src/main.cpp", "int main() { return 0; }\n");

    const ecosystem::cxx_analysis_report report
        = ecosystem::analyze_project_sources(
            manifest_value, root.path(), std::nullopt, false, false
        );
    require_true(
        report.errors.empty(),
        "Clang analysis should succeed on a simple project"
    );
    require_true(
        report.files_analyzed == 2,
        "analysis should include module and source-only files"
    );
    require_true(
        report.core_files == 1 && report.runtime_files == 1
            && report.test_files == 0 && report.benchmark_files == 0,
        "analysis should categorize module and runtime sources"
    );
    require_true(
        report.total_functions >= 2, "analysis should report structural counts"
    );
    require_true(
        !report.tests_included && !report.benchmarks_included,
        "analysis metadata should reflect disabled optional categories"
    );
}

void test_clang_analysis_skips_benchmarks_and_generated_sources_by_default() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_dependency_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );
    write_sample_dependency_project(root.path());
    write_text(
        root.path() / "tests/generated_auto.cpp", "this is not valid cxx\n"
    );

    const ecosystem::cxx_analysis_report report
        = ecosystem::analyze_project_sources(
            manifest_value, root.path(), std::nullopt, true, false
        );
    require_true(
        report.errors.empty(),
        "analysis should ignore undeclared generated sources"
    );
    require_true(
        report.files_analyzed == 4,
        "analysis should skip benchmark-only components by default"
    );
    require_true(
        report.core_files == 2 && report.runtime_files == 1
            && report.test_files == 1 && report.benchmark_files == 0,
        "analysis should keep declared test sources while excluding "
        "benchmark-only sources"
    );
    require_true(
        find_source_analysis(report, "tests/generated_auto.cpp") == nullptr,
        "undeclared generated sources must not be analyzed"
    );
    require_true(
        find_source_analysis(report, "benchmarks/bench_all.cpp") == nullptr,
        "benchmark-only sources must stay excluded by default"
    );
    require_true(
        find_source_analysis(report, "tests/test_main.cpp") != nullptr,
        "declared test sources must remain analyzable"
    );

    const ecosystem::component_analysis_summary* tests_summary
        = find_component_analysis_summary(report, "tests");
    require_true(
        tests_summary != nullptr && tests_summary->test_files == 1,
        "component summaries should retain test categorization"
    );
}

void test_clang_analysis_includes_benchmark_component_when_requested() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_dependency_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );
    write_sample_dependency_project(root.path());

    const ecosystem::cxx_analysis_report report
        = ecosystem::analyze_project_sources(
            manifest_value, root.path(),
            ecosystem::artifact_ref { "benchmarks", "bench" }, true, true
        );
    require_true(
        report.errors.empty(),
        "benchmark-only analysis should succeed when explicitly enabled"
    );
    require_true(
        report.files_analyzed == 1 && report.benchmark_files == 1,
        "benchmark-only analysis should include the selected benchmark "
        "source"
    );

    const ecosystem::source_analysis* benchmark_source
        = find_source_analysis(report, "benchmarks/bench_all.cpp");
    require_true(
        benchmark_source != nullptr
            && benchmark_source->component_id == "benchmarks"
            && benchmark_source->category == "benchmarks",
        "benchmark source metadata should report the owning component and "
        "category"
    );

    const ecosystem::component_analysis_summary* benchmark_summary
        = find_component_analysis_summary(report, "benchmarks");
    require_true(
        benchmark_summary != nullptr && benchmark_summary->files_analyzed == 1
            && benchmark_summary->benchmark_files == 1,
        "component summaries should include benchmark-only analysis state"
    );
}

} // namespace ecosystem_test_support
