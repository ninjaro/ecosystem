#include "workspace/project.hpp"

#include <algorithm>
#include <functional>
#include <set>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace ecosystem {
namespace project_support {

    bool path_is_within(const fs::path& root, const fs::path& path) {
        const fs::path relative = path.lexically_relative(root);
        return !relative.empty() && !relative.is_absolute()
            && *relative.begin() != "..";
    }

    // Unlike weakly_canonical alone, this rejects dangling symlinks in a future
    // scaffold destination instead of treating them as ordinary missing paths.
    fs::path resolve_owned_path(
        const fs::path& path, const fs::path& boundary,
        std::string* error_message
    ) {
        fs::path resolved = path.root_path();
        for (const fs::path& part : path.relative_path()) {
            if (part.empty() || part == ".") {
                continue;
            }
            if (part == "..") {
                resolved = resolved.parent_path();
                continue;
            }
            resolved /= part;
            std::error_code error;
            const fs::file_status status = fs::symlink_status(resolved, error);
            if (error == std::errc::no_such_file_or_directory) {
                continue;
            }
            if (error) {
                *error_message = "unable to inspect owned path " + path.string()
                    + ": " + error.message();
                return {};
            }
            if (!fs::is_symlink(status)) {
                continue;
            }
            resolved = fs::canonical(resolved, error);
            if (error) {
                *error_message = "unable to resolve owned symlink "
                    + path.string() + ": " + error.message();
                return {};
            }
            if (!boundary.empty() && !path_is_within(boundary, resolved)) {
                *error_message
                    = "owned path resolves outside project: " + path.string();
                return {};
            }
        }
        return resolved;
    }

    std::vector<fs::path> owned_paths(
        const fs::path& project_root, const std::vector<fs::path>& files
    ) {
        std::vector<fs::path> result;
        for (const auto& path : files)
            result.push_back((project_root / path).lexically_normal());
        return result;
    }

std::string normalize_generic(const fs::path& path) {
    return path.lexically_normal().generic_string();
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0U) == 0U;
}

bool is_runtime_source_only_id(const std::string& value) {
    const std::string leaf = fs::path(value).filename().generic_string();
    return leaf == "main" || leaf.ends_with("_main");
}

bool all_file_units_have_prefix(const component& value, const std::string& prefix) {
    if (value.file_units.empty()) {
        return false;
    }
    for (const file_unit& unit : value.file_units) {
        if (!starts_with(unit.id, prefix)) {
            return false;
        }
    }
    return true;
}

bool stack_contains_feature(const component& value, const std::string& feature) {
    if (!value.stack.is_object()) {
        return false;
    }
    if (value.stack.contains(feature)) {
        const json& entry = value.stack.at(feature);
        if (entry.is_boolean()) {
            return entry.get<bool>();
        }
        if (entry.is_string()) {
            return !entry.get<std::string>().empty();
        }
        if (entry.is_array()) {
            return !entry.empty();
        }
        if (entry.is_object()) {
            return !entry.empty();
        }
    }

    for (const auto& [key, entry] : value.stack.items()) {
        if (key == feature) {
            return true;
        }
        if (entry.is_string() && entry.get<std::string>() == feature) {
            return true;
        }
        if (entry.is_array()) {
            for (const json& array_item : entry) {
                if (array_item.is_string() && array_item.get<std::string>() == feature) {
                    return true;
                }
            }
        }
    }

    return false;
}

