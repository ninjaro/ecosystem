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

    bool safe_output_name(const std::string& text) {
        static const std::regex pattern("^[A-Za-z0-9_][A-Za-z0-9_.+-]*$");
        return std::regex_match(text, pattern);
    }

    bool is_valid_id(const std::string& value) {
        return std::regex_match(value, id_pattern);
    }

    void flatten_modules(
        const json& value, const std::string& prefix,
        const std::string& context, std::vector<std::string>* modules,
        string_list* errors
    ) {
        if (!value.is_array()) {
            errors->push_back(context + " must be an array");
            return;
        }
        for (std::size_t index = 0; index < value.size(); ++index) {
            const json& entry = value.at(index);
            const std::string entry_context
                = context + "[" + std::to_string(index) + "]";
            if (entry.is_string()) {
                const std::string name = normalize_relative_path(
                    prefix + entry.get<std::string>()
                );
                if (name.empty() || name == ".") {
                    errors->push_back(entry_context + " must not be empty");
                    continue;
                }
                modules->push_back(name);
                continue;
            }
            if (entry.is_object() && entry.size() == 1U) {
                const auto iterator = entry.begin();
                const std::string group
                    = normalize_relative_path(iterator.key());
                if (group.empty() || group == ".") {
                    errors->push_back(
                        entry_context + " group key must not be empty"
                    );
                    continue;
                }
                flatten_modules(
                    iterator.value(), prefix + group + "/",
                    entry_context + "." + group, modules, errors
                );
                continue;
            }
            errors->push_back(
                entry_context
                + " must be a string leaf or a single-key object group"
            );
        }
    }

    struct module_node {
        bool leaf = false;
        std::map<std::string, module_node> children;
    };

    void insert_module(module_node* root, const std::string& path) {
        module_node* current = root;
        std::stringstream stream(path);
        std::string segment;
        while (std::getline(stream, segment, '/')) {
            current = &current->children[segment];
        }
        current->leaf = true;
    }

    json emit_module_array(const module_node& root) {
        json output = json::array();
        for (const auto& [name, child] : root.children) {
            if (child.leaf && child.children.empty()) {
                output.push_back(name);
                continue;
            }
            json group = json::object();
            group[name] = emit_module_array(child);
            output.push_back(group);
        }
        return output;
    }

    json modules_to_json(const std::vector<std::string>& modules) {
        module_node root;
        for (const std::string& module_path : modules) {
            insert_module(&root, module_path);
        }
        return emit_module_array(root);
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

    artifact parse_artifact(
        const json& object, const std::string& context, string_list* errors
    ) {
        artifact value;
        value.id = require_string(object, "id", context, errors);
        value.kind = require_string(object, "kind", context, errors);
        if (object.contains("name")) {
            value.name = require_string(object, "name", context, errors);
        }
        value.link = read_string_list(object, "link", context, errors);
        return value;
    }

    file_unit parse_file_unit(
        const json& object, const std::string& context, string_list* errors
    ) {
        file_unit value;
        value.id = require_string(object, "id", context, errors);
        value.kind = require_string(object, "kind", context, errors);
        return value;
    }

    component parse_component(
        const json& object, const std::string& context, string_list* errors
    ) {
        component value;
        value.id = require_string(object, "id", context, errors);
        value.description
            = require_string(object, "description", context, errors);
        value.root = normalize_relative_path(
            require_string(object, "root", context, errors)
        );
        if (value.root.empty()) {
            value.root = ".";
        }

        if (object.contains("stack")) {
            if (!object.at("stack").is_object()) {
                errors->push_back(context + ".stack must be a JSON object");
            } else {
                value.stack = object.at("stack");
            }
        }

        if (object.contains("tests")) {
            if (!object.at("tests").is_object()) {
                errors->push_back(context + ".tests must be a JSON object");
            } else {
                value.tests = object.at("tests");
            }
        }

        if (object.contains("benchmarks")) {
            if (!object.at("benchmarks").is_object()) {
                errors->push_back(
                    context + ".benchmarks must be a JSON object"
                );
            } else {
                value.benchmarks = object.at("benchmarks");
            }
        }

        if (!object.contains("modules")) {
            errors->push_back(context + ".modules must be an array");
        } else {
            flatten_modules(
                object.at("modules"), "", context + ".modules", &value.modules,
                errors
            );
        }

        if (!object.contains("artifacts")
            || !object.at("artifacts").is_array()) {
            errors->push_back(context + ".artifacts must be an array");
        } else {
            for (std::size_t index = 0; index < object.at("artifacts").size();
                 ++index) {
                const json& entry = object.at("artifacts").at(index);
                if (!entry.is_object()) {
                    errors->push_back(
                        context + ".artifacts[" + std::to_string(index)
                        + "] must be a JSON object"
                    );
                    continue;
                }
                value.artifacts.push_back(parse_artifact(
                    entry,
                    context + ".artifacts[" + std::to_string(index) + "]",
                    errors
                ));
            }
        }

        if (object.contains("file_units")) {
            if (!object.at("file_units").is_array()) {
                errors->push_back(context + ".file_units must be an array");
            } else {
                for (std::size_t index = 0;
                     index < object.at("file_units").size(); ++index) {
                    const json& entry = object.at("file_units").at(index);
                    if (!entry.is_object()) {
                        errors->push_back(
                            context + ".file_units[" + std::to_string(index)
                            + "] must be a JSON object"
                        );
                        continue;
                    }
                    value.file_units.push_back(parse_file_unit(
                        entry,
                        context + ".file_units[" + std::to_string(index) + "]",
                        errors
                    ));
                }
            }
        }

        return value;
    }

    manifest parse_manifest(const json& root, string_list* errors) {
        manifest value;
        value.id = require_string(root, "id", "manifest", errors);
        value.description
            = require_string(root, "description", "manifest", errors);
        if (root.contains("version")) {
            value.version = require_string(root, "version", "manifest", errors);
        }
        value.cpp_standard
            = require_integer(root, "cpp_standard", "manifest", errors);
        if (root.contains("android_application_id")) {
            value.android_application_id = require_string(
                root, "android_application_id", "manifest", errors
            );
        }
        if (root.contains("android_package_source_dir")) {
            value.android_package_source_dir
                = normalize_relative_path(require_string(
                    root, "android_package_source_dir", "manifest", errors
                ));
        }
        if (root.contains("install_assets")) {
            if (!root.at("install_assets").is_boolean()) {
                errors->push_back("manifest.install_assets must be a boolean");
            } else {
                value.install_assets = root.at("install_assets").get<bool>();
            }
        }

        const json facade = require_object(root, "facade", "manifest", errors);
        value.facade_entry_artifact = require_string(
            facade, "entry_artifact", "manifest.facade", errors
        );
        if (root.contains("install_artifacts")) {
            if (!root.at("install_artifacts").is_array()) {
                errors->push_back(
                    "manifest.install_artifacts must be an array"
                );
            } else {
                for (const json& entry : root.at("install_artifacts")) {
                    if (!entry.is_string()) {
                        errors->push_back(
                            "manifest.install_artifacts entries must be "
                            "artifact references"
                        );
                    } else {
                        value.install_artifacts.push_back(
                            entry.get<std::string>()
                        );
                    }
                }
            }
        }

        if (!root.contains("components") || !root.at("components").is_array()) {
            errors->push_back("manifest.components must be an array");
        } else {
            for (std::size_t index = 0; index < root.at("components").size();
                 ++index) {
                const json& entry = root.at("components").at(index);
                if (!entry.is_object()) {
                    errors->push_back(
                        "manifest.components[" + std::to_string(index)
                        + "] must be a JSON object"
                    );
                    continue;
                }
                value.components.push_back(parse_component(
                    entry, "manifest.components[" + std::to_string(index) + "]",
                    errors
                ));
            }
        }

        if (root.contains("relations")) {
            if (!root.at("relations").is_array()) {
                errors->push_back("manifest.relations must be an array");
            } else {
                value.relations = root.at("relations");
            }
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
        errors.push_back("manifest.components must not be empty");
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
        if (!component_ids.insert(component_value.id).second) {
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
            artifacts.emplace(ref, &artifact_value);
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

    // The developer surface creates test executables from declared test
    // support. Reserve their target and filenames even before test files are
    // discovered.
    for (const component& owner : value.components) {
        const auto target = component_generated_test_target(owner);
        if (!target.has_value()) {
            continue;
        }
        const std::string ref = "generated tests for " + owner.id;
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
        errors.push_back(
            "manifest.facade.entry_artifact must be in component:artifact form"
        );
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
                    "manifest.facade.entry_artifact must reference a "
                    "facade-eligible artifact: "
                    + value.facade_entry_artifact
                );
            }
            break;
        }
    }
    if (!found_facade) {
        errors.push_back(
            "manifest.facade.entry_artifact does not resolve: "
            + value.facade_entry_artifact
        );
    }

    return errors;
}

