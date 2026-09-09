#include "manifest.hpp"

#include "workspace/project.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace ecosystem {
namespace manifest_support {

    const std::regex id_pattern("^[a-z][a-z0-9_]*$");
    const std::regex android_application_id_pattern(
        "^[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+$"
    );
    const std::regex android_package_source_dir_pattern(
        "^[A-Za-z0-9._-]+(?:/[A-Za-z0-9._-]+)*$"
    );
    const std::regex project_version_pattern(
        "^[0-9]+\\.[0-9]+\\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$"
    );
    const std::set<std::string> artifact_kinds {
        "static_lib", "shared_lib", "interface_lib", "exe", "qt_app",
    };
    const std::set<std::string> file_unit_kinds {
        "header_only", "header_only_h", "header_template_impl",
        "source_only", "source_pair_h",
    };

    std::string trim_copy(const std::string& value) {
        const std::size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }
        const std::size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1U);
    }

    std::string normalize_relative_path(const std::string& raw_path) {
        fs::path path = fs::path(trim_copy(raw_path)).lexically_normal();
        std::string normalized = path.generic_string();
        if (normalized == ".") {
            return normalized;
        }
        while (normalized.size() > 1U && normalized.back() == '/') {
            normalized.pop_back();
        }
        if (normalized.rfind("./", 0U) == 0U) {
            normalized.erase(0U, 2U);
        }
        return normalized;
    }

    std::string require_string(
        const json& object, const std::string& field_name,
        const std::string& context, string_list* errors
    ) {
        if (!object.contains(field_name)
            || !object.at(field_name).is_string()) {
            errors->push_back(
                context + "." + field_name + " must be a non-empty string"
            );
            return {};
        }
        const std::string value
            = trim_copy(object.at(field_name).get<std::string>());
        if (value.empty()) {
            errors->push_back(
                context + "." + field_name + " must be a non-empty string"
            );
        }
        return value;
    }

    int require_integer(
        const json& object, const std::string& field_name,
        const std::string& context, string_list* errors
    ) {
        if (!object.contains(field_name)
            || !object.at(field_name).is_number_integer()) {
            errors->push_back(
                context + "." + field_name + " must be an integer"
            );
            return 0;
        }
        return object.at(field_name).get<int>();
    }

    json require_object(
        const json& object, const std::string& field_name,
        const std::string& context, string_list* errors
    ) {
        if (!object.contains(field_name)) {
            errors->push_back(
                context + "." + field_name + " must be a JSON object"
            );
            return json::object();
        }
        if (!object.at(field_name).is_object()) {
            errors->push_back(
                context + "." + field_name + " must be a JSON object"
            );
            return json::object();
        }
        return object.at(field_name);
    }

    bool
    safe_owned_path(const std::string& text, const bool allow_root = false) {
        if (text == ".") {
            return allow_root;
        }
        if (text.empty()
            || !std::regex_match(text, android_package_source_dir_pattern)) {
            return false;
        }
        const fs::path path(text);
        if (path.is_absolute()
            || path.generic_string()
                != path.lexically_normal().generic_string()) {
            return false;
        }
        return std::none_of(
            path.begin(), path.end(), [](const fs::path& segment) {
                return segment == "." || segment == "..";
            }
        );
    }

    std::vector<fs::path> ownership_candidates(const component& owner) {
        std::vector<fs::path> result;
        for (const std::string& scope : owner.ownership->scopes) {
            if (!safe_owned_path(scope, true))
                continue;
            const fs::path logical(scope);
            const std::string first = logical.begin()->string();
            const bool qualified = first == "include" || first == "src"
                || first == "tests" || first == "benchmarks";
            const string_list directories = qualified
                ? string_list { "" }
                : string_list { "include", "src", "tests", "benchmarks" };
            for (const auto& directory : directories) {
                const auto candidate
                    = (fs::path(owner.root) / directory / logical)
                          .lexically_normal();
                result.push_back(candidate);
                if (logical.has_extension() || scope == "."
                    || (qualified && logical == fs::path(first)))
                    continue;
                for (const std::string extension :
                     { ".hpp", ".h", ".hh", ".hxx", ".tpp", ".cpp", ".cc",
                       ".cxx" })
                    result.emplace_back(candidate.string() + extension);
                if (!qualified && directory == "tests")
                    result.emplace_back(candidate.string() + "_tests.cpp");
                if (!qualified && directory == "benchmarks")
                    result.emplace_back(candidate.string() + "_benchmarks.cpp");
            }
        }
        return result;
    }

    bool contains_owned_path(const fs::path& scope, const fs::path& path) {
        const auto relative = path.lexically_relative(scope);
        return !relative.empty() && !relative.is_absolute()
            && *relative.begin() != "..";
    }

    bool safe_output_name(const std::string& text) {
        static const std::regex pattern("^[A-Za-z0-9_][A-Za-z0-9_.+-]*$");
        return std::regex_match(text, pattern);
    }

    bool is_valid_id(const std::string& value) {
        return std::regex_match(value, id_pattern);
    }

    string_list read_string_list(
        const json& object, const std::string& field_name,
        const std::string& context, string_list* errors
    ) {
        string_list values;
        if (!object.contains(field_name)) {
            return values;
        }
        const json& value = object.at(field_name);
        if (value.is_string()) {
            values.push_back(trim_copy(value.get<std::string>()));
            return values;
        }
        if (!value.is_array()) {
            errors->push_back(
                context + "." + field_name
                + " must be a string or array of strings"
            );
            return values;
        }
        for (std::size_t index = 0; index < value.size(); ++index) {
            if (!value.at(index).is_string()) {
                errors->push_back(
                    context + "." + field_name + "[" + std::to_string(index)
                    + "] must be a string"
                );
                continue;
            }
            values.push_back(trim_copy(value.at(index).get<std::string>()));
        }
        return values;
    }

    void check_fields(
        const json& object, const std::set<std::string>& allowed,
        const std::string& context, string_list* errors
    ) {
        for (const auto& [key, ignored] : object.items()) {
            if (!allowed.contains(key))
                errors->push_back(
                    context + ": unsupported field '" + key + "'"
                );
        }
    }

    string_list authored_list(
        const json& object, const std::string& field,
        const std::string& context, string_list* errors, bool required = false
    ) {
        if (!object.contains(field) && !required)
            return {};
        if (!object.contains(field) || !object.at(field).is_array()) {
            errors->push_back(context + "." + field + " must be an array");
            return {};
        }
        return read_string_list(object, field, context, errors);
    }

    manifest parse_manifest(const json& root, string_list* errors) {
        check_fields(
            root,
            { "id", "description", "version", "cpp_standard", "facade",
              "artifacts", "install_artifacts", "install_assets",
              "android_application_id", "android_package_source_dir" },
            "manifest", errors
        );
        manifest value;
        value.id = require_string(root, "id", "manifest", errors);
        value.description
            = require_string(root, "description", "manifest", errors);
        if (root.contains("version"))
            value.version = require_string(root, "version", "manifest", errors);
        if (root.contains("cpp_standard"))
            value.cpp_standard
                = require_integer(root, "cpp_standard", "manifest", errors);
        value.facade_entry_artifact
            = require_string(root, "facade", "manifest", errors);
        value.install_artifacts
            = authored_list(root, "install_artifacts", "manifest", errors);
        if (root.contains("android_application_id"))
            value.android_application_id = require_string(
                root, "android_application_id", "manifest", errors
            );
        if (root.contains("android_package_source_dir"))
            value.android_package_source_dir = require_string(
                root, "android_package_source_dir", "manifest", errors
            );
        if (root.contains("install_assets")) {
            if (!root.at("install_assets").is_boolean())
                errors->push_back("manifest.install_assets must be a boolean");
            else
                value.install_assets = root.at("install_assets").get<bool>();
        }
        if (!root.contains("artifacts") || !root.at("artifacts").is_array()) {
            errors->push_back(
                "manifest.artifacts must be an array; "
                "components/modules/file_units are obsolete authored fields"
            );
            return value;
        }
        for (std::size_t index = 0; index < root.at("artifacts").size();
             ++index) {
            const auto& item = root.at("artifacts").at(index);
            const std::string context
                = "manifest.artifacts[" + std::to_string(index) + "]";
            if (!item.is_object()) {
                errors->push_back(context + " must be an object");
                continue;
            }
            check_fields(
                item,
                { "id", "kind", "name", "description", "root", "owns", "entry",
                  "dependencies", "packages", "tests", "benchmarks" },
                context, errors
            );
            const std::string identity
                = require_string(item, "id", context, errors);
            const auto ref = parse_artifact_ref(identity);
            if (!ref) {
                errors->push_back(
                    context + ".id must be a namespace:artifact identity"
                );
                continue;
            }
            component owner;
            owner.id = ref->component_id;
            owner.description = item.contains("description")
                ? require_string(item, "description", context, errors)
                : value.description;
            owner.root = item.contains("root")
                ? require_string(item, "root", context, errors)
                : ".";
            owner.ownership.emplace();
            owner.ownership->scopes
                = authored_list(item, "owns", context, errors, true);
            if (item.contains("entry"))
                owner.ownership->entry
                    = require_string(item, "entry", context, errors);
            for (const auto& [field, destination] :
                 std::vector<std::pair<std::string, json*>> {
                     { "packages", &owner.stack },
                     { "tests", &owner.tests },
                     { "benchmarks", &owner.benchmarks } }) {
                if (item.contains(field))
                    *destination = require_object(item, field, context, errors);
            }
            artifact output;
            output.id = ref->artifact_id;
            output.kind = require_string(item, "kind", context, errors);
            if (item.contains("name"))
                output.name = require_string(item, "name", context, errors);
            output.link = authored_list(item, "dependencies", context, errors);
            owner.artifacts.push_back(std::move(output));
            value.components.push_back(std::move(owner));
        }
        return value;
    }

    bool is_facade_entry_kind(const std::string& kind) {
        return kind == "static_lib" || kind == "shared_lib"
            || kind == "interface_lib" || kind == "exe" || kind == "qt_app";
    }

    bool contains_key_conflict(
        const std::vector<std::string>& values, const std::string& candidate
    ) {
        const std::string prefix = candidate + "/";
        for (const std::string& value : values) {
            if (value == candidate) {
                return true;
            }
            if (value.rfind(prefix, 0U) == 0U) {
                return true;
            }
            const std::string other_prefix = value + "/";
            if (candidate.rfind(other_prefix, 0U) == 0U) {
                return true;
            }
        }
        return false;
    }

    void validate_external_project(
        const component& component_value, string_list* errors
    ) {
        if (!component_value.stack.contains("external_project")) {
            return;
        }

        const std::string context
            = "component.stack.external_project for " + component_value.id;
        const json& external = component_value.stack.at("external_project");
        if (!external.is_object()) {
            errors->push_back(context + " must be a JSON object");
            return;
        }

        for (const std::string field :
             { "repository", "revision", "component" }) {
            if (!external.contains(field) || !external.at(field).is_string()
                || trim_copy(external.at(field).get<std::string>()).empty()) {
                errors->push_back(
                    context + "." + field + " must be a non-empty string"
                );
            }
        }

        if (external.contains("component")
            && external.at("component").is_string()
            && !is_valid_id(external.at("component").get<std::string>())) {
            errors->push_back(
                context + ".component must match ^[a-z][a-z0-9_]*$"
            );
        }

        const fs::path root(component_value.root);
        const std::string normalized = root.lexically_normal().generic_string();
        if (root.is_absolute() || normalized == ".."
            || normalized.rfind("../", 0U) == 0U) {
            errors->push_back(
                "external component.root must be repository-relative: "
                + component_value.id
            );
        }
        if (component_value.ownership
            && (!component_value.ownership->scopes.empty()
                || !component_value.ownership->entry.empty())) {
            errors->push_back(
                "external artifacts must not declare local owns or entry: "
                + component_value.id
            );
        }
        if (!component_value.modules.empty()
            || !component_value.file_units.empty()) {
            errors->push_back(
                "external components must not redeclare repository-owned "
                "modules or file_units: "
                + component_value.id
            );
        }
        for (const artifact& artifact_value : component_value.artifacts) {
            if (artifact_value.kind != "static_lib") {
                errors->push_back(
                    "external_project currently supports static_lib "
                    "artifacts only: "
                    + component_value.id + ":" + artifact_value.id
                );
            }
            if (trim_copy(artifact_value.name).empty()) {
                errors->push_back(
                    "external_project artifacts require their installed "
                    "output name: "
                    + component_value.id + ":" + artifact_value.id
                );
            }
        }
    }

} // namespace manifest_support

