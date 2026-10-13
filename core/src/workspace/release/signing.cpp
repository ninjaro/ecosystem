#include "internal.hpp"

namespace ecosystem::release_detail {

std::optional<std::string>
effective_signing_key(const prerelease_signing_options& options) {
    if (options.key_id.has_value() && !options.key_id->empty()) {
        return options.key_id;
    }

    const std::string prerelease_key
        = trim(env_value("MANIFESTO_PRERELEASE_SIGN_KEY"));
    if (!prerelease_key.empty()) {
        return prerelease_key;
    }

    const std::string generic_key = trim(env_value("MANIFESTO_GPG_KEY"));
    if (!generic_key.empty()) {
        return generic_key;
    }

    return std::nullopt;
}

void remove_if_exists(const fs::path& path) {
    std::error_code error;
    fs::remove(path, error);
}

std::vector<std::string> gpg_base_command(
    const tool_status& gpg_tool,
    const prerelease_signing_options& signing_options
) {
    std::vector<std::string> command {
        gpg_tool.path,
        "--batch",
        "--yes",
    };

    const std::optional<std::string> signing_key
        = effective_signing_key(signing_options);
    if (signing_key.has_value()) {
        command.push_back("--local-user");
        command.push_back(*signing_key);
    }

    return command;
}

bool gpg_detach_sign(
    const tool_status& gpg_tool, const fs::path& input_path,
    const fs::path& output_path,
    const prerelease_signing_options& signing_options,
    std::string* error_message
) {
    std::vector<std::string> command
        = gpg_base_command(gpg_tool, signing_options);
    command.push_back("--output");
    command.push_back(output_path.string());
    command.push_back("--detach-sign");
    command.push_back(input_path.string());
    if (run_command(command, input_path.parent_path()) != 0) {
        *error_message = "unable to sign " + input_path.string();
        return false;
    }
    return true;
}

bool gpg_clearsign(
    const tool_status& gpg_tool, const fs::path& input_path,
    const fs::path& output_path,
    const prerelease_signing_options& signing_options,
    std::string* error_message
) {
    std::vector<std::string> command
        = gpg_base_command(gpg_tool, signing_options);
    command.push_back("--output");
    command.push_back(output_path.string());
    command.push_back("--clearsign");
    command.push_back(input_path.string());
    if (run_command(command, input_path.parent_path()) != 0) {
        *error_message = "unable to clear-sign " + input_path.string();
        return false;
    }
    return true;
}

bool rewrite_debian_signatures(
    const fs::path& project_root, const tool_status& gpg_tool,
    const prerelease_signing_options& signing_options,
    std::string* error_message
) {
    const fs::path distribution_dir = deb_distribution(project_root);
    const fs::path release_path = deb_release_path(project_root);
    if (!gpg_clearsign(
            gpg_tool, release_path, distribution_dir / "InRelease",
            signing_options, error_message
        )) {
        return false;
    }
    return gpg_detach_sign(
        gpg_tool, release_path, distribution_dir / "Release.gpg",
        signing_options, error_message
    );
}

void clear_debian_signatures(const fs::path& project_root) {
    const fs::path repo_dir = deb_dir(project_root);
    const fs::path distribution_dir = deb_distribution(project_root);
    remove_if_exists(distribution_dir / "InRelease");
    remove_if_exists(distribution_dir / "Release.gpg");
    remove_if_exists(repo_dir / "InRelease");
    remove_if_exists(repo_dir / "Release.gpg");
}

bool rewrite_pacman_signatures(
    const release_state& state, const manifest& manifest_value,
    const fs::path& project_root, const std::string& architecture,
    const tool_status& gpg_tool,
    const prerelease_signing_options& signing_options,
    std::string* error_message
) {
    const fs::path repo_dir = pacman_dir(project_root, architecture);
    for (const package_record& package : state.packages) {
        if (package.latest_pacman_package.empty()) {
            continue;
        }
        const fs::path package_path = repo_dir / package.latest_pacman_package;
        std::error_code error;
        if (!fs::exists(package_path, error) || error) {
            error.clear();
            continue;
        }
        if (!gpg_detach_sign(
                gpg_tool, package_path,
                fs::path(package_path.string() + ".sig"), signing_options,
                error_message
            )) {
            return false;
        }
    }

    const fs::path db_path
        = repo_dir / (package_token(manifest_value.id) + "-prerelease.db");
    if (!gpg_detach_sign(
            gpg_tool, db_path, fs::path(db_path.string() + ".sig"),
            signing_options, error_message
        )) {
        return false;
    }

    const fs::path files_path
        = repo_dir / (package_token(manifest_value.id) + "-prerelease.files");
    return gpg_detach_sign(
        gpg_tool, files_path, fs::path(files_path.string() + ".sig"),
        signing_options, error_message
    );
}

void clear_pacman_signatures(
    const manifest& manifest_value, const fs::path& project_root,
    const std::string& architecture
) {
    const fs::path repo_dir = pacman_dir(project_root, architecture);
    const fs::path db_path
        = repo_dir / (package_token(manifest_value.id) + "-prerelease.db");
    const fs::path files_path
        = repo_dir / (package_token(manifest_value.id) + "-prerelease.files");
    remove_if_exists(fs::path(db_path.string() + ".sig"));
    remove_if_exists(fs::path(files_path.string() + ".sig"));
}

} // namespace ecosystem::release_detail
