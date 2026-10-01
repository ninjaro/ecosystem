#include "workspace/changes.hpp"

#include "workspace/project.hpp"
#include "workspace/tooling.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ecosystem {
namespace {
    namespace fs = std::filesystem;
    constexpr std::size_t max_record_bytes = 128U * 1024U;
    using names = std::set<std::string>;

    void require(bool condition, const std::string& reason) {
        if (!condition)
            throw std::runtime_error(reason);
    }

    std::string git(const fs::path& root, const string_list& arguments) {
        string_list command { "git", "--no-replace-objects", "-c",
                              "core.fsmonitor=false" };
        command.insert(command.end(), arguments.begin(), arguments.end());
        const auto result = capture_command_result(
            command, root, { { "GIT_OPTIONAL_LOCKS", "0" } }
        );
        require(result.exit_code == 0, "git-validation-failed");
        return result.output;
    }

    std::string git_line(const fs::path& root, const string_list& arguments) {
        auto result = git(root, arguments);
        if (!result.empty() && result.back() == '\n')
            result.pop_back();
        return result;
    }

    bool commit_id(const json& value) {
        if (!value.is_string())
            return false;
        const auto text = value.get<std::string>();
        return text.size() == 40U && text != std::string(40U, '0')
            && std::all_of(text.begin(), text.end(), [](char ch) {
                   return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
               });
    }

    bool safe_relative(const std::string& name) {
        if (name.empty() || name.find('\0') != std::string::npos)
            return false;
        const fs::path path(name);
        return !path.is_absolute() && path.generic_string() == name
            && path.lexically_normal().generic_string() == name
            && std::none_of(path.begin(), path.end(), [](const fs::path& part) {
                   return part == "." || part == ".." || part.empty();
               });
    }

    bool beneath(const fs::path& scope, const fs::path& file) {
        const auto relative = file.lexically_relative(scope);
        return !relative.empty() && !relative.is_absolute()
            && *relative.begin() != "..";
    }

    string_list nul_fields(const std::string& data) {
        string_list result;
        std::size_t start = 0;
        while (start < data.size()) {
            const auto end = data.find('\0', start);
            require(end != std::string::npos, "invalid-git-delta");
            result.push_back(data.substr(start, end - start));
            start = end + 1U;
        }
        return result;
    }

    // Independently validate the transported list before local policy uses it.
    // Renames normalize to delete/add, avoiding Git rename-heuristic
    // differences.
    std::map<std::string, std::string>
    validate_delta(const fs::path& root, const json& input) {
        require(
            input.at("schema") == 1 && input.at("complete") == true,
            "incomplete-change-record"
        );
        for (const auto* key :
             { "checkout", "event_commit", "base", "head", "comparison_base" })
            require(commit_id(input.at(key)), "invalid-change-revision");
        const auto checkout = input.at("checkout").get<std::string>();
        const auto base = input.at("base").get<std::string>();
        const auto head = input.at("head").get<std::string>();
        const auto comparison = input.at("comparison_base").get<std::string>();
        require(
            input.at("event_commit") == checkout
                && git_line(root, { "rev-parse", "--verify", "HEAD" })
                    == checkout,
            "checkout-mismatch"
        );
        require(
            git_line(root, { "rev-parse", "--show-prefix" }).empty(),
            "checkout-not-root"
        );
        require(
            git_line(root, { "rev-parse", "--is-shallow-repository" })
                == "false",
            "shallow-history"
        );
        require(
            git(root,
                { "status", "--porcelain=v1", "-z", "--untracked-files=normal",
                  "--ignore-submodules=none" })
                .empty(),
            "dirty-checkout"
        );
        for (const auto& revision : { base, head, comparison })
            require(
                git_line(
                    root, { "rev-parse", "--verify", revision + "^{commit}" }
                ) == revision,
                "invalid-change-revision"
            );
        if (input.at("event") == "push") {
            require(
                checkout == head && comparison == base,
                "push-comparison-mismatch"
            );
            git(root, { "merge-base", "--is-ancestor", base, head });
        } else {
            require(
                input.at("event") == "pull_request", "unsupported-change-event"
            );
            require(
                checkout == head
                    || git_line(root, { "show", "-s", "--format=%P", checkout })
                        == base + " " + head,
                "pr-checkout-mismatch"
            );
            require(
                git_line(root, { "merge-base", "--all", base, head })
                    == comparison,
                "merge-base-mismatch"
            );
        }
        const auto& entries = input.at("changes");
        require(
            entries.is_array() && entries.size() <= 2000U, "invalid-change-list"
        );
        std::map<std::string, std::string> expected;
        const auto add = [&](const json& path, const std::string& status) {
            require(path.is_string(), "invalid-change-path");
            const auto name = path.get<std::string>();
            require(safe_relative(name), "invalid-change-path");
            require(
                expected.emplace(name, status).second, "duplicate-change-path"
            );
        };
        for (const auto& entry : entries) {
            const auto status = entry.at("status").get<std::string>();
            if (status == "R") {
                add(entry.at("previous_path"), "D");
                add(entry.at("path"), "A");
            } else {
                require(
                    status == "A" || status == "D" || status == "M"
                        || status == "T",
                    "invalid-change-status"
                );
                require(
                    !entry.contains("previous_path"), "invalid-change-status"
                );
                add(entry.at("path"), status);
            }
        }
        require(
            input.at("paths").is_array()
                && input.at("paths").size() == expected.size()
                && expected.size() <= 2000U,
            "change-paths-mismatch"
        );
        names supplied;
        for (const auto& path : input.at("paths")) {
            require(
                path.is_string() && expected.contains(path.get<std::string>()),
                "change-paths-mismatch"
            );
            supplied.insert(path.get<std::string>());
        }
        require(supplied.size() == expected.size(), "change-paths-mismatch");
        const auto actual = nul_fields(
            git(root,
                { "diff", "--no-ext-diff", "--no-textconv", "--no-relative",
                  "--no-renames", "--ignore-submodules=none", "--name-status",
                  "-z", comparison, head, "--" })
        );
        require(actual.size() == expected.size() * 2U, "git-delta-mismatch");
        std::map<std::string, std::string> observed;
        for (std::size_t index = 0; index < actual.size(); index += 2U)
            observed.emplace(actual[index + 1U], actual[index]);
        require(observed == expected, "git-delta-mismatch");
        return expected;
    }