using namespace manifest_support;

manifest_report load_manifest(const fs::path& manifest_path) {
    manifest_report report;
    report.path = manifest_path;
    std::error_code status_error;
    report.has_manifest
        = std::filesystem::symlink_status(manifest_path, status_error).type()
        != std::filesystem::file_type::not_found;
    report.errors = validate_project_paths(
        manifest_path.parent_path(), { manifest_path.filename() }
    );
    if (!report.errors.empty()) {
        return report;
    }
    std::ifstream file(manifest_path);
    if (!file.is_open()) {
        report.errors.push_back("missing manifest: " + manifest_path.string());
        return report;
    }

    try {
        json root = json::parse(file);
        if (!root.is_object()) {
            report.errors.push_back("manifest top-level must be a JSON object");
            return report;
        }
        report.has_manifest = true;
        report.value = parse_manifest(root, &report.errors);
        if (report.value.has_value()) {
            const string_list validation_errors = validate_manifest_paths(
                *report.value, manifest_path.parent_path()
            );
            report.errors.insert(
                report.errors.end(), validation_errors.begin(),
                validation_errors.end()
            );
        }
        if (report.value && report.errors.empty()) {
            const auto discovery_errors = discover_owned_files(
                &*report.value, manifest_path.parent_path()
            );
            report.errors.insert(
                report.errors.end(), discovery_errors.begin(),
                discovery_errors.end()
            );
        }
    } catch (const json::exception& error) {
        report.errors.push_back(
            "invalid JSON in manifest: " + std::string(error.what())
        );
    }

    return report;
}

