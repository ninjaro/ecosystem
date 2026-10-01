#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ecosystem {

using json = nlohmann::ordered_json;
using string_list = std::vector<std::string>;

/// Artifact identity shared by authored manifests and local command selectors.
struct artifact_ref {
    /// Namespace before the colon in a namespace:artifact selector.
    std::string component_id;
    /// Artifact identifier after the colon.
    std::string artifact_id;
};

struct artifact {
    std::string id;
    std::string kind;
    std::string name;
    string_list link;
};

struct file_unit {
    std::string id;
    std::string kind;
};

struct qml_module {
    std::string uri;
    std::string version;
    string_list files;
};

struct source_ownership {
    string_list scopes;
    std::string entry;
    // Derived project-root-relative files, never serialized as authored state.
    std::vector<std::filesystem::path> headers;
    std::vector<std::filesystem::path> sources;
    std::vector<std::filesystem::path> tests;
    std::vector<std::filesystem::path> benchmarks;
    std::optional<qml_module> qml;
};

struct component {
    std::string id;
    std::string description;
    std::string root;
    json stack = json::object();
    json tests = json::object();
    json benchmarks = json::object();
    std::vector<std::string> modules;
    std::vector<artifact> artifacts;
    std::vector<file_unit> file_units;
    std::optional<source_ownership> ownership;
};

struct manifest {
    std::string id;
    std::string description;
    std::string version;
    int cpp_standard = 20;
    std::string android_application_id;
    std::string android_package_source_dir;
    bool install_assets = false;
    std::string facade_entry_artifact;
    string_list install_artifacts;
    std::vector<component> components;
    json relations = json::array();
};

/// Result of loading a manifest, including diagnostics when loading fails.
struct manifest_report {
    /// Manifest path supplied to the loader.
    std::filesystem::path path;
    /// Parsed model when available; errors may still report invalid state.
    std::optional<manifest> value;
    /// Parse, validation and owned-file discovery diagnostics.
    string_list errors;
    /// Whether a manifest file was present at the supplied path.
    bool has_manifest = false;
};

/// Load authored JSON, validate its model, and discover its owned source files.
/// @param manifest_path Path to the project's manifest.json.
/// @return The loaded model or diagnostics; inspect errors before using value.
manifest_report load_manifest(const std::filesystem::path& manifest_path);
bool save_manifest(
    const std::filesystem::path& manifest_path, const manifest& value,
    std::string* error_message
);

std::string artifact_output_name(
    const manifest& manifest_value, const component& component_value,
    const artifact& artifact_value
);
std::vector<std::filesystem::path> artifact_output_candidates(
    const std::filesystem::path& build_dir, const artifact& artifact_value,
    const std::string& output_name
);

string_list validate_manifest(const manifest& value);
string_list discover_owned_files(
    manifest* value, const std::filesystem::path& project_root
);
// Project-relative ownership scopes and companion candidates, including entry
// and QML paths. Does not require files to exist (e.g. deleted change inputs).
std::vector<std::filesystem::path>
owned_path_candidates(const component& owner);
json to_json(const manifest& value);

/// Split a namespace:artifact selector; return no value for invalid syntax.
/// @param value Selector containing exactly one colon and two nonempty parts.
/// @return Parsed identity, or std::nullopt when the separator shape is invalid.
std::optional<artifact_ref> parse_artifact_ref(const std::string& value);
/// Join an artifact identity as namespace:artifact without resolving its owner.
/// @param value Identity to format.
/// @return The colon-separated selector.
std::string format_artifact_ref(const artifact_ref& value);

} // namespace ecosystem