std::vector<fs::path> discover_sources(const fs::path& directory) {
    std::vector<fs::path> files;
    std::error_code error;
    if (!fs::exists(directory, error)) {
        return files;
    }
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(directory, error)) {
        if (error) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string extension = entry.path().extension().generic_string();
        if (extension == ".cpp" || extension == ".cc" || extension == ".cxx") {
            files.push_back(entry.path().lexically_normal());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

void append_unique_paths(std::vector<fs::path>* destination, const std::vector<fs::path>& source) {
    std::set<std::string> seen;
    for (const fs::path& path : *destination) {
        seen.insert(normalize_generic(path));
    }
    for (const fs::path& path : source) {
        if (seen.insert(normalize_generic(path)).second) {
            destination->push_back(path);
        }
    }
}

fs::path resolve_source_only_path(const fs::path& component_root, const std::string& id) {
    if (starts_with(id, "tests/")) {
        return component_root / "tests" / (id.substr(6U) + ".cpp");
    }
    if (starts_with(id, "benchmarks/")) {
        return component_root / "benchmarks" / (id.substr(11U) + ".cpp");
    }
    if (starts_with(id, "src/")) {
        return component_root / (id + ".cpp");
    }
    return component_root / "src" / (id + ".cpp");
}

fs::path resolve_header_path(
    const fs::path& component_root, const std::string& id, const std::string& extension
) {
    if (starts_with(id, "tests/")) {
        return component_root / "tests" / (id.substr(6U) + extension);
    }
    if (starts_with(id, "benchmarks/")) {
        return component_root / "benchmarks" / (id.substr(11U) + extension);
    }
    if (starts_with(id, "include/")) {
        return component_root / (id + extension);
    }
    return component_root / "include" / (id + extension);
}

std::vector<fs::path> file_unit_paths(
    const fs::path& component_root,
    const component& value,
    const std::set<std::string>& accepted_kinds,
    const bool source_paths
) {
    std::vector<fs::path> files;
    for (const file_unit& unit : value.file_units) {
        if (!accepted_kinds.contains(unit.kind)) {
            continue;
        }
        if (source_paths) {
            if (unit.kind == "source_only") {
                files.push_back(resolve_source_only_path(component_root, unit.id));
            } else if (unit.kind == "source_pair_h") {
                files.push_back(resolve_source_only_path(component_root, unit.id));
            }
            continue;
        }
        if (unit.kind == "header_only" || unit.kind == "header_template_impl") {
            files.push_back(resolve_header_path(component_root, unit.id, ".hpp"));
        } else if (unit.kind == "header_only_h" || unit.kind == "source_pair_h") {
            files.push_back(resolve_header_path(component_root, unit.id, ".h"));
        }
    }
    return files;
}

void visit_artifact_closure(
    const manifest& value,
    const artifact_ref& ref,
    std::vector<const component*>* components,
    std::set<std::string>* seen_artifacts,
    std::set<std::string>* seen_components
) {
    const std::string ref_key = format_artifact_ref(ref);
    if (!seen_artifacts->insert(ref_key).second) {
        return;
    }

    const component* component_value
        = find_component(value, format_artifact_ref(ref));
    if (component_value == nullptr) {
        return;
    }
    if (seen_components
            ->insert(component_value->ownership ? ref_key : component_value->id)
            .second) {
        components->push_back(component_value);
    }

    const artifact* artifact_value = find_artifact(*component_value, ref.artifact_id);
    if (artifact_value == nullptr) {
        return;
    }

    for (const std::string& link : artifact_value->link) {
        const std::optional<artifact_ref> linked_ref = parse_artifact_ref(link);
        if (!linked_ref.has_value()) {
            continue;
        }
        visit_artifact_closure(
            value,
            *linked_ref,
            components,
            seen_artifacts,
            seen_components
        );
    }
}

bool component_has_tests(const component& component_value) {
    return !component_value.tests.empty();
}

bool component_has_benchmarks(const component& component_value) {
    return !component_value.benchmarks.empty();
}

std::string analysis_source_category_for_id(const std::string& value) {
    if (starts_with(value, "tests/")) {
        return "tests";
    }
    if (starts_with(value, "benchmarks/")) {
        return "benchmarks";
    }
    if (is_runtime_source_only_id(value)) {
        return "runtime";
    }
    return "core";
}

bool analysis_category_enabled(
    const std::string& category, const bool include_tests,
    const bool include_benchmarks
) {
    if (category == "tests") {
        return include_tests;
    }
    if (category == "benchmarks") {
        return include_benchmarks;
    }
    return true;
}

void append_unique_analysis_source(
    std::vector<cxx_analysis_source>* destination, const fs::path& path,
    const std::string& component_id, const std::string& category,
    const std::string& artifact_id = {}
) {
    const std::string normalized_path = normalize_generic(path);
    for (const cxx_analysis_source& source : *destination) {
        if (normalize_generic(source.path) == normalized_path) {
            return;
        }
    }
    destination->push_back(
        cxx_analysis_source { path, component_id, category, artifact_id }
    );
}

std::vector<cxx_analysis_source> component_analysis_sources(
    const fs::path& project_root, const component& component_value,
    const bool include_tests, const bool include_benchmarks
) {
    std::vector<cxx_analysis_source> files;
    const fs::path root = component_root_path(project_root, component_value);
    const std::string artifact_id = component_value.artifacts.size() == 1
        ? format_artifact_ref(
              { component_value.id, component_value.artifacts.front().id }
          )
        : std::string {};
    if (component_value.ownership) {
        const auto& owned = *component_value.ownership;
        for (const auto& source : owned.sources) {
            const std::string category = component_is_test_only(component_value)
                ? "tests"
                : component_is_benchmark_only(component_value) ? "benchmarks"
                : source
                    == (fs::path(component_value.root) / owned.entry)
                           .lexically_normal()
                ? "runtime"
                : "core";
            if (analysis_category_enabled(
                    category, include_tests, include_benchmarks
                ))
                append_unique_analysis_source(
                    &files, project_root / source, component_value.id, category,
                    artifact_id
                );
        }
        if (include_tests)
            for (const auto& source : owned.tests)
                append_unique_analysis_source(
                    &files, project_root / source, component_value.id, "tests",
                    artifact_id
                );
        if (include_benchmarks)
            for (const auto& source : owned.benchmarks)
                append_unique_analysis_source(
                    &files, project_root / source, component_value.id,
                    "benchmarks", artifact_id
                );
        return files;
    }

    for (const std::string& module_path : component_value.modules) {
        append_unique_analysis_source(
            &files, root / "src" / (module_path + ".cpp"), component_value.id,
            "core", artifact_id
        );
    }

    for (const file_unit& unit : component_value.file_units) {
        if (unit.kind != "source_only" && unit.kind != "source_pair_h") {
            continue;
        }
        const std::string category = analysis_source_category_for_id(unit.id);
        if (!analysis_category_enabled(
                category, include_tests, include_benchmarks
            )) {
            continue;
        }
        append_unique_analysis_source(
            &files, resolve_source_only_path(root, unit.id), component_value.id,
            category, artifact_id
        );
    }

    return files;
}

std::vector<fs::path> component_analysis_include_dirs(
    const fs::path& project_root, const component& value,
    const bool include_tests, const bool include_benchmarks
) {
    std::vector<fs::path> include_dirs;
    const fs::path root = component_root_path(project_root, value);
    const fs::path project_include = root / "include";
    const fs::path test_include = root / "tests" / "include";
    const fs::path benchmark_include = root / "benchmarks" / "include";
    std::error_code error;
    if (fs::exists(project_include, error) && !error) {
        include_dirs.push_back(project_include);
    }
    error.clear();
    if (include_tests && fs::exists(test_include, error) && !error) {
        include_dirs.push_back(test_include);
    }
    error.clear();
    if (include_benchmarks && fs::exists(benchmark_include, error) && !error) {
        include_dirs.push_back(benchmark_include);
    }
    if (include_dirs.empty()) {
        include_dirs.push_back(project_include);
    }
    return include_dirs;
}

}  // namespace project_support

using namespace project_support;

string_list validate_project_paths(
    const fs::path& project_root, const std::vector<fs::path>& relative_paths
) {
    string_list errors;
    std::error_code error;
    const fs::path absolute_root = fs::absolute(
        project_root.empty() ? fs::path(".") : project_root, error
    );
    if (error) {
        return { "unable to resolve project root: " + error.message() };
    }
    std::string error_message;
    const fs::path root = resolve_owned_path(absolute_root, {}, &error_message);
    if (!error_message.empty()) {
        return { error_message };
    }
    std::set<fs::path> seen;
    for (const fs::path& relative : relative_paths) {
        if (!seen.insert(relative).second) {
            continue;
        }
        if (relative.empty() || relative.is_absolute()
            || std::find(relative.begin(), relative.end(), fs::path(".."))
                != relative.end()) {
            errors.push_back(
                "owned path must be project-relative: " + relative.string()
            );
            continue;
        }
        error_message.clear();
        resolve_owned_path(
            (root / relative).lexically_normal(), root, &error_message
        );
        if (!error_message.empty()) {
            errors.push_back(error_message);
        }
    }
    return errors;
}

string_list
validate_manifest_paths(const manifest& value, const fs::path& project_root) {
    string_list errors = validate_manifest(value);
    if (!errors.empty()) {
        return errors;
    }
    std::vector<fs::path> paths { "manifest.json", ".ecosystem" };
    std::vector<fs::path> trees;
    for (const component& owner : value.components) {
        paths.emplace_back(owner.root);
        for (const std::string directory :
             { "include", "src", "tests", "benchmarks" }) {
            paths.push_back(fs::path(owner.root) / directory);
        }
        for (const auto enumerate :
             { component_module_headers, component_module_sources,
               component_source_only_files, component_header_only_files,
               component_c_header_only_files, component_template_impl_files,
               component_c_header_pair_headers,
               component_c_header_pair_sources }) {
            append_unique_paths(&paths, enumerate({}, owner));
        }
        trees.push_back(fs::path(owner.root) / "tests");
        trees.push_back(fs::path(owner.root) / "benchmarks");
    }
    if (!value.android_package_source_dir.empty()) {
        trees.emplace_back(value.android_package_source_dir);
    }
    if (value.install_assets) {
        trees.emplace_back("assets");
    }
    append_unique_paths(&paths, trees);
    errors = validate_project_paths(project_root, paths);
    if (!errors.empty()) {
        return errors;
    }

    // Check discovered inputs as well as declared files. Validate each entry
    // before following directory symlinks, and visit each physical directory
    // once so an in-project symlink cycle cannot make traversal unbounded.
    std::set<fs::path> visited;
    const fs::path base = project_root.empty() ? fs::path(".") : project_root;
    for (const fs::path& tree : trees) {
        std::error_code error;
        if (!fs::exists(base / tree, error) && !error) {
            continue;
        }
        const fs::path canonical_tree = fs::canonical(base / tree, error);
        if (!error && !visited.insert(canonical_tree).second) {
            continue;
        }
        fs::recursive_directory_iterator entry;
        if (!error) {
            entry = fs::recursive_directory_iterator(
                base / tree, fs::directory_options::follow_directory_symlink,
                error
            );
        }
        const fs::recursive_directory_iterator end;
        while (!error && entry != end) {
            const fs::path relative = entry->path().lexically_relative(base);
            string_list entry_errors
                = validate_project_paths(base, { relative });
            if (!entry_errors.empty()) {
                errors.insert(
                    errors.end(), entry_errors.begin(), entry_errors.end()
                );
                entry.disable_recursion_pending();
            } else if (entry->is_directory(error) && !error) {
                const fs::path directory = fs::canonical(entry->path(), error);
                if (!error && !visited.insert(directory).second) {
                    entry.disable_recursion_pending();
                }
            }
            if (!error) {
                entry.increment(error);
            }
        }
        if (error) {
            errors.push_back(
                "unable to inspect owned tree " + tree.string() + ": "
                + error.message()
            );
        }
    }
    return errors;
}

std::vector<std::string> component_stack_values(const component& value, const std::string& key) {
    std::vector<std::string> values;
    if (!value.stack.is_object() || !value.stack.contains(key)) {
        return values;
    }

    const json& entry = value.stack.at(key);
    if (entry.is_string()) {
        values.push_back(entry.get<std::string>());
        return values;
    }
    if (!entry.is_array()) {
        return values;
    }
    for (const json& item : entry) {
        if (item.is_string()) {
            values.push_back(item.get<std::string>());
        }
    }
    return values;
}

bool component_has_stack_key(const component& value, const std::string& key) {
    return stack_contains_feature(value, key);
}

const component* find_component(const manifest& value, const std::string& component_id) {
    const auto ref = parse_artifact_ref(component_id);
    const component* result = nullptr;
    for (const component& item : value.components) {
        if (ref && item.id == ref->component_id
            && find_artifact(item, ref->artifact_id))
            return &item;
        if (!ref && item.id == component_id) {
            if (result != nullptr)
                return nullptr;
            result = &item;
        }
    }
    return result;
}

const artifact* find_artifact(const component& value, const std::string& artifact_id) {
    for (const artifact& artifact_value : value.artifacts) {
        if (artifact_value.id == artifact_id) {
            return &artifact_value;
        }
    }
    return nullptr;
}

std::optional<resolved_artifact> resolve_artifact(
    const manifest& value, const std::optional<artifact_ref>& requested
) {
    const artifact_ref resolved_ref
        = requested.has_value() ? *requested : *parse_artifact_ref(value.facade_entry_artifact);
    const component* component_value
        = find_component(value, format_artifact_ref(resolved_ref));
    if (component_value == nullptr) {
        return std::nullopt;
    }
    const artifact* artifact_value = find_artifact(*component_value, resolved_ref.artifact_id);
    if (artifact_value == nullptr) {
        return std::nullopt;
    }
    return resolved_artifact { resolved_ref, component_value, artifact_value };
}

std::vector<const component*> component_closure_for_artifact(
    const manifest& value, const artifact_ref& root_ref
) {
    std::vector<const component*> components;
    std::set<std::string> seen_artifacts;
    std::set<std::string> seen_components;
    visit_artifact_closure(
        value,
        root_ref,
        &components,
        &seen_artifacts,
        &seen_components
    );
    return components;
}

fs::path component_root_path(const fs::path& project_root, const component& value) {
    return (project_root / value.root).lexically_normal();
}

std::vector<fs::path> component_include_dirs(
    const fs::path& project_root, const component& value
) {
    std::vector<fs::path> include_dirs;
    const fs::path root = component_root_path(project_root, value);
    const fs::path project_include = root / "include";
    const fs::path test_include = root / "tests" / "include";
    const fs::path benchmark_include = root / "benchmarks" / "include";
    std::error_code error;
    if (fs::exists(project_include, error) && !error) {
        include_dirs.push_back(project_include);
    }
    error.clear();
    if (fs::exists(test_include, error) && !error) {
        include_dirs.push_back(test_include);
    }
    error.clear();
    if (fs::exists(benchmark_include, error) && !error) {
        include_dirs.push_back(benchmark_include);
    }
    if (include_dirs.empty()) {
        include_dirs.push_back(project_include);
    }
    return include_dirs;
}

std::vector<fs::path> component_module_headers(
    const fs::path& project_root, const component& value
) {
    if (value.ownership) {
        return owned_paths(project_root, value.ownership->headers);
    }

    std::vector<fs::path> files;
    const fs::path root = component_root_path(project_root, value);
    for (const std::string& module_path : value.modules) {
        files.push_back(root / "include" / (module_path + ".hpp"));
    }
    return files;
}

std::vector<fs::path> component_module_sources(
    const fs::path& project_root, const component& value
) {
    if (value.ownership) {
        auto result = owned_paths(project_root, value.ownership->sources);
        const fs::path entry
            = (project_root / value.root / value.ownership->entry)
                  .lexically_normal();
        std::erase(result, entry);
        return result;
    }

    std::vector<fs::path> files;
    const fs::path root = component_root_path(project_root, value);
    for (const std::string& module_path : value.modules) {
        files.push_back(root / "src" / (module_path + ".cpp"));
    }
    return files;
}

std::vector<fs::path> component_source_only_files(
    const fs::path& project_root, const component& value
) {
    if (value.ownership) {
        if (value.ownership->entry.empty())
            return {};
        return { (project_root / value.root / value.ownership->entry)
                     .lexically_normal() };
    }

    return file_unit_paths(
        component_root_path(project_root, value),
        value,
        { "source_only" },
        true
    );
}

std::vector<fs::path> component_runtime_only_files(
    const fs::path& project_root, const component& value
) {
    if (value.ownership) {
        return component_source_only_files(project_root, value);
    }

    std::vector<fs::path> files;
    const fs::path root = component_root_path(project_root, value);
    for (const file_unit& unit : value.file_units) {
        if (unit.kind == "source_only" && is_runtime_source_only_id(unit.id)) {
            files.push_back(resolve_source_only_path(root, unit.id));
        }
    }
    return files;
}

std::vector<fs::path> component_non_runtime_source_only_files(
    const fs::path& project_root, const component& value
) {
    if (value.ownership) {
        return {};
    }

    std::vector<fs::path> files;
    const fs::path root = component_root_path(project_root, value);
    for (const file_unit& unit : value.file_units) {
        if (unit.kind == "source_only" && !is_runtime_source_only_id(unit.id)) {
            files.push_back(resolve_source_only_path(root, unit.id));
        }
    }
    return files;
}

std::vector<fs::path> component_header_only_files(
    const fs::path& project_root, const component& value
) {
    if (value.ownership) {
        return {};
    }

    return file_unit_paths(
        component_root_path(project_root, value),
        value,
        { "header_only", "header_template_impl" },
        false
    );
}

std::vector<fs::path> component_c_header_only_files(
    const fs::path& project_root, const component& value
) {
    if (value.ownership) {
        return {};
    }

    return file_unit_paths(
        component_root_path(project_root, value),
        value,
        { "header_only_h" },
        false
    );
}

std::vector<fs::path> component_template_impl_files(
    const fs::path& project_root, const component& value
) {
    if (value.ownership) {
        return {};
    }

    std::vector<fs::path> files;
    const fs::path root = component_root_path(project_root, value);
    for (const file_unit& unit : value.file_units) {
        if (unit.kind == "header_template_impl") {
            files.push_back(resolve_header_path(root, unit.id, ".tpp"));
        }
    }
    return files;
}

std::vector<fs::path> component_c_header_pair_headers(
    const fs::path& project_root, const component& value
) {
    if (value.ownership) {
        return {};
    }

    return file_unit_paths(
        component_root_path(project_root, value),
        value,
        { "source_pair_h" },
        false
    );
}

std::vector<fs::path> component_c_header_pair_sources(
    const fs::path& project_root, const component& value
) {
    if (value.ownership) {
        return {};
    }

    return file_unit_paths(
        component_root_path(project_root, value),
        value,
        { "source_pair_h" },
        true
    );
}

std::vector<fs::path> component_test_sources(
    const fs::path& project_root, const component& value
) {
    if (value.ownership) {
        return owned_paths(project_root, value.ownership->tests);
    }

    return discover_sources(component_root_path(project_root, value) / "tests");
}

std::vector<fs::path> component_benchmark_sources(
    const fs::path& project_root, const component& value
) {
    if (value.ownership) {
        return owned_paths(project_root, value.ownership->benchmarks);
    }

    return discover_sources(component_root_path(project_root, value) / "benchmarks");
}

bool component_is_test_only(const component& value) {
    if (value.ownership)
        return value.ownership->entry.starts_with("tests/");
    return !value.tests.empty() && value.modules.empty() && all_file_units_have_prefix(value, "tests/");
}

bool component_is_benchmark_only(const component& value) {
    if (value.ownership)
        return value.ownership->entry.starts_with("benchmarks/");
    return !value.benchmarks.empty() && value.modules.empty()
        && all_file_units_have_prefix(value, "benchmarks/");
}

std::optional<std::string>
component_generated_test_target(const component& value) {
    if (value.tests.empty() || component_is_test_only(value)) {
        return std::nullopt;
    }
    if (value.ownership && !value.artifacts.empty())
        return cmake_target_name({ value.id, value.artifacts.front().id })
            + "__tests";
    return cmake_target_name({ value.id, "tests" });
}

std::optional<fs::path> artifact_output_path(
    const fs::path& build_dir,
    const artifact& artifact_value,
    const std::string& output_name
) {
    std::error_code error;
    for (const fs::path& candidate : artifact_output_candidates(build_dir, artifact_value, output_name)) {
        if (fs::exists(candidate, error) && !error && !fs::is_directory(candidate, error)) {
            return candidate;
        }
        error.clear();
    }
    return std::nullopt;
}

std::vector<fs::path> format_candidate_files(
    const manifest& value, const fs::path& project_root,
    const std::optional<artifact_ref>& requested_artifact
) {
    std::vector<fs::path> files;
    for (const component& component_value : value.components) {
        if (requested_artifact
            && (component_value.id != requested_artifact->component_id
                || !find_artifact(
                    component_value, requested_artifact->artifact_id
                ))) {
            continue;
        }
        append_unique_paths(&files, component_module_headers(project_root, component_value));
        append_unique_paths(&files, component_module_sources(project_root, component_value));
        append_unique_paths(&files, component_header_only_files(project_root, component_value));
        append_unique_paths(&files, component_c_header_only_files(project_root, component_value));
        append_unique_paths(&files, component_template_impl_files(project_root, component_value));
        append_unique_paths(&files, component_c_header_pair_headers(project_root, component_value));
        append_unique_paths(&files, component_c_header_pair_sources(project_root, component_value));
        append_unique_paths(&files, component_source_only_files(project_root, component_value));
        append_unique_paths(&files, component_test_sources(project_root, component_value));
        append_unique_paths(&files, component_benchmark_sources(project_root, component_value));
    }
    std::vector<fs::path> existing_files;
    for (const fs::path& file : files) {
        std::error_code error;
        if (fs::exists(file, error) && !error) {
            existing_files.push_back(file);
        }
    }
    return existing_files;
}

std::vector<cxx_analysis_source> cxx_analysis_sources(
    const manifest& value,
    const fs::path& project_root,
    const std::optional<std::string>& component_filter,
    const bool include_tests,
    const bool include_benchmarks
) {
    std::vector<cxx_analysis_source> files;
    for (const component& component_value : value.components) {
        if (component_filter.has_value() && component_value.id != *component_filter) {
            continue;
        }
        if (component_is_test_only(component_value) && !include_tests) {
            continue;
        }
        if (component_is_benchmark_only(component_value)
            && !include_benchmarks) {
            continue;
        }
        for (const cxx_analysis_source& source :
             component_analysis_sources(
                 project_root,
                 component_value,
                 include_tests,
                 include_benchmarks
             )) {
            append_unique_analysis_source(
                &files, source.path, source.component_id, source.category,
                source.artifact
            );
        }
    }
    return files;
}

std::vector<fs::path> manifest_include_dirs(
    const manifest& value,
    const fs::path& project_root,
    const std::optional<std::string>& component_filter,
    const bool include_tests,
    const bool include_benchmarks
) {
    std::vector<fs::path> include_dirs;
    for (const component& component_value : value.components) {
        if (component_filter.has_value() && component_value.id != *component_filter) {
            continue;
        }
        if (component_is_test_only(component_value) && !include_tests) {
            continue;
        }
        if (component_is_benchmark_only(component_value)
            && !include_benchmarks) {
            continue;
        }
        append_unique_paths(
            &include_dirs,
            component_analysis_include_dirs(
                project_root,
                component_value,
                include_tests,
                include_benchmarks
            )
        );
    }
    return include_dirs;
}

string_list missing_declared_files(const manifest& value, const fs::path& project_root) {
    string_list missing_files;
    for (const component& component_value : value.components) {
        std::vector<fs::path> declared_files = component_module_headers(project_root, component_value);
        append_unique_paths(&declared_files, component_module_sources(project_root, component_value));
        append_unique_paths(&declared_files, component_header_only_files(project_root, component_value));
        append_unique_paths(&declared_files, component_c_header_only_files(project_root, component_value));
        append_unique_paths(&declared_files, component_template_impl_files(project_root, component_value));
        append_unique_paths(&declared_files, component_c_header_pair_headers(project_root, component_value));
        append_unique_paths(&declared_files, component_c_header_pair_sources(project_root, component_value));
        append_unique_paths(&declared_files, component_source_only_files(project_root, component_value));
        for (const fs::path& file : declared_files) {
            std::error_code error;
            if (!fs::exists(file, error)) {
                missing_files.push_back(normalize_generic(file.lexically_relative(project_root)));
            }
        }
    }
    return missing_files;
}

std::vector<std::string> supported_build_profiles(const manifest& value) {
    std::vector<std::string> profiles { "debug", "release" };
    if (manifest_has_stack_key(value, "android")) {
        profiles.push_back("android");
    }
    if (manifest_has_stack_key(value, "kde")) {
        profiles.push_back("kde");
    }
    return profiles;
}

std::vector<std::string> supported_check_profiles(const manifest& value) {
    std::vector<std::string> profiles;
    if (has_tests_enabled(value)) {
        profiles.push_back("tests");
        profiles.push_back("coverage");
        profiles.push_back("leaks");
    }
    if (manifest_has_stack_key(value, "jni")) {
        profiles.push_back("java");
    }
    profiles.push_back("tidy");
    profiles.push_back("format");
    profiles.push_back("naming");
    profiles.push_back("style");
    profiles.push_back("repo");
    profiles.push_back("doxy");
    profiles.push_back("sphinx");
    profiles.push_back("ci");
    return profiles;
}

std::vector<std::string> supported_report_kinds() {
    return { "cxx", "toolchains", "matrix", "naming", "style" };
}

std::vector<std::string> supported_platforms(const manifest& value) {
    std::vector<std::string> platforms { "native" };
    if (manifest_has_stack_key(value, "android")) {
        platforms.push_back("android");
    }
    return platforms;
}

std::vector<artifact_ref> artifact_refs_for_kind(const manifest& value, const std::string& kind) {
    std::vector<artifact_ref> refs;
    for (const component& component_value : value.components) {
        for (const artifact& artifact_value : component_value.artifacts) {
            if (artifact_value.kind == kind) {
                refs.push_back(artifact_ref { component_value.id, artifact_value.id });
            }
        }
    }
    return refs;
}

std::vector<artifact_ref> benchmark_artifact_refs(const manifest& value) {
    std::vector<artifact_ref> refs;
    for (const component& component_value : value.components) {
        if (!component_is_benchmark_only(component_value)) {
            continue;
        }
        for (const artifact& artifact_value : component_value.artifacts) {
            refs.push_back(artifact_ref { component_value.id, artifact_value.id });
        }
    }
    return refs;
}

bool supports_build_profile(const manifest& value, const std::string& profile) {
    const std::vector<std::string> profiles = supported_build_profiles(value);
    return std::find(profiles.begin(), profiles.end(), profile) != profiles.end();
}

bool supports_check_profile(const manifest& value, const std::string& profile) {
    const std::vector<std::string> profiles = supported_check_profiles(value);
    return std::find(profiles.begin(), profiles.end(), profile) != profiles.end();
}

bool supports_report_kind(const std::string& kind) {
    const std::vector<std::string> reports = supported_report_kinds();
    return std::find(reports.begin(), reports.end(), kind) != reports.end();
}

bool manifest_has_stack_key(const manifest& value, const std::string& key) {
    for (const component& component_value : value.components) {
        if (component_has_stack_key(component_value, key)) {
            return true;
        }
    }
    return false;
}

bool has_tests_enabled(const manifest& value) {
    return std::any_of(value.components.begin(), value.components.end(), component_has_tests);
}

bool has_benchmarks_enabled(const manifest& value) {
    return std::any_of(
        value.components.begin(),
        value.components.end(),
        component_has_benchmarks
    );
}

std::string cmake_target_name(const artifact_ref& ref) {
    return ref.component_id + "__" + ref.artifact_id;
}

std::vector<artifact_ref>
distribution_artifacts(const manifest& value, const artifact_ref& primary) {
    std::vector<artifact_ref> refs { primary };
    if (format_artifact_ref(primary) == value.facade_entry_artifact) {
        for (const std::string& entry : value.install_artifacts) {
            if (entry != format_artifact_ref(primary)) {
                if (const auto ref = parse_artifact_ref(entry);
                    ref.has_value()) {
                    refs.push_back(*ref);
                }
            }
        }
    }
    return refs;
}

}  // namespace ecosystem
