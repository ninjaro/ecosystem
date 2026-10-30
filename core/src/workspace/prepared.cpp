#include "analysis/tidy.hpp"
#include "command_internal.hpp"
#include "packages/package_cache.hpp"
#include "prepared_internal.hpp"
#include "release/internal.hpp"
#include "workspace/read_commands.hpp"
#include "workspace/source_dependencies.hpp"
#include "workspace/sync.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ecosystem::prepared_detail {

void require(const bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

void regular(const fs::path& path) {
    release_detail::reject_aliases(path);
    require(
        fs::is_regular_file(path), "missing/nonregular input: " + path.string()
    );
}

std::string digest(const fs::path& path) {
    if (find_command_path("sha256sum").empty())
        throw unavailable("SHA256 verification requires sha256sum");
    std::string error;
    const auto result = prepared_file_sha256(path, &error);
    require(!result.empty() && error.empty(), error);
    return result;
}

namespace {

    void keys(
        const json& object, const string_list& required,
        const string_list& optional = {}
    ) {
        require(object.is_object(), "expected a JSON object");
        for (const auto& key : required)
            require(object.contains(key), "missing input field: " + key);
        for (const auto& [key, unused] : object.items()) {
            (void)unused;
            require(
                std::ranges::find(required, key) != required.end()
                    || std::ranges::find(optional, key) != optional.end(),
                "unsupported input field: " + key
            );
        }
    }

    json read_input(const fs::path& path) {
        regular(path);
        std::ifstream stream(path);
        std::vector<std::set<std::string>> members;
        return json::parse(
            stream, [&](int, json::parse_event_t event, json& value) {
                if (event == json::parse_event_t::object_start)
                    members.emplace_back();
                else if (event == json::parse_event_t::key)
                    require(
                        members.back().insert(value.get<std::string>()).second,
                        "duplicate JSON key"
                    );
                else if (event == json::parse_event_t::object_end)
                    members.pop_back();
                return true;
            }
        );
    }

    void load(context& value, const std::optional<artifact_ref>& requested) {
        release_detail::reject_aliases(value.root);
        const auto relative = release_detail::contract_path(
            value.input_path.lexically_relative(value.root)
        );
        const fs::path relative_path(relative);
        require(
            relative.starts_with(".ecosystem/prerelease/attempts/")
                && std::distance(relative_path.begin(), relative_path.end())
                    == 6
                && value.input_path.filename() == "input.json"
                && value.input_path.parent_path().filename() == "prepared",
            "input must be an existing Marx attempt's prepared/input.json"
        );
        value.payload = value.input_path.parent_path() / "payload";
        value.input = read_input(value.input_path);
        value.input_digest = digest(value.input_path);
        keys(
            value.input,
            { "input_version", "project", "product", "distribution",
              "provenance" },
            { "version_base" }
        );
        require(
            value.input.at("input_version").is_number_integer()
                && value.input.at("input_version") == 1,
            "unsupported prepared input version"
        );
        keys(value.input.at("project"), { "root", "id" });
        keys(
            value.input.at("product"),
            { "artifact", "facade_artifact", "package_name", "description" }
        );
        keys(
            value.input.at("distribution"), { "complete", "artifacts", "files" }
        );
        keys(
            value.input.at("provenance"), { "manifest_sha256" },
            { "source_revision", "evidence" }
        );
        regular(value.root / "manifest.json");
        const auto loaded = load_manifest(value.root / "manifest.json");
        require(
            loaded.errors.empty() && loaded.value.has_value(),
            "current manifest is invalid: "
                + command_support::join_strings(loaded.errors, "\n")
        );
        value.project = *loaded.value;
        value.manifest_digest = digest(value.root / "manifest.json");
        require(
            value.input.at("provenance").at("manifest_sha256")
                == value.manifest_digest,
            "manifest changed since preparation"
        );
        require(
            value.input.at("project").at("root") == value.root.generic_string()
                && value.input.at("project").at("id") == value.project.id,
            "project root/identity mismatch"
        );
        const auto selected = resolve_artifact(value.project, requested);
        require(
            selected.has_value()
                && selected->artifact_value->kind != "interface_lib",
            "unknown/non-distributable selected product"
        );
        value.selected = selected->ref;
        const auto& product = value.input.at("product");
        require(
            product.at("artifact") == format_artifact_ref(value.selected)
                && product.at("facade_artifact")
                    == value.project.facade_entry_artifact
                && product.at("package_name")
                    == prerelease_package_name(
                        value.project, value.selected, *selected->artifact_value
                    )
                && product.at("description")
                    == release_detail::package_description(
                        value.project, *selected
                    ),
            "product/facade/package identity mismatch"
        );
        require(
            value.input.at("distribution").at("complete").is_boolean()
                && value.input.at("distribution").at("complete") == true,
            "incomplete distribution declaration"
        );
        value.revision = release_detail::clean_revision(value.root);
        if (value.input.at("provenance").contains("source_revision"))
            require(
                !value.revision.empty()
                    && value.input.at("provenance").at("source_revision")
                        == value.revision,
                "stale or unverifiable clean source revision"
            );
        if (value.input.at("provenance").contains("evidence")) {
            const auto reference = release_detail::contract_path(
                value.input.at("provenance").at("evidence").get<std::string>()
            );
            regular(value.root / reference);
        }
        if (!value.project.version.empty())
            require(
                value.input.contains("version_base")
                    && value.input.at("version_base") == value.project.version,
                "prepared version conflicts with manifest version"
            );
        if (value.input.contains("version_base")) {
            require(
                value.input.at("version_base").is_string()
                    && std::regex_match(
                        value.input.at("version_base").get<std::string>(),
                        release_detail::semver_pattern
                    ),
                "prepared version must use major.minor.patch form"
            );
        }
    }

    std::map<fs::path, std::string> observed_inputs(const context& value) {
        std::set<fs::path> files;
        for (const auto& path :
             format_candidate_files(value.project, value.root, std::nullopt))
            files.insert(path);
        for (const auto* name :
             { "manifest.json", "CMakeLists.txt", ".clang-format",
               ".clang-tidy", "ecosystem.local.json" })
            if (fs::exists(value.root / name))
                files.insert(value.root / name);
        const auto build = local_build_dir(value.root, "release");
        for (const auto* name : { "CMakeCache.txt", "compile_commands.json",
                                  "CTestTestfile.cmake", "CTestCustom.cmake" })
            if (fs::exists(build / name))
                files.insert(build / name);
        for (const auto& [unused, expected] : value.expected) {
            (void)unused;
            files.insert(fs::canonical(expected.source));
        }
        files.insert(local_developer_source_dir(value.root) / "CMakeLists.txt");
        std::map<fs::path, std::string> result;
        for (const auto& file : files)
            result.emplace(file, digest(file));
        return result;
    }

    json project_check(const context& value, const std::string& profile) {
        std::ostringstream out;
        std::ostringstream error;
        const auto status = run_check(
            value.root, value.project, profile, std::nullopt, std::nullopt, out,
            error
        );
        return { { "status",
                   status == command_error::ok ? "passed"
                       : status == command_error::missing_local_tooling
                       ? "not_run"
                       : "failed" },
                 { "exit_code", static_cast<int>(status) },
                 { "output", out.str() },
                 { "diagnostics", error.str() } };
    }

    json tidy(const context& value) {
        const auto selected = distribution_build_manifest(
            select_local_features(value.root, value.project, value.selected),
            value.selected
        );
        const auto report = run_tidy_check(
            selected, value.root, std::nullopt, false, false, "release"
        );
        if (report.clang_tidy_configuration_error)
            return { { "status", "failed" },
                     { "reason", "invalid required Tidy configuration" },
                     { "details", to_json(report) } };
        if (!report.clang_tidy_used)
            return { { "status", "not_run" },
                     { "reason", report.clang_tidy_skip_reason },
                     { "details", to_json(report) } };
        const bool passed = !report.clang_tidy_configuration_error
            && report.clang_tidy_exit_code == 0
            && report.analysis.errors.empty()
            && report.analysis.total_errors == 0
            && report.analysis.total_warnings == 0;
        return { { "status", passed ? "passed" : "failed" },
                 { "scope",
                   "current selected production sources using existing Release "
                   "compilation database; no configure/build" },
                 { "details", to_json(report) } };
    }

    json release_context(const context& value) {
        const auto cache_path = local_build_cache_path(value.root, "release");
        if (fs::exists(cache_path))
            regular(cache_path);
        const auto cache = load_cmake_cache(cache_path);
        if (!cache)
            return { { "status", "not_run" },
                     { "reason",
                       "existing Release configuration is unavailable; "
                       "verifier will not configure it" } };
        const auto get = [&](const std::string& key) {
            const auto found = cache->entries.find(key);
            return found == cache->entries.end() ? std::string()
                                                 : found->second;
        };
        require(
            get("CMAKE_BUILD_TYPE") == "Release",
            "existing build is not Release"
        );
        require(
            fs::path(get("ECOSYSTEM_PROJECT_ROOT")) == value.root,
            "Release configuration belongs to a different project root"
        );
        require(
            get("ECOSYSTEM_RESOLVED_CXX_STANDARD")
                == std::to_string(value.project.cpp_standard),
            "Release language-standard metadata is stale"
        );
        const auto source = local_developer_source_dir(value.root);
        require(
            fs::path(get("CMAKE_HOME_DIRECTORY")) == source,
            "Release configuration is not the canonical actor-owned source "
            "graph"
        );
        regular(source / "CMakeLists.txt");
        std::string error;
        const auto current = read_text_file(source / "CMakeLists.txt", &error);
        require(error.empty(), error);
        const auto selected
            = select_local_features(value.root, value.project, value.selected);
        const auto full
            = select_local_features(value.root, value.project, std::nullopt);
        require(
            current
                    == generate_developer_cmakelists(
                        distribution_build_manifest(selected, value.selected),
                        value.root
                    )
                || current == generate_developer_cmakelists(full, value.root),
            "existing Release graph is stale or inconsistent; prepare it "
            "through Marx"
        );
        return { { "status", "passed" },
                 { "cpp_standard", get("ECOSYSTEM_RESOLVED_CXX_STANDARD") },
                 { "compiler", get("ECOSYSTEM_RESOLVED_CXX_COMPILER") },
                 { "source_graph_sha256", digest(source / "CMakeLists.txt") },
                 { "scope",
                   "observed canonical configuration; not proof of a fresh "
                   "successful compilation" } };
    }

    json tests(context& value) {
        const auto targets = command_support::collect_test_targets(
            value.project, std::nullopt
        );
        if (targets.empty())
            return { { "status", "not_applicable" },
                     { "reason", "manifest has no runnable native tests" } };
        const auto tool = find_command_path("ctest");
        const auto build = local_build_dir(value.root, "release");
        if (tool.empty() || !fs::is_regular_file(build / "CTestTestfile.cmake"))
            return { { "status", "not_run" },
                     { "reason",
                       "Release CTest graph unavailable; verifier does not "
                       "configure or build it" } };
        const auto regex = command_support::ctest_regex_for(targets);
        const string_list query { tool, "--show-only=json-v1", "-R", regex };
        const auto listed = capture_command_result(query, build);
        require(
            listed.exit_code == 0, "CTest discovery failed: " + listed.output
        );
        const auto listing = json::parse(listed.output);
        const auto& selected = listing.at("tests");
        std::set<std::string> required;
        for (const auto& target : targets)
            required.insert(target.test_name);
        std::set<std::string> actual;
        json binaries = json::array();
        for (const auto& test : selected) {
            actual.insert(test.at("name").get<std::string>());
            if (!test.contains("command") || test.at("command").empty())
                return { { "status", "not_run" },
                         { "reason",
                           "required Release test executable is unavailable" },
                         { "discovered", listing } };
            const fs::path binary(test.at("command").at(0).get<std::string>());
            if (!fs::exists(binary))
                return { { "status", "not_run" },
                         { "reason",
                           "required Release test executable is missing: "
                               + binary.string() } };
            regular(binary);
            require(
                release_detail::path_within(binary, build),
                "CTest executable escapes its existing Release build"
            );
            binaries.push_back(
                { { "name", test.at("name") },
                  { "path", binary.string() },
                  { "sha256", digest(binary) } }
            );
        }
        if (actual != required)
            return { { "status", "not_run" },
                     { "reason",
                       "existing Release test graph does not contain every "
                       "manifest-declared test" },
                     { "discovered", listing } };
        const auto python = find_command_path("python3");
        const auto supported
            = capture_command_result({ tool, "--help" }, build);
        if (python.empty()
            || supported.output.find("--output-junit") == std::string::npos)
            return { { "status", "not_run" },
                     { "reason",
                       "counted native verification requires Python 3 and "
                       "CTest --output-junit (3.21+)" } };
        const auto junit = value.work / "native-tests.xml";
        const string_list command {
            tool,  "--output-on-failure", "--no-tests=error", "-R",
            regex, "--output-junit",      junit.string()
        };
        const auto result = capture_command_result(command, build);
        const string_list parse {
            python, "-c",
            "import json,sys,xml.etree.ElementTree as E; "
            "r=E.parse(sys.argv[1]).getroot(); "
            "print(json.dumps([{'name':t.attrib['name'], 'status':"
            "'not_run' if t.find('skipped') is not None else "
            "'failed' if t.find('failure') is not None or t.find('error') is "
            "not None else "
            "'passed'} for t in r.iter('testcase')]))",
            junit.string()
        };
        const auto parsed = capture_command_result(parse, value.root);
        require(
            parsed.exit_code == 0,
            "missing/invalid native test results: " + result.output
                + parsed.output
        );
        const auto outcomes = json::parse(parsed.output);
        std::set<std::string> measured;
        bool skipped = false;
        bool failed = result.exit_code != 0;
        for (const auto& outcome : outcomes) {
            require(
                measured.insert(outcome.at("name").get<std::string>()).second,
                "duplicate native test result"
            );
            skipped = skipped || outcome.at("status") == "not_run";
            failed = failed || outcome.at("status") == "failed";
        }
        require(
            measured == required,
            "native results do not cover every required test"
        );
        for (const auto& binary : binaries)
            require(
                digest(binary.at("path").get<std::string>())
                    == binary.at("sha256").get<std::string>(),
                "test executable changed during verification"
            );
        return { { "status",
                   failed        ? "failed"
                       : skipped ? "not_run"
                                 : "passed" },
                 { "reason",
                   failed        ? "native test failure"
                       : skipped ? "required tests were skipped"
                                 : "all required existing native tests ran" },
                 { "test_count", outcomes.size() },
                 { "command", command },
                 { "results_command", parse },
                 { "outcomes", outcomes },
                 { "discovery_command", query },
                 { "exit_code", result.exit_code },
                 { "output", result.output },
                 { "tests", binaries },
                 { "scope",
                   "existing Release native test executables; not "
                   "assembled/installed package tests or proof of build "
                   "freshness" } };
    }

} // namespace
} // namespace ecosystem::prepared_detail

