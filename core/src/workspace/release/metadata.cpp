#include "internal.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace ecosystem::release_detail {

struct debian_release_file {
    std::string filename;
    std::uintmax_t size = 0U;
    std::string sha256;
};

std::string join_space_separated(const std::set<std::string>& values) {
    std::ostringstream joined;
    bool first = true;
    for (const std::string& value : values) {
        if (!first) {
            joined << " ";
        }
        joined << value;
        first = false;
    }
    return joined.str();
}

std::set<std::string> debian_architectures(const release_state& state) {
    std::set<std::string> architectures;
    for (const package_record& package : state.packages) {
        if (!package.latest_debian_package.empty()
            && !package.latest_debian_architecture.empty()) {
            architectures.insert(package.latest_debian_architecture);
        }
    }
    return architectures;
}

std::string format_rfc2822_utc(const std::int64_t build_epoch) {
    const auto raw_time = static_cast<std::time_t>(build_epoch);
    const std::tm* utc = std::gmtime(&raw_time);
    if (utc == nullptr) {
        return {};
    }

    std::ostringstream formatted;
    formatted << std::put_time(utc, "%a, %d %b %Y %H:%M:%S +0000");
    return formatted.str();
}

std::string
sha256_for_file(const tool_status& sha256_tool, const fs::path& path) {
    if (!sha256_tool.available) {
        return {};
    }
    const std::string output
        = trim(capture_command({ sha256_tool.path, path.string() }));
    const std::size_t separator = output.find_first_of(" \t");
    if (separator == std::string::npos) {
        return {};
    }
    return output.substr(0U, separator);
}

bool write_packages_gz(
    const tool_status& gzip_tool, const fs::path& packages_path,
    const fs::path& packages_gz_path, std::string* error_message
) {
    // gzip writes binary bytes directly to its file. Text capture truncates at
    // embedded NUL bytes and must never carry a compressed archive.
    const auto result = capture_command_result(
        { gzip_tool.path, "-n", "-f", "-k", packages_path.string() },
        packages_path.parent_path()
    );
    if (result.exit_code != 0 || !fs::is_regular_file(packages_gz_path)
        || fs::file_size(packages_gz_path) == 0U) {
        *error_message
            = "unable to compress Debian package index\n" + result.output;
        return false;
    }
    return true;
}

std::string release_sha256(const std::vector<debian_release_file>& files) {
    std::vector<std::string> lines;
    for (const debian_release_file& file : files) {
        if (file.sha256.empty()) {
            continue;
        }
        lines.push_back(
            " " + file.sha256 + " " + std::to_string(file.size) + " "
            + file.filename
        );
    }
    if (lines.empty()) {
        return {};
    }
    return "SHA256:\n" + newline_terminated_block(lines);
}

std::string debian_release_contents(
    const manifest& manifest_value, const std::set<std::string>& architectures,
    const std::vector<debian_release_file>& files,
    const std::int64_t build_epoch, std::string* error_message
) {
    const std::string formatted_date = format_rfc2822_utc(build_epoch);
    return render_release(
        "release/debian_release.tpl",
        {
            { "project_id", manifest_value.id },
            { "date_line", optional_template_line("Date: ", formatted_date) },
            { "architectures_line",
              optional_template_line(
                  "Architectures: ", join_space_separated(architectures)
              ) },
            { "description", manifest_value.description },
            { "sha256_block", release_sha256(files) },
        },
        error_message
    );
}

std::string debian_package_entry(
    const package_record& package, const fs::path& package_path,
    const tool_status& sha256_tool, std::string* error_message
) {
    std::error_code error;
    return render_release(
        "release/debian_packages_entry.tpl",
        {
            { "package_name", package.package_name },
            { "debian_version", package.latest_debian_version },
            { "dependencies",
              debian_dependency_line(package.latest_debian_dependencies) },
            { "architecture", package.latest_debian_architecture },
            { "packager", package_author() },
            { "description", package.description },
            { "filename", package.latest_debian_package },
            { "size", std::to_string(fs::file_size(package_path, error)) },
            { "sha256_line",
              optional_template_line(
                  "SHA256: ", sha256_for_file(sha256_tool, package_path)
              ) },
        },
        error_message
    );
}

