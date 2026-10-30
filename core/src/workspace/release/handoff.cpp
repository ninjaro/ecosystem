#include "internal.hpp"

#include <algorithm>
#include <stdexcept>

namespace ecosystem {
namespace release_detail {

    void contract_text(const std::string& value) {
        if (value.empty()
            || std::any_of(value.begin(), value.end(), [](unsigned char c) {
                   return c < 32U || c == 127U;
               }))
            throw std::runtime_error(
                "identity/description is not representable in release input v1"
            );
    }

    void reject_aliases(const fs::path& path) {
        auto current = path.root_path();
        for (const auto& part : path.relative_path()) {
            current /= part;
            if (fs::is_symlink(fs::symlink_status(current)))
                throw std::runtime_error(
                    "release input path is a symlink: " + current.string()
                );
        }
    }

    std::string contract_path(const fs::path& path) {
        const auto value = path.generic_string();
        if (value.empty() || path.is_absolute()
            || path.lexically_normal() != path
            || value.find('\\') != std::string::npos
            || std::any_of(value.begin(), value.end(), [](unsigned char c) {
                   return c < 32U || c == 127U;
               }))
            throw std::runtime_error(
                "path is not representable in release input v1: " + value
            );
        for (const auto& part : path)
            if (part == ".." || part == "." || part == ".git" || part.empty())
                throw std::runtime_error("unsafe release input path: " + value);
        return value;
    }

    void require_manifest(
        const fs::path& root, const manifest& value, const std::string& expected
    ) {
        std::string error;
        const auto digest
            = prepared_file_sha256(root / "manifest.json", &error);
        const auto loaded = load_manifest(root / "manifest.json");
        if (expected.empty() || digest != expected || !error.empty()
            || !loaded.errors.empty() || !loaded.value
            || to_json(*loaded.value) != to_json(value))
            throw std::runtime_error(
                "manifest changed or preparation does not match the current "
                "manifest: "
                + error
            );
    }

    std::string clean_revision(const fs::path& root) {
        const auto git = find_command_path("git");
        if (git.empty())
            return {};
        const std::vector<std::pair<std::string, std::string>> environment {
            { "GIT_OPTIONAL_LOCKS", "0" }
        };
        const auto tracked = capture_command_result(
            { git, "ls-files", "--error-unmatch", "--", "manifest.json" }, root,
            environment
        );
        const auto status = capture_command_result(
            { git, "status", "--porcelain=v1", "--untracked-files=normal", "--",
              "." },
            root, environment
        );
        if (tracked.exit_code != 0 || status.exit_code != 0
            || !status.output.empty())
            return {};
        const auto revision = capture_command_result(
            { git, "rev-parse", "--verify", "HEAD" }, root, environment
        );
        auto value = release_detail::trim(revision.output);
        if (revision.exit_code != 0
            || (value.size() != 40 && value.size() != 64)
            || !std::all_of(value.begin(), value.end(), [](char c) {
                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
               }))
            return {};
        return value;
    }

} // namespace release_detail

std::string
prepared_file_sha256(const fs::path& path, std::string* error_message) {
    error_message->clear();
    const auto tool = find_command_path("sha256sum");
    if (tool.empty()) {
        *error_message = "prepared-product export requires sha256sum";
        return {};
    }
    if (!fs::is_regular_file(path)) {
        *error_message = "missing/nonregular prepared input: " + path.string();
        return {};
    }
    const auto result = capture_command_result(
        { tool, "--zero", "--", path.string() }, path.parent_path()
    );
    auto digest = result.output.substr(0, 64);
    if (result.exit_code != 0 || digest.size() != 64
        || result.output.size() < 67 || result.output.substr(64, 2) != "  "
        || result.output.back() != '\0'
        || !std::all_of(digest.begin(), digest.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           })) {
        *error_message = "sha256sum failed or returned invalid output (exit "
            + std::to_string(result.exit_code) + "): " + result.output;
        return {};
    }
    return digest;
}