json to_json(const manifest& value) {
    json root = json::object();
    root["id"] = value.id;
    root["description"] = value.description;
    if (!value.version.empty()) {
        root["version"] = value.version;
    }
    root["cpp_standard"] = value.cpp_standard;
    if (!value.android_application_id.empty()) {
        root["android_application_id"] = value.android_application_id;
    }
    if (!value.android_package_source_dir.empty()) {
        root["android_package_source_dir"] = value.android_package_source_dir;
    }
    if (value.install_assets) {
        root["install_assets"] = true;
    }

    json facade = json::object();
    facade["entry_artifact"] = value.facade_entry_artifact;
    root["facade"] = facade;
    if (!value.install_artifacts.empty()) {
        root["install_artifacts"] = value.install_artifacts;
    }

    json components_json = json::array();
    for (const component& component_value : value.components) {
        json component_json = json::object();
        component_json["id"] = component_value.id;
        component_json["description"] = component_value.description;
        component_json["root"] = component_value.root;
        if (!component_value.stack.empty()) {
            component_json["stack"] = component_value.stack;
        }
        component_json["modules"] = modules_to_json(component_value.modules);

        if (!component_value.file_units.empty()) {
            json file_units_json = json::array();
            for (const file_unit& file_unit_value :
                 component_value.file_units) {
                json file_unit_json = json::object();
                file_unit_json["id"] = file_unit_value.id;
                file_unit_json["kind"] = file_unit_value.kind;
                file_units_json.push_back(file_unit_json);
            }
            component_json["file_units"] = file_units_json;
        }

        json artifacts_json = json::array();
        for (const artifact& artifact_value : component_value.artifacts) {
            json artifact_json = json::object();
            artifact_json["id"] = artifact_value.id;
            artifact_json["kind"] = artifact_value.kind;
            if (!artifact_value.name.empty()) {
                artifact_json["name"] = artifact_value.name;
            }
            if (!artifact_value.link.empty()) {
                artifact_json["link"] = artifact_value.link;
            }
            artifacts_json.push_back(artifact_json);
        }
        component_json["artifacts"] = artifacts_json;

        if (!component_value.tests.empty()) {
            component_json["tests"] = component_value.tests;
        }
        if (!component_value.benchmarks.empty()) {
            component_json["benchmarks"] = component_value.benchmarks;
        }

        components_json.push_back(component_json);
    }
    root["components"] = components_json;
    if (!value.relations.empty()) {
        root["relations"] = value.relations;
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