bool write_debian_release_file(
    const release_state& state, const manifest& manifest_value,
    const fs::path& project_root, const tool_status& sha256_tool,
    const std::int64_t build_epoch, std::string* error_message
) {
    const fs::path distribution_dir = deb_distribution(project_root);
    std::error_code error;
    const std::set<std::string> architectures = debian_architectures(state);

    std::vector<debian_release_file> files;
    for (const std::string& architecture : architectures) {
        for (const fs::path& path : {
                 deb_packages_path(project_root, architecture),
                 deb_packages_gz(project_root, architecture),
             }) {
            if (!fs::exists(path, error) || error) {
                error.clear();
                continue;
            }
            files.push_back({
                path.lexically_relative(distribution_dir).generic_string(),
                fs::file_size(path, error),
                sha256_for_file(sha256_tool, path),
            });
            error.clear();
        }
    }

    const std::string release_contents = debian_release_contents(
        manifest_value, architectures, files, build_epoch, error_message
    );
    if (!error_message->empty()) {
        return false;
    }
    return write_text_file(
        deb_release_path(project_root), release_contents, error_message
    );
}

bool rewrite_debian_repo(
    const release_state& state, const manifest& manifest_value,
    const fs::path& project_root, const tool_status& gzip_tool,
    const tool_status& sha256_tool, const std::int64_t build_epoch,
    std::string* error_message
) {
    const fs::path repo_dir = deb_dir(project_root);
    if (!ensure_clean_directory(
            deb_distribution(project_root), error_message
        )) {
        return false;
    }
    const std::set<std::string> architectures = debian_architectures(state);
    for (const std::string& architecture : architectures) {
        std::ostringstream packages;
        for (const package_record& package : state.packages) {
            if (package.latest_debian_package.empty()
                || package.latest_debian_architecture != architecture) {
                continue;
            }
            const fs::path package_path
                = repo_dir / package.latest_debian_package;
            std::error_code error;
            if (!fs::exists(package_path, error) || error) {
                error.clear();
                continue;
            }
            packages << debian_package_entry(
                package, package_path, sha256_tool, error_message
            );
            if (!error_message->empty()) {
                return false;
            }
        }

        const fs::path packages_path
            = deb_packages_path(project_root, architecture);
        const fs::path packages_gz_path
            = deb_packages_gz(project_root, architecture);
        if (!write_text_file(packages_path, packages.str(), error_message)) {
            return false;
        }
        if (!write_packages_gz(
                gzip_tool, packages_path, packages_gz_path, error_message
            )) {
            return false;
        }
    }
    return write_debian_release_file(
        state, manifest_value, project_root, sha256_tool, build_epoch,
        error_message
    );
}

void clear_flat_indexes(const fs::path& project_root) {
    const fs::path repo_dir = deb_dir(project_root);
    remove_if_exists(repo_dir / "Packages");
    remove_if_exists(repo_dir / "Packages.gz");
    remove_if_exists(repo_dir / "Release");
}

std::string pacman_desc_contents(
    const package_record& package, const fs::path& package_path,
    const tool_status& sha256_tool, std::string* error_message
) {
    std::error_code error;
    const std::string sha256 = sha256_for_file(sha256_tool, package_path);
    return render_release(
        "release/pacman_desc.tpl",
        {
            { "filename", package.latest_pacman_package },
            { "package_name", package.package_name },
            { "pacman_version", package.latest_pacman_version },
            { "dependencies_block",
              package.latest_pacman_dependencies.empty()
                  ? std::string()
                  : "%DEPENDS%\n"
                      + newline_terminated_block(
                          package.latest_pacman_dependencies
                      )
                      + "\n" },
            { "description", package.description },
            { "compressed_size",
              std::to_string(fs::file_size(package_path, error)) },
            { "installed_size", std::to_string(package.latest_installed_size) },
            { "architecture", package.latest_pacman_architecture },
            { "build_epoch", std::to_string(package.latest_build_epoch) },
            { "packager", package_author() },
            { "sha256_block",
              sha256.empty() ? std::string()
                             : "%SHA256SUM%\n" + sha256 + "\n\n" },
        },
        error_message
    );
}