bool save_manifest(
    const fs::path& manifest_path, const manifest& value,
    std::string* error_message
) {
    const string_list path_errors = validate_project_paths(
        manifest_path.parent_path(), { manifest_path.filename() }
    );
    if (!path_errors.empty()) {
        *error_message = path_errors.front();
        return false;
    }
    std::error_code error;
    fs::create_directories(
        manifest_path.has_parent_path() ? manifest_path.parent_path()
                                        : fs::path("."),
        error
    );
    if (error) {
        *error_message = "unable to create "
            + manifest_path.parent_path().string() + ": " + error.message();
        return false;
    }

    std::ofstream file(manifest_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        *error_message = "unable to open " + manifest_path.string();
        return false;
    }

    file << to_json(value).dump(2) << "\n";
    file.close();
    if (!file) {
        *error_message = "unable to write " + manifest_path.string();
        return false;
    }
    return true;
}

std::string artifact_output_name(
    const manifest& manifest_value, const component& component_value,
    const artifact& artifact_value
) {
    if (!artifact_value.name.empty()) {
        return artifact_value.name;
    }

    int matching_ids = 0;
    for (const component& other_component : manifest_value.components) {
        for (const artifact& other_artifact : other_component.artifacts) {
            if (other_artifact.id == artifact_value.id) {
                ++matching_ids;
            }
        }
    }

    if (matching_ids <= 1) {
        return artifact_value.id;
    }
    return component_value.id + "_" + artifact_value.id;
}

