#include "workspace/release.hpp"
#include "release/internal.hpp"

#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

namespace ecosystem {
using namespace release_detail;

fs::path local_prerelease_dir(const fs::path& project_root) {
    return local_state_dir(project_root) / "prerelease";
}

std::string prerelease_package_name(
    const manifest& manifest_value, const artifact_ref& ref,
    const artifact& artifact_value
) {
    const std::optional<artifact_ref> facade_ref
        = parse_artifact_ref(manifest_value.facade_entry_artifact);
    const std::string project_name = package_token(manifest_value.id);
    if (facade_ref.has_value()
        && format_artifact_ref(*facade_ref) == format_artifact_ref(ref)) {
        return project_name;
    }
    return project_name + "-" + package_token(ref.component_id) + "-"
        + package_token(artifact_value.id);
}

/** Prepare archives and indexes in the publication candidate tree. */
static command_error prepare_packages(
    const fs::path& project_root, const fs::path& publication_project_root,
    const manifest& manifest_value, const resolved_artifact& resolved,
    const fs::path& built_artifact_path,
    const std::optional<std::string>& version_base,
    const prerelease_signing_options& signing_options,
    prerelease_version* version, prerelease_artifacts* artifacts,
    std::string* error_message
) {
    const tool_status tar_tool = probe_tool("tar");
    const tool_status ar_tool = probe_tool("ar");
    const tool_status gzip_tool = probe_tool("gzip");
    const tool_status sha256_tool = probe_tool("sha256sum");
    const tool_status gpg_tool = probe_tool("gpg");
    if (!tar_tool.available || !ar_tool.available || !gzip_tool.available) {
        *error_message = "prerelease packaging requires tar, ar, and gzip";
        return command_error::missing_local_tooling;
    }
    if (signing_options.sign && !gpg_tool.available) {
        *error_message = "prerelease signing requires gpg";
        return command_error::missing_local_tooling;
    }

    std::string state_error;
    release_state state = load_state(publication_project_root, &state_error);
    if (!state_error.empty()) {
        *error_message = state_error;
        return command_error::task_failed;
    }

    package_record* package_state = nullptr;
    if (!reserve_version(
            &state,
            prerelease_package_name(
                manifest_value, resolved.ref, *resolved.artifact_value
            ),
            package_description(manifest_value, resolved),
            format_artifact_ref(resolved.ref), version_base, &package_state,
            version, error_message
        )) {
        return command_error::invalid_request;
    }

    const std::string machine = native_machine();
    const std::string debian_architecture = debian_arch(machine);
    const std::string pacman_architecture = pacman_arch(machine);
    const std::int64_t build_epoch
        = static_cast<std::int64_t>(std::time(nullptr));

    const fs::path work_dir = release_work_dir(publication_project_root)
        / (package_state->package_name + "-" + version->logical_version);
    const fs::path payload_root = work_dir / "payload";
    std::uintmax_t installed_size = 0U;
    std::vector<std::string> installed_files;
    std::map<fs::path, fs::path> payload_sources;
    if (!ensure_clean_directory(payload_root, error_message)) {
        return command_error::task_failed;
    }
    if (!stage_payload(
            project_root, resolved, built_artifact_path, payload_root,
            manifest_value.install_assets, &payload_sources, error_message
        )) {
        return command_error::task_failed;
    }

    for (const artifact_ref& companion :
         distribution_artifacts(manifest_value, resolved.ref)) {
        if (format_artifact_ref(companion)
            == format_artifact_ref(resolved.ref)) {
            continue;
        }
        const auto item = resolve_artifact(manifest_value, companion);
        if (!item.has_value()) {
            *error_message = "unresolved install artifact: "
                + format_artifact_ref(companion);
            return command_error::invalid_request;
        }
        const auto path = artifact_output_path(
            local_build_dir(project_root, "release"), *item->artifact_value,
            artifact_output_name(
                manifest_value, *item->component_value, *item->artifact_value
            )
        );
        if (!path.has_value()) {
            *error_message
                = "missing install artifact: " + format_artifact_ref(companion);
            return command_error::task_failed;
        }
        if (!stage_payload(
                project_root, *item, *path, payload_root,
                manifest_value.install_assets, &payload_sources, error_message
            )) {
            return command_error::task_failed;
        }
    }

    if (!stage_tooling_templates(
            project_root, manifest_value, resolved.ref, payload_root,
            error_message
        )) {
        return command_error::task_failed;
    }
    std::set<fs::path> system_libraries;
    if (!stage_runtime(
            project_root, payload_root, work_dir, &payload_sources,
            &system_libraries, error_message
        )) {
        return command_error::task_failed;
    }
    if (!resolve_runtime_packages(
            system_libraries, package_state->package_name, work_dir, artifacts,
            error_message
        ))
        return command_error::task_failed;
    installed_size = directory_file_size(payload_root);
    installed_files = collect_relative_files(payload_root);

    artifacts->deb_repo_dir = deb_dir(publication_project_root);
    artifacts->pacman_repo_dir
        = pacman_dir(publication_project_root, pacman_architecture);
    std::error_code error;
    fs::create_directories(artifacts->deb_repo_dir, error);
    if (error) {
        *error_message = "unable to create " + artifacts->deb_repo_dir.string()
            + ": " + error.message();
        return command_error::task_failed;
    }
    error.clear();
    fs::create_directories(artifacts->pacman_repo_dir, error);
    if (error) {
        *error_message = "unable to create "
            + artifacts->pacman_repo_dir.string() + ": " + error.message();
        return command_error::task_failed;
    }

    command_error status = create_debian_package(
        work_dir,
        deb_pool_dir(publication_project_root, package_state->package_name),
        tar_tool, ar_tool, package_state->package_name, *version,
        debian_architecture, package_state->description, package_author(),
        payload_root, installed_size, artifacts->debian_dependencies,
        &artifacts->deb_package_path, error_message
    );
    if (status != command_error::ok) {
        return status;
    }

    status = create_pacman_package(
        work_dir, artifacts->pacman_repo_dir, tar_tool,
        package_state->package_name, *version, pacman_architecture,
        package_state->description, package_author(), payload_root,
        installed_size, build_epoch, artifacts->pacman_dependencies,
        &artifacts->pacman_package_path, error_message
    );
    if (status != command_error::ok) {
        return status;
    }

    package_state->next_prerelease = version->prerelease_number + 1;
    package_state->latest_logical_version = version->logical_version;
    package_state->latest_debian_version = version->debian_version;
    package_state->latest_debian_architecture = debian_architecture;
    package_state->latest_debian_package
        = artifacts->deb_package_path
              .lexically_relative(artifacts->deb_repo_dir)
              .generic_string();
    package_state->latest_pacman_version = version->pacman_version;
    package_state->latest_pacman_architecture = pacman_architecture;
    package_state->latest_pacman_package
        = artifacts->pacman_package_path.filename().generic_string();
    package_state->latest_installed_size = installed_size;
    package_state->latest_build_epoch = build_epoch;
    package_state->latest_files = installed_files;
    package_state->latest_pacman_dependencies = artifacts->pacman_dependencies;
    package_state->latest_debian_dependencies = artifacts->debian_dependencies;

    clear_flat_indexes(publication_project_root);
    if (!rewrite_debian_repo(
            state, manifest_value, publication_project_root, gzip_tool,
            sha256_tool, build_epoch, error_message
        )) {
        return command_error::task_failed;
    }
    if (!rewrite_pacman_repo(
            state, manifest_value, publication_project_root,
            pacman_architecture, tar_tool, sha256_tool, error_message
        )) {
        return command_error::task_failed;
    }
    if (signing_options.sign) {
        if (!rewrite_debian_signatures(
                publication_project_root, gpg_tool, signing_options,
                error_message
            )) {
            return command_error::task_failed;
        }
        if (!rewrite_pacman_signatures(
                state, manifest_value, publication_project_root,
                pacman_architecture, gpg_tool, signing_options, error_message
            )) {
            return command_error::task_failed;
        }
    } else {
        clear_debian_signatures(publication_project_root);
        clear_pacman_signatures(
            manifest_value, publication_project_root, pacman_architecture
        );
    }

    artifacts->deb_packages_path
        = deb_packages_path(publication_project_root, debian_architecture);
    artifacts->deb_packages_gz_path
        = deb_packages_gz(publication_project_root, debian_architecture);
    artifacts->pacman_db_path = artifacts->pacman_repo_dir
        / (package_token(manifest_value.id) + "-prerelease.db");
    if (!save_state(publication_project_root, state, error_message)) {
        return command_error::task_failed;
    }
    return command_error::ok;
}

namespace {