    bool cxx_path(const fs::path& path) {
        static const names extensions { ".cpp", ".cc", ".cxx", ".c",  ".hpp",
                                        ".h",   ".hh", ".hxx", ".tpp" };
        return extensions.contains(path.extension().string());
    }

    std::string global_kind(const fs::path& path) {
        const auto name = path.filename().string();
        if (name == "manifest.json" || name == "manifesto.github.vars.json"
            || name == "manifesto.local.json")
            return "manifest";
        if (beneath(".github", path))
            return "workflow";
        if (beneath("templates", path) || name == "CMakeLists.txt"
            || path.extension() == ".cmake" || name.ends_with(".cmake.in")
            || name == "CMakePresets.json" || name == "CMakeUserPresets.json"
            || name == ".clang-format" || name == ".clang-tidy"
            || name == ".gitignore" || name == ".gitattributes"
            || name == ".gitmodules")
            return "build-policy";
        return {};
    }

    bool presentation_path(const fs::path& path) {
        const auto name = path.filename().string();
        if (!path.has_parent_path()
            && (name == "README.md" || name == "readme.md"))
            return true;
        static const names extensions { ".md",  ".rst",  ".txt",
                                        ".png", ".jpg",  ".jpeg",
                                        ".svg", ".webp", ".gif" };
        return beneath("docs", path)
            && extensions.contains(path.extension().string());
    }
} // namespace