std::vector<fs::path> artifact_output_candidates(
    const fs::path& build_dir, const artifact& artifact_value,
    const std::string& output_name
) {
    if (artifact_value.kind == "exe" || artifact_value.kind == "qt_app") {
        return {
            build_dir / output_name,
            build_dir / (output_name + ".exe"),
            build_dir / (output_name + ".app") / "Contents" / "MacOS"
                / output_name,
        };
    }
    if (artifact_value.kind == "shared_lib") {
        return {
            build_dir / ("lib" + output_name + ".so"),
            build_dir / ("lib" + output_name + ".dylib"),
            build_dir / (output_name + ".dll"),
            build_dir / ("cyg" + output_name + ".dll"),
        };
    }
    if (artifact_value.kind == "static_lib") {
        return {
            build_dir / ("lib" + output_name + ".a"),
            build_dir / (output_name + ".lib"),
        };
    }
    return {};
}

string_list validate_manifest(const manifest& value) {
    string_list errors;
    if (!is_valid_id(value.id)) {
        errors.push_back("manifest.id must match ^[a-z][a-z0-9_]*$");
    }
    if (trim_copy(value.description).empty()) {
        errors.push_back("manifest.description must not be empty");
    }
    if (!value.version.empty()
        && !std::regex_match(value.version, project_version_pattern)) {
        errors.push_back(
            "manifest.version must be a three-part semantic version"
        );
    }
    if (value.cpp_standard < 17) {
        errors.push_back("manifest.cpp_standard must be at least 17");
    }
    if (!value.android_application_id.empty()
        && !std::regex_match(
            value.android_application_id, android_application_id_pattern
        )) {
        errors.push_back(
            "manifest.android_application_id must be a reverse-DNS identifier"
        );
    }
    if (!value.android_package_source_dir.empty()) {
        const fs::path package_source(value.android_package_source_dir);
        const std::string normalized
            = package_source.lexically_normal().generic_string();
        if (package_source.is_absolute() || normalized == "."
            || normalized == ".." || normalized.rfind("../", 0U) == 0U
            || normalized != value.android_package_source_dir
            || !std::regex_match(
                value.android_package_source_dir,
                android_package_source_dir_pattern
            )) {
            errors.push_back(
                "manifest.android_package_source_dir must be a safe, "
                "non-root project-relative directory"
            );
        }
    }
    if (value.components.empty()) {
        errors.push_back("manifest.artifacts must not be empty");
    }

    std::set<std::string> component_ids;
    std::map<std::string, const artifact*> artifacts;
    std::map<std::string, std::string> output_owners;
    std::map<std::string, std::string> target_owners;
    for (const component& component_value : value.components) {
        if (!is_valid_id(component_value.id)) {
            errors.push_back(
                "component.id must match ^[a-z][a-z0-9_]*$: "
                + component_value.id
            );
        }
        if (!component_ids.insert(component_value.id).second
            && !component_value.ownership) {
            errors.push_back("duplicate component id: " + component_value.id);
        }
        if (trim_copy(component_value.description).empty()) {
            errors.push_back(
                "component.description must not be empty: " + component_value.id
            );
        }
        if (!safe_owned_path(component_value.root, true)) {
            errors.push_back(
                "component.root must be a safe project-relative path: "
                + component_value.id
            );
        }
        validate_external_project(component_value, &errors);
        if (component_value.ownership) {
            const auto& owned = *component_value.ownership;
            if (component_value.artifacts.size() != 1)
                errors.push_back(
                    "an authored owner must contain exactly one artifact"
                );
            std::set<std::string> scopes;
            for (const auto& scope : owned.scopes) {
                if (!safe_owned_path(scope, true))
                    errors.push_back(
                        "artifact.owns must contain safe project-relative "
                        "scopes: "
                        + scope
                    );
                if (!scopes.insert(scope).second)
                    errors.push_back("duplicate ownership scope: " + scope);
            }
            if (!owned.entry.empty() && !safe_owned_path(owned.entry))
                errors.push_back(
                    "artifact.entry must be a safe root-relative file: "
                    + owned.entry
                );
            if (!owned.entry.empty()) {
                const auto ext = fs::path(owned.entry).extension().string();
                if (ext != ".cpp" && ext != ".cc" && ext != ".cxx")
                    errors.push_back(
                        "artifact.entry must name a C++ source file: "
                        + owned.entry
                    );
            }
            if (!component_value.artifacts.empty()) {
                const auto& item = component_value.artifacts.front();
                const bool runnable
                    = item.kind == "exe" || item.kind == "qt_app";
                if (runnable && owned.entry.empty())
                    errors.push_back(
                        "runnable artifact requires an explicit entry: "
                        + component_value.id + ":" + item.id
                    );
                if (!runnable && !owned.entry.empty())
                    errors.push_back(
                        "library artifact must not declare an entry: "
                        + component_value.id + ":" + item.id
                    );
            }
        }

        std::vector<std::string> seen_modules;
        for (const std::string& module_path : component_value.modules) {
            if (!safe_owned_path(module_path)) {
                errors.push_back(
                    "component module must be a safe project-relative path: "
                    + component_value.id + ":" + module_path
                );
                continue;
            }
            if (contains_key_conflict(seen_modules, module_path)) {
                errors.push_back(
                    "component modules must not overlap as both leaf and group "
                    "paths: "
                    + component_value.id + ":" + module_path
                );
                continue;
            }
            seen_modules.push_back(module_path);
        }

        std::set<std::string> artifact_ids;
        for (const artifact& artifact_value : component_value.artifacts) {
            if (!is_valid_id(artifact_value.id)) {
                errors.push_back(
                    "artifact.id must match ^[a-z][a-z0-9_]*$: "
                    + component_value.id + ":" + artifact_value.id
                );
            }
            if (!artifact_ids.insert(artifact_value.id).second) {
                errors.push_back(
                    "duplicate artifact id in component " + component_value.id
                    + ": " + artifact_value.id
                );
            }
            if (!artifact_kinds.contains(artifact_value.kind)) {
                errors.push_back(
                    "unsupported artifact kind for " + component_value.id + ":"
                    + artifact_value.id + ": " + artifact_value.kind
                );
            }
            const std::string ref
                = component_value.id + ":" + artifact_value.id;
            if (!artifacts.emplace(ref, &artifact_value).second)
                errors.push_back("duplicate artifact identity: " + ref);
            const std::string target
                = component_value.id + "__" + artifact_value.id;
            if (const auto [existing, inserted]
                = target_owners.emplace(target, ref);
                !inserted && existing->second != ref) {
                errors.push_back(
                    "CMake target collision: " + existing->second + " and "
                    + ref + " produce " + target
                );
            }
            const std::string output
                = artifact_output_name(value, component_value, artifact_value);
            if (!safe_output_name(output)) {
                errors.push_back(
                    "artifact output name must be a safe filename: " + ref
                    + ": " + output
                );
            } else if (!component_value.stack.contains("external_project")) {
                for (const fs::path& path :
                     artifact_output_candidates({}, artifact_value, output)) {
                    const std::string key = path.generic_string();
                    if (const auto [existing, inserted]
                        = output_owners.emplace(key, ref);
                        !inserted && existing->second != ref) {
                        errors.push_back(
                            "artifact output collision: " + existing->second
                            + " and " + ref + " produce " + key
                        );
                    }
                }
            }
        }
        if (component_value.artifacts.empty()) {
            errors.push_back(
                "component.artifacts must not be empty: " + component_value.id
            );
        }

        std::set<std::string> file_unit_ids;
        for (const file_unit& file_unit_value : component_value.file_units) {
            if (!safe_owned_path(file_unit_value.id)) {
                errors.push_back(
                    "file_unit.id must be a safe project-relative path: "
                    + component_value.id + ":" + file_unit_value.id
                );
            }
            if (!file_unit_ids.insert(file_unit_value.id).second) {
                errors.push_back(
                    "duplicate file_unit id in component " + component_value.id
                    + ": " + file_unit_value.id
                );
            }
            if (!file_unit_kinds.contains(file_unit_value.kind)) {
                errors.push_back(
                    "unsupported file_unit kind for " + component_value.id + ":"
                    + file_unit_value.id + ": " + file_unit_value.kind
                );
            }
        }
    }

    std::vector<std::pair<fs::path, std::string>> ownership_claims;
    std::set<std::pair<std::string, std::string>> reported_overlaps;
    for (const auto& owner : value.components) {
        if (!owner.ownership || owner.artifacts.size() != 1)
            continue;
        const auto identity
            = format_artifact_ref({ owner.id, owner.artifacts.front().id });
        auto claims = ownership_candidates(owner);
        if (!owner.ownership->entry.empty())
            claims.push_back((fs::path(owner.root) / owner.ownership->entry)
                                 .lexically_normal());
        for (const auto& claim : claims) {
            for (const auto& [prior, other] : ownership_claims) {
                if (identity != other
                    && (contains_owned_path(prior, claim)
                        || contains_owned_path(claim, prior))) {
                    if (reported_overlaps.insert({ other, identity }).second)
                        errors.push_back(
                            "overlapping artifact ownership: " + other + " and "
                            + identity + " claim " + claim.generic_string()
                        );
                    break;
                }
            }
            ownership_claims.push_back({ claim, identity });
        }
    }

    // The developer surface creates test executables from declared test
    // support. Reserve their target and filenames even before test files are
    // discovered.
    for (const component& owner : value.components) {
        const auto target = component_generated_test_target(owner);
        if (!target.has_value()) {
            continue;
        }
        const std::string ref = "generated tests for " + owner.id
            + (owner.ownership ? ":" + owner.artifacts.front().id : "");
        if (const auto [existing, inserted]
            = target_owners.emplace(*target, ref);
            !inserted) {
            errors.push_back(
                "CMake target collision: " + existing->second + " and " + ref
                + " produce " + *target
            );
        }
        const artifact test_artifact { "tests", "exe", *target, {} };
        for (const fs::path& path :
             artifact_output_candidates({}, test_artifact, *target)) {
            const std::string key = path.generic_string();
            if (const auto [existing, inserted]
                = output_owners.emplace(key, ref);
                !inserted) {
                errors.push_back(
                    "artifact output collision: " + existing->second + " and "
                    + ref + " produce " + key
                );
            }
        }
    }

    for (const auto& [ref, item] : artifacts) {
        std::set<std::string> links;
        for (const std::string& link : item->link) {
            if (!links.insert(link).second) {
                errors.push_back(
                    "duplicate artifact link: " + ref + " -> " + link
                );
            }
            const auto target = artifacts.find(link);
            if (!parse_artifact_ref(link).has_value()
                || target == artifacts.end()) {
                errors.push_back(
                    "unresolved artifact link: " + ref + " -> " + link
                );
            } else if (
                target->second->kind == "exe"
                || target->second->kind == "qt_app"
            ) {
                errors.push_back(
                    "artifact link must reference a library: " + ref + " -> "
                    + link
                );
            }
        }
    }
    std::map<std::string, int> state;
    std::vector<std::string> chain;
    std::function<void(const std::string&)> visit
        = [&](const std::string& ref) {
              if (state[ref] == 2) {
                  return;
              }
              if (state[ref] == 1) {
                  std::string cycle;
                  for (auto it = std::find(chain.begin(), chain.end(), ref);
                       it != chain.end(); ++it) {
                      cycle += *it + " -> ";
                  }
                  errors.push_back("artifact dependency cycle: " + cycle + ref);
                  return;
              }
              state[ref] = 1;
              chain.push_back(ref);
              for (const auto& link : artifacts.at(ref)->link) {
                  if (artifacts.contains(link)) {
                      visit(link);
                  }
              }
              chain.pop_back();
              state[ref] = 2;
          };
    for (const auto& [ref, item] : artifacts) {
        visit(ref);
    }

    std::set<std::string> installed;
    for (const std::string& ref_text : value.install_artifacts) {
        const auto ref = parse_artifact_ref(ref_text);
        bool found = false;
        if (ref.has_value()) {
            for (const component& owner : value.components) {
                for (const artifact& item : owner.artifacts) {
                    found = found
                        || (owner.id == ref->component_id
                            && item.id == ref->artifact_id);
                }
            }
        }
        if (!found) {
            errors.push_back(
                "manifest.install_artifacts does not resolve: " + ref_text
            );
        }
        if (!installed.insert(ref_text).second) {
            errors.push_back(
                "duplicate manifest.install_artifacts entry: " + ref_text
            );
        }
    }

    const std::optional<artifact_ref> facade_ref
        = parse_artifact_ref(value.facade_entry_artifact);
    if (!facade_ref.has_value()) {
        errors.push_back("manifest.facade must be in component:artifact form");
        return errors;
    }

    bool found_facade = false;
    for (const component& component_value : value.components) {
        if (component_value.id != facade_ref->component_id) {
            continue;
        }
        for (const artifact& artifact_value : component_value.artifacts) {
            if (artifact_value.id != facade_ref->artifact_id) {
                continue;
            }
            found_facade = true;
            if (!is_facade_entry_kind(artifact_value.kind)) {
                errors.push_back(
                    "manifest.facade must reference a "
                    "facade-eligible artifact: "
                    + value.facade_entry_artifact
                );
            }
            if (artifact_value.kind == "exe" || artifact_value.kind == "qt_app") {
                for (const std::string name : { "mvp", "mvp.exe" }) {
                    const auto owner = output_owners.find(name);
                    if (owner != output_owners.end()
                        && owner->second != value.facade_entry_artifact) {
                        errors.push_back(
                            "visitor facade output collision: " + owner->second
                            + " produces " + name + "; reserved for "
                            + value.facade_entry_artifact
                        );
                    }
                }
            }
            break;
        }
    }
    if (!found_facade) {
        errors.push_back(
            "manifest.facade does not resolve: " + value.facade_entry_artifact
        );
    }

    return errors;
}