    // Limit publication to these three owned entries. Work products remain
    // available after a failed attempt; a failed rollback must never be erased
    // by a retry.
    const std::vector<fs::path> publication_entries { "deb", "pacman",
                                                      "state.json" };

    bool copy_release(
        const fs::path& source, const fs::path& destination,
        std::string* error_message
    ) {
        std::error_code error;
        fs::create_directories(destination, error);
        if (error) {
            *error_message
                = "unable to create release candidate: " + error.message();
            return false;
        }
        for (const auto& entry : publication_entries) {
            const auto path = source / entry;
            if (!fs::exists(path))
                continue;
            // Publication trees contain generated regular files/directories.
            // Never follow an alias while copying or rewriting a candidate
            // repository.
            if (fs::is_symlink(path)) {
                *error_message = "release publication entry is a symlink: "
                    + path.string();
                return false;
            }
            if (fs::is_directory(path)) {
                for (const auto& item :
                     fs::recursive_directory_iterator(path)) {
                    if (item.is_symlink()
                        || (!item.is_directory() && !item.is_regular_file())) {
                        *error_message
                            = "unsupported release publication entry: "
                            + item.path().string();
                        return false;
                    }
                }
            } else if (!fs::is_regular_file(path)) {
                *error_message
                    = "unsupported release publication entry: " + path.string();
                return false;
            }
            fs::copy(
                path, destination / entry, fs::copy_options::recursive, error
            );
            if (error) {
                *error_message = "unable to copy previous release "
                    + path.string() + ": " + error.message();
                return false;
            }
        }
        return true;
    }