json change_report(const fs::path& root, const manifest& value) {
    names all_artifacts;
    for (const auto& owner : value.components)
        for (const auto& artifact : owner.artifacts)
            all_artifacts.insert(
                format_artifact_ref({ owner.id, artifact.id })
            );
    json result { { "schema", 1 },
                  { "project", value.id },
                  { "delta_validated", false },
                  { "classification", "full" },
                  { "reason", "missing-change-record" },
                  { "comparison", nullptr },
                  { "paths", json::array() },
                  { "affected_projects", json::array({ value.id }) },
                  { "affected_artifacts", all_artifacts },
                  { "format",
                    { { "scope", "full" }, { "files", json::array() } } },
                  { "checks",
                    { { "repository", "full" },
                      { "build", "full" },
                      { "tests", "full" },
                      { "coverage", "full" },
                      { "codeql", "full" },
                      { "presentation", true } } } };
    try {
        const auto input_path = root / ".ecosystem/github/changes.json";
        require(
            validate_project_paths(root, { ".ecosystem/github/changes.json" })
                .empty(),
            "unsafe-change-record"
        );
        require(fs::is_regular_file(input_path), "missing-change-record");
        require(
            fs::file_size(input_path) <= max_record_bytes + 1U,
            "oversized-change-record"
        );
        std::ifstream stream(input_path);
        const auto input = json::parse(stream);
        const auto delta = validate_delta(root, input);
        require(
            validate_manifest_paths(value, root).empty(),
            "unsafe-manifest-paths"
        );
        result["delta_validated"] = true;
        result["comparison"]
            = { { "event", input.at("event") },
                { "checkout", input.at("checkout") },
                { "base", input.at("base") },
                { "head", input.at("head") },
                { "comparison_base", input.at("comparison_base") },
                { "manifest_blob",
                  git_line(root, { "rev-parse", "HEAD:manifest.json" }) } };
        names affected;
        names formatting;
        std::vector<std::pair<fs::path, std::string>> claims;
        std::map<fs::path, names> physical_owners;
        std::map<fs::path, names> physical_format_files;
        for (const auto& owner : value.components)
            for (const auto& artifact : owner.artifacts) {
                const artifact_ref ref { owner.id, artifact.id };
                const auto identity = format_artifact_ref(ref);
                for (const auto& scope : owned_path_candidates(owner))
                    claims.emplace_back(scope, identity);
                for (const auto& file : declared_cpp_files(value, root, ref)) {
                    const auto physical = fs::weakly_canonical(file);
                    physical_owners[physical].insert(identity);
                    if (fs::is_regular_file(file))
                        physical_format_files[physical].insert(
                            file.lexically_relative(root).generic_string()
                        );
                }
            }
        bool full = false;
        bool presentation = false;
        for (const auto& [name, status] : delta) {
            const fs::path path(name);
            std::string kind = global_kind(path);
            names owners;
            for (const auto& [scope, identity] : claims)
                if (beneath(scope, path))
                    owners.insert(identity);
            const auto physical = fs::weakly_canonical(root / path);
            if (const auto found = physical_owners.find(physical);
                found != physical_owners.end())
                owners.insert(found->second.begin(), found->second.end());
            if (const auto found = physical_format_files.find(physical);
                found != physical_format_files.end())
                formatting.insert(found->second.begin(), found->second.end());
            if (!kind.empty()) {
                full = true;
            } else if (beneath("assets", path)) {
                kind = "assets";
                for (const auto& owner : value.components)
                    for (const auto& artifact : owner.artifacts)
                        if (artifact.kind == "exe" || artifact.kind == "qt_app"
                            || !owner.tests.empty())
                            owners.insert(
                                format_artifact_ref({ owner.id, artifact.id })
                            );
                // Assets affect generated staging/install surfaces as well as
                // runnable targets, including when install_assets is false.
                full = true;
                presentation = true;
            } else if (
                (!value.android_package_source_dir.empty()
                 && beneath(value.android_package_source_dir, path))
                || beneath("java", path)
            ) {
                kind = "platform-input";
                full = true;
            } else if (!owners.empty()) {
                kind = cxx_path(path) ? "cxx" : "owned-input";
            } else if (presentation_path(path)) {
                kind = "presentation";
                presentation = true;
            } else {
                kind = cxx_path(path) ? "unowned-cxx" : "unknown";
                full = true;
            }
            // Historical aliases and ownership may have disappeared. Keep
            // deletions/renames and type changes on full verification for now.
            if (status == "D" || status == "T" || fs::is_symlink(root / path)
                || !fs::is_regular_file(root / path))
                full = true;
            affected.insert(owners.begin(), owners.end());
            result["paths"].push_back(
                { { "path", name },
                  { "status", status },
                  { "kind", kind },
                  { "owners", owners } }
            );
        }
        bool grew = true;
        while (grew) {
            grew = false;
            for (const auto& owner : value.components)
                for (const auto& artifact : owner.artifacts)
                    if (std::any_of(
                            artifact.link.begin(), artifact.link.end(),
                            [&](const auto& dependency) {
                                return affected.contains(dependency);
                            }
                        ))
                        grew = affected
                                   .insert(format_artifact_ref(
                                       { owner.id, artifact.id }
                                   ))
                                   .second
                            || grew;
        }
        if (full) {
            result["reason"] = "global-or-uncertain-input";
            return result;
        }
        const auto classification = !affected.empty() ? "affected"
            : presentation                            ? "presentation"
                                                      : "none";
        result["classification"] = classification;
        result["reason"] = "manifest-owned-relevance";
        result["affected_artifacts"] = affected;
        result["affected_projects"]
            = delta.empty() ? json::array() : json::array({ value.id });
        result["format"]
            = { { "scope", formatting.empty() ? "none" : "changed" },
                { "files", formatting } };
        result["checks"]["build"] = affected.empty() ? "none" : "affected";
        result["checks"]["tests"] = affected.empty() ? "none" : "affected";
        result["checks"]["codeql"] = affected.empty() ? "none" : "full";
        result["checks"]["presentation"] = presentation;
        return result;
    } catch (const json::exception&) {
        result["reason"] = "invalid-change-record";
    } catch (const std::exception& error) {
        // No failure may leave a partially narrowed plan behind.
        result["reason"] = error.what();
    }
    result["delta_validated"] = false;
    return result;
}

} // namespace ecosystem