namespace ecosystem {

prepared_verification verify_prepared_distribution(
    const fs::path& project_root, const fs::path& input_file,
    const std::optional<artifact_ref>& requested_artifact
) {
    using namespace prepared_detail;
    prepared_verification result;
    result.report
        = { { "result_version", 1 },
            { "kind", "engels-prepared-distribution" },
            { "status", "failed" },
            { "prepared_distribution_verified", false },
            { "release_ready", false },
            { "publication_authorized", false },
            { "trust",
              "descriptive local observations, not authenticated or "
              "tamper-proof authorization" },
            { "checks", json::array() },
            { "files", json::array() },
            { "limitations",
              { "No compilation or source-to-binary build attestation is "
                "performed.",
                "No final archive, installation, target-native or REL-01 "
                "released-representation acceptance is established.",
                "Checks use a locally trusted checkout/tools; concurrent "
                "hostile filesystem changes are outside this contract.",
                "Caller-supplied evidence and passed fields cannot authorize a "
                "release." } } };
    const auto phase = [&](const std::string& name, const std::string& scope,
                           const auto& action) {
        const auto start = std::chrono::steady_clock::now();
        json row { { "name", name }, { "scope", scope } };
        try {
            row.update(action());
            if (!row.contains("status"))
                row["status"] = "passed";
        } catch (const unavailable& error) {
            row["status"] = "not_run";
            row["reason"] = error.what();
        } catch (const std::exception& error) {
            row["status"] = "failed";
            row["reason"] = error.what();
        }
        row["seconds"] = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - start
        )
                             .count();
        const bool passed = row.at("status") == "passed"
            || row.at("status") == "not_applicable";
        result.report["checks"].push_back(row);
        return passed;
    };
    context value;
    value.root = fs::absolute(project_root).lexically_normal();
    value.input_path = fs::absolute(value.root / input_file).lexically_normal();
    const auto finish = [&] {
        if (result.report.at("status") == "failed") {
            const bool failed = std::ranges::any_of(
                result.report.at("checks"),
                [](const auto& row) { return row.at("status") == "failed"; }
            );
            if (!failed) {
                result.report["status"] = "incomplete";
                result.status = command_error::unsupported_by_manifest;
            }
        }
        if (!value.measurements.empty())
            result.report["files"] = value.measurements;
        result.report["manifest_sha256"] = value.manifest_digest;
        result.report["input_sha256"] = value.input_digest;
        result.report["current_project"]
            = { { "root", value.root.generic_string() },
                { "id", value.project.id } };
        if (value.input.is_object()) {
            result.report["declared_project"]
                = value.input.value("project", json());
            result.report["declared_product"]
                = value.input.value("product", json());
        }
        for (const auto* name :
             { "handoff-identity", "prepared-distribution", "repository",
               "existing-release-context", "verification-inputs", "format",
               "tidy", "native-tests", "final-revalidation" }) {
            const bool recorded = std::ranges::any_of(
                result.report.at("checks"),
                [&](const auto& row) { return row.at("name") == name; }
            );
            if (!recorded)
                result.report["checks"].push_back(
                    { { "name", name },
                      { "status", "not_run" },
                      { "reason", "blocked by an unsuccessful prerequisite" } }
                );
        }
        for (const std::string name :
             { "source-build", "coverage", "sanitizers",
               "managed-platform-checks" }) {
            const bool applicable = name == "source-build"
                || (name == "managed-platform-checks"
                        ? command_support::project_supports_java_check(
                              value.root, value.project
                          )
                        : has_tests_enabled(value.project));
            result.report["checks"].push_back(
                { { "name", name },
                  { "status", applicable ? "not_run" : "not_applicable" },
                  { "reason",
                    applicable
                        ? "requires separate upstream build/instrumentation or "
                          "platform verification; assertions are not imported"
                        : "no corresponding declared executable test or "
                          "managed surface" } }
            );
        }
        for (const auto* name :
             { "REL-01-released-representation", "package-contents",
               "installed-product-tests", "target-native-installation" })
            result.report["checks"].push_back(
                { { "name", name },
                  { "status", "deferred" },
                  { "reason",
                    "requires assembled package, installation or target-native "
                    "environment" } }
            );
        result.report["all_release_gates_passed"] = false;
        result.report["commands"] = value.commands;
        return result;
    };
    if (!phase("handoff-identity", "current project/manifest", [&] {
            load(value, requested_artifact);
            result.report["project"] = value.input.at("project");
            result.report["product"] = value.input.at("product");
            result.report["manifest_sha256"] = value.manifest_digest;
            result.report["input_sha256"] = value.input_digest;
            result.report["source_revision"] = {
                { "current_clean_revision", value.revision },
                { "recorded",
                  value.input.at("provenance").value("source_revision", "") },
                { "status",
                  value.input.at("provenance").contains("source_revision")
                      ? "matched"
                      : "unavailable_in_input" }
            };
            return json { { "supplied_evidence",
                            "not used as verification or authorization" } };
        }))
        return finish();
    if (!phase(
            "prepared-distribution",
            "prepared bytes and authoritative distribution closure", [&] {
                const auto directory
                    = value.root / ".ecosystem/reports/prepared";
                release_detail::reject_aliases(directory);
                fs::create_directories(directory);
                value.work = directory
                    / std::to_string(
                                 std::chrono::steady_clock::now()
                                     .time_since_epoch()
                                     .count()
                    );
                require(
                    fs::create_directory(value.work),
                    "verification directory collision"
                );
                collect_inventory(value);
                result.report["files"] = check_inventory(value);
                result.report["prepared_distribution_verified"] = true;
                return json {
                    { "artifacts",
                      value.input.at("distribution").at("artifacts") },
                    { "files", value.expected.size() }
                };
            }
        ))
        return finish();
    if (!phase(
            "existing-release-context",
            "current canonical Release configuration",
            [&] { return release_context(value); }
        ))
        return finish();
    std::map<fs::path, std::string> before;
    if (!phase(
            "verification-inputs", "current source/reference observations",
            [&] {
                before = observed_inputs(value);
                result.report["observed_project_inputs"] = json::array();
                for (const auto& [path, hash] : before)
                    result.report["observed_project_inputs"].push_back(
                        { { "path",
                            path.lexically_relative(value.root)
                                .generic_string() },
                          { "sha256", hash } }
                    );
                return json { { "files", before.size() } };
            }
        ))
        return finish();
    bool complete = phase("repository", "current project", [&] {
        return project_check(value, "repo");
    });
    complete = phase(
                   "format", "current project sources",
                   [&] { return project_check(value, "format"); }
               )
        && complete;
    complete = phase(
                   "tidy", "current selected production sources",
                   [&] { return tidy(value); }
               )
        && complete;
    complete = phase(
                   "native-tests", "existing Release test executables",
                   [&] { return tests(value); }
               )
        && complete;
    complete
        = phase(
              "final-revalidation",
              "same input, manifest, reference and prepared bytes",
              [&] {
                  require(
                      observed_inputs(value) == before,
                      "project or reference inputs changed during checks"
                  );
                  require(
                      digest(value.input_path) == value.input_digest
                          && digest(value.root / "manifest.json")
                              == value.manifest_digest,
                      "handoff/manifest changed during checks"
                  );
                  require(
                      release_detail::clean_revision(value.root)
                          == value.revision,
                      "source revision changed during checks"
                  );
                  for (const auto& file : result.report.at("files")) {
                      const auto path
                          = value.root / file.at("source").get<std::string>();
                      regular(path);
                      require(
                          digest(path) == file.at("sha256").get<std::string>()
                              && fs::file_size(path)
                                  == file.at("size").get<std::uintmax_t>(),
                          "prepared bytes changed during checks"
                      );
                      require(
                          static_cast<unsigned int>(
                              fs::status(path).permissions() & fs::perms::all
                          ) == file.at("permissions").get<unsigned int>(),
                          "prepared permissions changed during checks"
                      );
                  }
                  return json::object();
              }
          )
        && complete;
    bool failed = false;
    for (const auto& row : result.report.at("checks"))
        failed = failed || row.at("status") == "failed";
    result.status = complete ? command_error::ok
        : failed             ? command_error::task_failed
                             : command_error::unsupported_by_manifest;
    result.report["status"] = complete ? "prepared_checks_passed"
        : failed                       ? "failed"
                                       : "incomplete";
    return finish();
}

} // namespace ecosystem