    bool publish_candidate(
        const fs::path& candidate, const fs::path& published,
        const fs::path& previous, std::string* error_message
    ) {
        std::vector<fs::path> saved;
        std::vector<fs::path> installed;
        std::error_code error;
        fs::create_directories(previous, error);
        if (error) {
            *error_message = "unable to create previous-release storage: "
                + error.message();
            return false;
        }
        for (const auto& entry : publication_entries) {
            const bool existed = fs::exists(published / entry, error);
            if (error)
                break;
            if (existed) {
                fs::rename(published / entry, previous / entry, error);
                if (error)
                    break;
                saved.push_back(entry);
            }
            fs::rename(candidate / entry, published / entry, error);
            if (error)
                break;
            installed.push_back(entry);
        }
        if (!error) {
            // Failure to clean the backup is visible on the next invocation,
            // which refuses to discard it. The newly published release itself
            // is complete.
            fs::remove_all(previous, error);
            return true;
        }

        *error_message = "unable to publish release: " + error.message();
        bool restored = true;
        for (auto item = installed.rbegin(); item != installed.rend(); ++item) {
            fs::rename(published / *item, candidate / *item, error);
            if (error) {
                restored = false;
                *error_message += "; unable to retire " + item->string() + ": "
                    + error.message();
            }
        }
        for (auto item = saved.rbegin(); item != saved.rend(); ++item) {
            fs::rename(previous / *item, published / *item, error);
            if (error) {
                restored = false;
                *error_message += "; unable to restore " + item->string() + ": "
                    + error.message();
            }
        }
        if (restored)
            fs::remove(previous, error);
        else
            *error_message
                += "; previous release retained at " + previous.string();
        return false;
    }

} // namespace

command_error create_prerelease_packages(
    const fs::path& project_root, const manifest& manifest_value,
    const resolved_artifact& resolved, const fs::path& built_artifact_path,
    const std::optional<std::string>& version_base,
    const prerelease_signing_options& signing_options,
    prerelease_version* version, prerelease_artifacts* artifacts,
    std::string* error_message
) try {
    *artifacts = {};
    error_message->clear();
    const auto published = local_prerelease_dir(project_root);
    const auto attempt = release_work_dir(project_root) / "publication";
    const auto previous = attempt / "previous";
    const auto candidate_project = attempt / "candidate";
    const auto candidate = local_prerelease_dir(candidate_project);
    const auto path_errors = validate_project_paths(
        project_root,
        { previous.lexically_relative(project_root),
          candidate.lexically_relative(project_root),
          (published / "deb").lexically_relative(project_root),
          (published / "pacman").lexically_relative(project_root),
          (published / "state.json").lexically_relative(project_root) }
    );
    if (!path_errors.empty()) {
        *error_message = path_errors.front();
        return command_error::task_failed;
    }
    // Containment alone permits internal aliases. An aliased work directory
    // could overlap the live repository and invalidate preparation isolation.
    for (const auto& path :
         { previous, candidate, published / "deb", published / "pacman",
           published / "state.json" }) {
        auto current = project_root;
        for (const auto& part : path.lexically_relative(project_root)) {
            current /= part;
            if (fs::is_symlink(current)) {
                *error_message = "release publication path is a symlink: "
                    + current.string();
                return command_error::task_failed;
            }
        }
    }
    if (fs::exists(previous)) {
        *error_message = "previous release awaits recovery at "
            + previous.string()
            + "; inspect it and restore the release before retrying";
        return command_error::task_failed;
    }
    if (!ensure_clean_directory(candidate_project, error_message)
        || !copy_release(published, candidate, error_message)) {
        return command_error::task_failed;
    }
    prerelease_artifacts prepared;
    const auto status = prepare_packages(
        project_root, candidate_project, manifest_value, resolved,
        built_artifact_path, version_base, signing_options, version, &prepared,
        error_message
    );
    if (status != command_error::ok)
        return status;
    // Tool probes can tolerate a native failure. A failed transcript must not
    // be ignored in the same way or permit an unrecorded publication.
    if (const auto log_error = scoped_command_log::active_error();
        !log_error.empty()) {
        *error_message = log_error;
        return command_error::task_failed;
    }
    if (!publish_candidate(candidate, published, previous, error_message)) {
        return command_error::task_failed;
    }
    for (auto* path :
         { &prepared.deb_repo_dir, &prepared.deb_package_path,
           &prepared.deb_packages_path, &prepared.deb_packages_gz_path,
           &prepared.pacman_repo_dir, &prepared.pacman_package_path,
           &prepared.pacman_db_path }) {
        *path = published / path->lexically_relative(candidate);
    }
    *artifacts = std::move(prepared);
    return command_error::ok;
} catch (const std::exception& error) {

    *error_message
        = "unable to prepare prerelease: " + std::string(error.what());
    return command_error::task_failed;
}

} // namespace ecosystem