command_error export_prepared_release_input(
    const fs::path& project_root, const manifest& manifest_value,
    const resolved_artifact& resolved, const fs::path& built_artifact_path,
    const fs::path& preparation_dir,
    const std::string& expected_manifest_sha256,
    const std::optional<std::string>& version_base, fs::path* input_file,
    std::string* error_message
) try {
    input_file->clear();
    error_message->clear();
    const auto root = fs::absolute(project_root).lexically_normal();
    const auto work = fs::absolute(preparation_dir).lexically_normal();
    const auto relative
        = release_detail::contract_path(work.lexically_relative(root));
    if (!relative.starts_with(".ecosystem/prerelease/attempts/")
        || fs::exists(work))
        throw std::runtime_error(
            "handoff requires a new directory under prerelease attempts"
        );
    release_detail::reject_aliases(root);
    release_detail::reject_aliases(work);
    const auto errors
        = validate_project_paths(root, { work.lexically_relative(root) });
    if (!errors.empty())
        throw std::runtime_error(errors.front());
    release_detail::require_manifest(
        root, manifest_value, expected_manifest_sha256
    );
    if (!resolved.component_value || !resolved.artifact_value)
        throw std::runtime_error("missing prepared product identity");
    const auto ref = format_artifact_ref(resolved.ref);
    for (const auto& text :
         { root.generic_string(), manifest_value.id, ref,
           manifest_value.facade_entry_artifact,
           release_detail::package_description(manifest_value, resolved) })
        release_detail::contract_text(text);
    const auto selected = resolve_artifact(manifest_value, resolved.ref);
    if (!selected || selected->artifact_value->kind == "interface_lib"
        || selected->artifact_value->kind != resolved.artifact_value->kind
        || selected->artifact_value->id != resolved.artifact_value->id
        || selected->component_value->id != resolved.component_value->id)
        throw std::runtime_error(
            "inconsistent or unsupported prepared product identity"
        );
    const auto output = artifact_output_path(
        local_build_dir(root, "release"), *selected->artifact_value,
        artifact_output_name(
            manifest_value, *selected->component_value,
            *selected->artifact_value
        )
    );
    if (!fs::is_regular_file(built_artifact_path))
        throw std::runtime_error(
            "missing primary output: " + built_artifact_path.string()
        );
    if (!output || !fs::equivalent(*output, built_artifact_path))
        throw std::runtime_error(
            "primary output does not match the prepared distribution"
        );
    for (const auto& artifact :
         distribution_artifacts(manifest_value, resolved.ref)) {
        const auto item = resolve_artifact(manifest_value, artifact);
        if (!item)
            throw std::runtime_error("incomplete distribution closure");
        const auto path = artifact_output_path(
            local_build_dir(root, "release"), *item->artifact_value,
            artifact_output_name(
                manifest_value, *item->component_value, *item->artifact_value
            )
        );
        if (!path)
            throw std::runtime_error(
                "missing install artifact: " + format_artifact_ref(artifact)
            );
        const auto path_errors
            = validate_project_paths(root, { path->lexically_relative(root) });
        if (!path_errors.empty())
            throw std::runtime_error(path_errors.front());
    }
    // Project identity comes from existing manifest intent, not a release
    // counter or a caller's competing version model.
    if (!manifest_value.version.empty() && version_base
        && *version_base != manifest_value.version) {
        *error_message = "--version-base contradicts manifest version";
        return command_error::invalid_request;
    }
    const auto project_version = manifest_value.version.empty()
        ? version_base
        : std::optional<std::string>(manifest_value.version);
    if (project_version
        && !std::regex_match(
            *project_version, release_detail::semver_pattern
        )) {
        *error_message
            = "prepared release version must use major.minor.patch form";
        return command_error::invalid_request;
    }
    fs::create_directories(work);
    release_detail::staged_distribution staged;
    auto status = release_detail::stage_distribution(
        root, manifest_value, resolved, built_artifact_path, work, &staged,
        error_message
    );
    if (status != command_error::ok)
        return status;
    json declarations = json::array();
    for (const auto& artifact :
         distribution_artifacts(manifest_value, resolved.ref)) {
        const auto item = resolve_artifact(manifest_value, artifact);
        if (!item)
            throw std::runtime_error("incomplete distribution closure");
        declarations.push_back(
            { { "id", format_artifact_ref(artifact) },
              { "kind", item->artifact_value->kind },
              { "role",
                format_artifact_ref(artifact) == ref ? "primary"
                                                     : "companion" } }
        );
    }
    std::map<std::string, json> ordered;
    std::set<fs::path> represented;
    for (const auto& entry :
         fs::recursive_directory_iterator(work / "payload")) {
        if (entry.is_symlink()
            || (!entry.is_directory() && !entry.is_regular_file()))
            throw std::runtime_error(
                "nonregular staged product input: " + entry.path().string()
            );
        if (entry.is_directory())
            continue;
        release_detail::reject_aliases(entry.path());
        const auto destination = release_detail::contract_path(
            entry.path().lexically_relative(work / "payload")
        );
        const auto source = release_detail::contract_path(
            entry.path().lexically_relative(root)
        );
        const auto artifact = staged.artifact_files.find(entry.path());
        const auto owner = artifact == staged.artifact_files.end()
            ? ref
            : format_artifact_ref(artifact->second);
        const auto role = artifact == staged.artifact_files.end() ? "data"
            : owner == ref                                        ? "primary"
                                                                  : "companion";
        if (artifact != staged.artifact_files.end()
            && (entry.path().filename() == "CMakeLists.txt"
                || entry.path().extension() == ".cmake"))
            throw std::runtime_error(
                "CMake surfaces cannot substitute for prepared product "
                "artifacts"
            );
        if (artifact != staged.artifact_files.end())
            represented.insert(entry.path());
        const auto size = entry.file_size();
        const auto digest = prepared_file_sha256(entry.path(), error_message);
        if (digest.empty() || entry.file_size() != size)
            throw std::runtime_error(
                "staged input changed or hashing failed: " + *error_message
            );
        if (!ordered
                 .emplace(
                     destination,
                     json { { "artifact", owner },
                            { "role", role },
                            { "source", source },
                            { "install_path", destination },
                            { "size", size },
                            { "sha256", digest } }
                 )
                 .second)
            throw std::runtime_error(
                "conflicting prepared install destinations"
            );
    }
    if (represented.size() != staged.artifact_files.size() || ordered.empty())
        throw std::runtime_error(
            "prepared distribution is missing required artifact files"
        );
    json files = json::array();
    for (const auto& [unused, file] : ordered) {
        (void)unused;
        files.push_back(file);
    }
    release_detail::require_manifest(
        root, manifest_value, expected_manifest_sha256
    );
    json provenance { { "manifest_sha256", expected_manifest_sha256 } };
    if (const auto revision = release_detail::clean_revision(root);
        !revision.empty())
        provenance["source_revision"] = revision;
    for (const auto& file : files) {
        const auto path = root / file.at("source").get<std::string>();
        release_detail::reject_aliases(path);
        if (!fs::is_regular_file(path)
            || fs::file_size(path) != file.at("size").get<std::uintmax_t>()
            || prepared_file_sha256(path, error_message)
                != file.at("sha256").get<std::string>())
            throw std::runtime_error(
                "prepared payload changed before export: " + path.string()
            );
    }
    json input {
        { "input_version", 1 },
        { "project",
          { { "root", root.generic_string() }, { "id", manifest_value.id } } },
        { "product",
          { { "artifact", ref },
            { "facade_artifact", manifest_value.facade_entry_artifact },
            { "package_name",
              prerelease_package_name(
                  manifest_value, resolved.ref, *resolved.artifact_value
              ) },
            { "description",
              release_detail::package_description(
                  manifest_value, resolved
              ) } } },
        { "distribution",
          { { "complete", true },
            { "artifacts", declarations },
            { "files", files } } },
        { "provenance", provenance }
    };
    if (project_version)
        input["version_base"] = *project_version;
    if (const auto log_error = scoped_command_log::active_error();
        !log_error.empty())
        throw std::runtime_error(log_error);
    if (!write_text_file(
            work / "input.tmp", input.dump(2) + "\n", error_message
        ))
        return command_error::task_failed;
    fs::rename(work / "input.tmp", work / "input.json");
    *input_file = work / "input.json";
    return command_error::ok;
} catch (const std::exception& error) {

    *error_message = error.what();
    return command_error::task_failed;
}

} // namespace ecosystem