std::string pacman_files_contents(
    const package_record& package, std::string* error_message
) {
    return render_release(
        "release/pacman_files.tpl",
        {
            { "files_block", newline_terminated_block(package.latest_files) },
        },
        error_message
    );
}

bool write_pacman_repo_archive(
    const tool_status& tar_tool, const fs::path& work_root,
    const fs::path& archive_path, const fs::path& working_directory,
    std::string* error_message
) {
    std::error_code error;
    fs::remove(archive_path, error);
    std::vector<std::string> entries;
    for (const auto& entry : fs::directory_iterator(work_root))
        entries.push_back(entry.path().filename().string());
    std::sort(entries.begin(), entries.end());
    std::vector<std::string> command { tar_tool.path,         "-czf",
                                       archive_path.string(), "-C",
                                       work_root.string(),    "--" };
    command.insert(command.end(), entries.begin(), entries.end());
    if (run_command(command, working_directory) != 0) {
        *error_message = "unable to write Pacman repository metadata";
        return false;
    }
    return true;
}

bool rewrite_pacman_repo(
    const release_state& state, const manifest& manifest_value,
    const fs::path& project_root, const std::string& architecture,
    const tool_status& tar_tool, const tool_status& sha256_tool,
    std::string* error_message
) {
    const fs::path repo_dir = pacman_dir(project_root, architecture);
    const fs::path db_root = release_work_dir(project_root) / "pacman-db";
    const fs::path files_root = release_work_dir(project_root) / "pacman-files";
    if (!ensure_clean_directory(db_root, error_message)) {
        return false;
    }
    if (!ensure_clean_directory(files_root, error_message)) {
        return false;
    }

    for (const package_record& package : state.packages) {
        if (package.latest_pacman_package.empty()) {
            continue;
        }
        const fs::path package_path = repo_dir / package.latest_pacman_package;
        std::error_code error;
        if (!fs::exists(package_path, error) || error) {
            continue;
        }
        const fs::path db_entry_dir = db_root
            / (package.package_name + "-" + package.latest_pacman_version + "-1"
            );
        const fs::path files_entry_dir = files_root
            / (package.package_name + "-" + package.latest_pacman_version + "-1"
            );
        fs::create_directories(db_entry_dir, error);
        if (error) {
            *error_message = "unable to create " + db_entry_dir.string() + ": "
                + error.message();
            return false;
        }
        error.clear();
        fs::create_directories(files_entry_dir, error);
        if (error) {
            *error_message = "unable to create " + files_entry_dir.string()
                + ": " + error.message();
            return false;
        }
        const std::string desc_contents = pacman_desc_contents(
            package, package_path, sha256_tool, error_message
        );
        if (!error_message->empty()) {
            return false;
        }
        if (!write_text_file(
                db_entry_dir / "desc", desc_contents, error_message
            )) {
            return false;
        }
        if (!write_text_file(
                files_entry_dir / "desc", desc_contents, error_message
            )) {
            return false;
        }
        const std::string files_contents
            = pacman_files_contents(package, error_message);
        if (!error_message->empty()) {
            return false;
        }
        if (!write_text_file(
                files_entry_dir / "files", files_contents, error_message
            )) {
            return false;
        }
    }

    const std::string repo_name
        = package_token(manifest_value.id) + "-prerelease";
    const fs::path db_path = repo_dir / (repo_name + ".db");
    if (!write_pacman_repo_archive(
            tar_tool, db_root, db_path, repo_dir, error_message
        )) {
        return false;
    }
    const fs::path files_path = repo_dir / (repo_name + ".files");
    return write_pacman_repo_archive(
        tar_tool, files_root, files_path, repo_dir, error_message
    );
}

} // namespace ecosystem::release_detail