string_list
discover_owned_files(manifest* value, const fs::path& project_root) {
    string_list errors = validate_manifest(*value);
    if (!errors.empty())
        return errors;
    const fs::path base = project_root.empty() ? fs::path(".") : project_root;
    std::vector<std::pair<fs::path, std::string>> physical_scopes;
    std::set<std::pair<std::string, std::string>> overlaps;
    for (const auto& owner : value->components) {
        if (!owner.ownership)
            continue;
        const auto identity
            = format_artifact_ref({ owner.id, owner.artifacts.front().id });
        auto candidates = ownership_candidates(owner);
        if (!owner.ownership->entry.empty())
            candidates.push_back((fs::path(owner.root) / owner.ownership->entry)
                                     .lexically_normal());
        auto path_errors = validate_project_paths(base, candidates);
        errors.insert(errors.end(), path_errors.begin(), path_errors.end());
        if (!path_errors.empty())
            continue;
        for (const auto& candidate : candidates) {
            std::error_code error;
            const auto resolved = fs::weakly_canonical(base / candidate, error);
            if (error) {
                errors.push_back(
                    "unable to resolve ownership scope: " + candidate.string()
                    + ": " + error.message()
                );
                continue;
            }
            for (const auto& [prior, other] : physical_scopes) {
                if (identity != other
                    && (contains_owned_path(prior, resolved)
                        || contains_owned_path(resolved, prior))
                    && overlaps.insert({ other, identity }).second)
                    errors.push_back(
                        "overlapping artifact ownership through filesystem "
                        "paths: "
                        + other + " and " + identity + " claim "
                        + candidate.generic_string()
                    );
            }
            physical_scopes.push_back({ resolved, identity });
        }
    }
    if (!errors.empty())
        return errors;
    std::map<fs::path, std::string> owners;
    for (component& owner : value->components) {
        if (!owner.ownership)
            continue;
        auto& owned = *owner.ownership;
        owned.headers.clear();
        owned.sources.clear();
        owned.tests.clear();
        owned.benchmarks.clear();
        const std::string identity
            = format_artifact_ref({ owner.id, owner.artifacts.front().id });
        std::set<fs::path> files;
        auto accept = [&](const fs::path& path) {
            auto path_errors = validate_project_paths(base, { path });
            errors.insert(errors.end(), path_errors.begin(), path_errors.end());
            if (!path_errors.empty()
                || !files.insert(path.lexically_normal()).second)
                return;
            const std::string extension = path.extension().string();
            const bool header = extension == ".hpp" || extension == ".h"
                || extension == ".hh" || extension == ".hxx"
                || extension == ".tpp";
            const bool source = extension == ".cpp" || extension == ".cc"
                || extension == ".cxx";
            if (!header && !source)
                return;
            std::error_code error;
            const fs::path physical = fs::weakly_canonical(base / path, error);
            if (error) {
                errors.push_back(
                    "unable to resolve owned file: " + path.string() + ": "
                    + error.message()
                );
                return;
            }
            const auto [prior, inserted] = owners.emplace(physical, identity);
            if (!inserted && prior->second != identity) {
                errors.push_back(
                    "overlapping artifact ownership: " + prior->second + " and "
                    + identity + " own " + path.string()
                );
                return;
            }
            const fs::path relative
                = path.lexically_relative(fs::path(owner.root));
            if (header)
                owned.headers.push_back(path.lexically_normal());
            else if (
                *relative.begin() == "tests"
                && relative != fs::path(owned.entry)
            )
                owned.tests.push_back(path.lexically_normal());
            else if (
                *relative.begin() == "benchmarks"
                && relative != fs::path(owned.entry)
            )
                owned.benchmarks.push_back(path.lexically_normal());
            else
                owned.sources.push_back(path.lexically_normal());
        };
        std::set<fs::path> visited;
        auto inspect = [&](const fs::path& candidate) {
            auto path_errors = validate_project_paths(base, { candidate });
            errors.insert(errors.end(), path_errors.begin(), path_errors.end());
            if (!path_errors.empty())
                return;
            std::error_code error;
            if (!fs::exists(base / candidate, error) && !error)
                return;
            if (!error && fs::is_regular_file(base / candidate, error)) {
                accept(candidate);
                return;
            }
            fs::recursive_directory_iterator iterator;
            if (!error)
                iterator = fs::recursive_directory_iterator(
                    base / candidate,
                    fs::directory_options::follow_directory_symlink, error
                );
            const fs::recursive_directory_iterator end;
            while (!error && iterator != end) {
                const fs::path path = iterator->path().lexically_relative(base);
                auto entry_errors = validate_project_paths(base, { path });
                errors.insert(
                    errors.end(), entry_errors.begin(), entry_errors.end()
                );
                if (!entry_errors.empty())
                    iterator.disable_recursion_pending();
                else if (iterator->is_directory(error) && !error) {
                    const fs::path canonical
                        = fs::canonical(iterator->path(), error);
                    if (!error && !visited.insert(canonical).second)
                        iterator.disable_recursion_pending();
                } else if (!error && iterator->is_regular_file(error))
                    accept(path);
                if (!error)
                    iterator.increment(error);
            }
            if (error)
                errors.push_back(
                    "unable to inspect ownership scope " + candidate.string()
                    + ": " + error.message()
                );
        };
        for (const auto& candidate : ownership_candidates(owner))
            inspect(candidate);
        if (!owned.entry.empty())
            accept((fs::path(owner.root) / owned.entry).lexically_normal());
        for (auto* paths : { &owned.headers, &owned.sources, &owned.tests,
                             &owned.benchmarks })
            std::sort(paths->begin(), paths->end());
    }
    return errors;
}

json to_json(const manifest& value) {
    json root = json::object();
    root["id"] = value.id;
    root["description"] = value.description;
    if (!value.version.empty())
        root["version"] = value.version;
    if (value.cpp_standard != 20)
        root["cpp_standard"] = value.cpp_standard;
    root["facade"] = value.facade_entry_artifact;
    if (!value.install_artifacts.empty())
        root["install_artifacts"] = value.install_artifacts;
    if (!value.android_application_id.empty())
        root["android_application_id"] = value.android_application_id;
    if (!value.android_package_source_dir.empty())
        root["android_package_source_dir"] = value.android_package_source_dir;
    if (value.install_assets)
        root["install_assets"] = true;
    root["artifacts"] = json::array();
    for (const component& owner : value.components) {
        const bool has_library = std::any_of(
            owner.artifacts.begin(), owner.artifacts.end(),
            [](const artifact& item) {
                return item.kind == "static_lib" || item.kind == "shared_lib"
                    || item.kind == "interface_lib";
            }
        );
        for (std::size_t index = 0; index < owner.artifacts.size(); ++index) {
            const auto& item = owner.artifacts[index];
            const bool runnable = item.kind == "exe" || item.kind == "qt_app";
            json authored = json::object();
            authored["id"] = format_artifact_ref({ owner.id, item.id });
            authored["kind"] = item.kind;
            if (!item.name.empty())
                authored["name"] = item.name;
            if (owner.description != value.description)
                authored["description"] = owner.description;
            if (owner.root != ".")
                authored["root"] = owner.root;
            string_list scopes;
            std::string entry;
            if (owner.ownership) {
                scopes = owner.ownership->scopes;
                entry = owner.ownership->entry;
            } else {
                // Project internal IR (fixtures and scaffolding) into the one
                // authored model. This is not an obsolete-schema parser.
                if (!runnable || !has_library)
                    scopes = owner.modules;
                for (const auto& unit : owner.file_units) {
                    const auto leaf = fs::path(unit.id).filename().string();
                    const bool runtime = unit.kind == "source_only"
                        && (leaf == "main" || leaf.ends_with("_main"));
                    if (runtime && runnable) {
                        entry
                            = (fs::path(
                                   unit.id.starts_with("src/")
                                           || unit.id.starts_with("tests/")
                                           || unit.id.starts_with("benchmarks/")
                                       ? ""
                                       : "src"
                               )
                               / (unit.id + ".cpp"))
                                  .generic_string();
                        continue;
                    }
                    if (runtime || (runnable && has_library))
                        continue;
                    const bool qualified = unit.id.starts_with("include/")
                        || unit.id.starts_with("src/")
                        || unit.id.starts_with("tests/")
                        || unit.id.starts_with("benchmarks/");
                    if (unit.kind == "source_only")
                        scopes.push_back((fs::path(qualified ? "" : "src")
                                          / (unit.id + ".cpp"))
                                             .generic_string());
                    else if (unit.kind == "source_pair_h") {
                        scopes.push_back((fs::path(qualified ? "" : "include")
                                          / (unit.id + ".h"))
                                             .generic_string());
                        scopes.push_back((fs::path(qualified ? "" : "src")
                                          / (unit.id + ".cpp"))
                                             .generic_string());
                    } else
                        scopes.push_back(
                            (fs::path(qualified ? "" : "include")
                             / (unit.id
                                + (unit.kind == "header_only_h" ? ".h"
                                       : unit.kind == "header_template_impl"
                                       ? ""
                                       : ".hpp")))
                                .generic_string()
                        );
                }
                if (runnable && entry.empty()) {
                    std::vector<std::string> candidates;
                    for (const auto& unit : owner.file_units)
                        if (unit.kind == "source_only") {
                            candidates.push_back(
                                (fs::path(
                                     unit.id.starts_with("src/")
                                             || unit.id.starts_with("tests/")
                                             || unit.id.starts_with(
                                                 "benchmarks/"
                                             )
                                         ? ""
                                         : "src"
                                 )
                                 / (unit.id + ".cpp"))
                                    .generic_string()
                            );
                        }
                    if (candidates.size() == 1)
                        entry = candidates.front();
                    else if (candidates.empty() && owner.modules.size() == 1)
                        entry = "src/" + owner.modules.front() + ".cpp";
                }
                if (index == 0 && !owner.tests.empty()
                    && !component_is_test_only(owner))
                    scopes.push_back("tests");
                if (index == 0 && !owner.benchmarks.empty()
                    && !component_is_benchmark_only(owner))
                    scopes.push_back("benchmarks");
            }
            authored["owns"] = scopes;
            if (!entry.empty())
                authored["entry"] = entry;
            if (!item.link.empty())
                authored["dependencies"] = item.link;
            if (!owner.stack.empty())
                authored["packages"] = owner.stack;
            if ((owner.ownership || index == 0) && !owner.tests.empty())
                authored["tests"] = owner.tests;
            if ((owner.ownership || index == 0) && !owner.benchmarks.empty())
                authored["benchmarks"] = owner.benchmarks;
            root["artifacts"].push_back(std::move(authored));
        }
    }
    return root;
}

std::optional<artifact_ref> parse_artifact_ref(const std::string& value) {
    const std::size_t separator = value.find(':');
    if (separator == std::string::npos || separator == 0U
        || separator + 1U >= value.size()) {
        return std::nullopt;
    }
    if (value.find(':', separator + 1U) != std::string::npos) {
        return std::nullopt;
    }
    return artifact_ref {
        value.substr(0U, separator),
        value.substr(separator + 1U),
    };
}

std::string format_artifact_ref(const artifact_ref& value) {
    return value.component_id + ":" + value.artifact_id;
}

} // namespace ecosystem
