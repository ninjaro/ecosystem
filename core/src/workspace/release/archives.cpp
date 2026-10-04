#include "internal.hpp"

#include <sstream>

namespace ecosystem::release_detail {

std::string debian_dependency_line(const string_list& dependencies) {
    std::string line;
    for (const auto& dependency : dependencies)
        line += (line.empty() ? "Depends: " : ", ") + dependency;
    return line.empty() ? line : line + "\n";
}

std::string render_release(
    const fs::path& relative_path, const template_bindings& bindings,
    std::string* error_message
) {
    return render_text_template(relative_path, bindings, error_message);
}

std::string
optional_template_line(const std::string& prefix, const std::string& value) {
    if (value.empty()) {
        return {};
    }
    return prefix + value + "\n";
}

std::string newline_terminated_block(const std::vector<std::string>& lines) {
    std::ostringstream block;
    for (const std::string& line : lines) {
        block << line << "\n";
    }
    return block.str();
}

std::string debian_control_contents(
    const std::string& package_name, const prerelease_version& version,
    const std::string& architecture, const std::string& description,
    const std::string& packager, const std::uintmax_t installed_size,
    const string_list& dependencies, std::string* error_message
) {
    return render_release(
        "release/debian_control.tpl",
        {
            { "package_name", package_name },
            { "debian_version", version.debian_version },
            { "dependencies", debian_dependency_line(dependencies) },
            { "architecture", architecture },
            { "installed_size_kib",
              std::to_string((installed_size + 1023U) / 1024U) },
            { "packager", packager },
            { "description", description },
        },
        error_message
    );
}

std::string pacman_pkginfo_contents(
    const std::string& package_name, const prerelease_version& version,
    const std::string& architecture, const std::string& description,
    const std::string& packager, const std::uintmax_t installed_size,
    const std::int64_t build_epoch, const string_list& dependencies,
    std::string* error_message
) {
    std::string dependency_lines;
    for (const auto& dependency : dependencies)
        dependency_lines += "depend = " + dependency + "\n";
    return render_release(
        "release/pacman_pkginfo.tpl",
        {
            { "package_name", package_name },
            { "pacman_version", version.pacman_version },
            { "description", description },
            { "architecture", architecture },
            { "installed_size", std::to_string(installed_size) },
            { "build_epoch", std::to_string(build_epoch) },
            { "packager", packager },
            { "dependencies", dependency_lines },
        },
        error_message
    );
}

command_error create_debian_package(
    const fs::path& work_dir, const fs::path& package_dir,
    const tool_status& tar_tool, const tool_status& ar_tool,
    const std::string& package_name, const prerelease_version& version,
    const std::string& architecture, const std::string& description,
    const std::string& packager, const fs::path& payload_root,
    const std::uintmax_t installed_size, const string_list& dependencies,
    fs::path* package_path, std::string* error_message
) {
    const std::string file_name = package_name + "_" + version.debian_version
        + "_" + architecture + ".deb";
    *package_path = package_dir / file_name;

    const fs::path package_work_dir = work_dir / "deb";
    const fs::path control_dir = package_work_dir / "control";
    if (!ensure_clean_directory(control_dir, error_message)) {
        return command_error::task_failed;
    }
    const std::string control_contents = debian_control_contents(
        package_name, version, architecture, description, packager,
        installed_size, dependencies, error_message
    );
    if (!error_message->empty()) {
        return command_error::task_failed;
    }
    if (!write_text_file(
            control_dir / "control", control_contents, error_message
        )) {
        return command_error::task_failed;
    }
    if (!write_text_file(
            package_work_dir / "debian-binary", "2.0\n", error_message
        )) {
        return command_error::task_failed;
    }

    const fs::path control_archive = package_work_dir / "control.tar.gz";
    const fs::path data_archive = package_work_dir / "data.tar.gz";
    std::error_code error;
    fs::create_directories(package_path->parent_path(), error);
    if (error) {
        *error_message = "unable to create "
            + package_path->parent_path().string() + ": " + error.message();
        return command_error::task_failed;
    }
    error.clear();
    fs::remove(*package_path, error);
    if (run_command(
            {
                tar_tool.path,
                "--owner=0",
                "--group=0",
                "--numeric-owner",
                "-czf",
                control_archive.string(),
                "-C",
                control_dir.string(),
                ".",
            },
            package_work_dir
        )
        != 0) {
        *error_message = "unable to archive Debian control payload";
        return command_error::task_failed;
    }
    if (run_command(
            {
                tar_tool.path,
                "--owner=0",
                "--group=0",
                "--numeric-owner",
                "-czf",
                data_archive.string(),
                "-C",
                payload_root.string(),
                ".",
            },
            package_work_dir
        )
        != 0) {
        *error_message = "unable to archive Debian data payload";
        return command_error::task_failed;
    }
    if (run_command(
            {
                ar_tool.path,
                "r",
                package_path->string(),
                "debian-binary",
                "control.tar.gz",
                "data.tar.gz",
            },
            package_work_dir
        )
        != 0) {
        *error_message = "unable to assemble Debian package";
        return command_error::task_failed;
    }
    return command_error::ok;
}

command_error create_pacman_package(
    const fs::path& work_dir, const fs::path& repo_dir,
    const tool_status& tar_tool, const std::string& package_name,
    const prerelease_version& version, const std::string& architecture,
    const std::string& description, const std::string& packager,
    const fs::path& payload_root, const std::uintmax_t installed_size,
    const std::int64_t build_epoch, const string_list& dependencies,
    fs::path* package_path, std::string* error_message
) {
    const std::string file_name = package_name + "-" + version.pacman_version
        + "-1-" + architecture + ".pkg.tar.gz";
    *package_path = repo_dir / file_name;

    const fs::path package_root = work_dir / "pacman" / "package";
    if (!ensure_clean_directory(package_root, error_message)) {
        return command_error::task_failed;
    }
    const std::string pkginfo_contents = pacman_pkginfo_contents(
        package_name, version, architecture, description, packager,
        installed_size, build_epoch, dependencies, error_message
    );
    if (!error_message->empty()) {
        return command_error::task_failed;
    }
    if (!write_text_file(
            package_root / ".PKGINFO", pkginfo_contents, error_message
        )) {
        return command_error::task_failed;
    }

    const fs::path payload_usr = payload_root / "usr";
    std::error_code error;
    if (fs::exists(payload_usr, error) && !error) {
        if (!copy_directory_tree(
                payload_usr, package_root / "usr", error_message
            )) {
            return command_error::task_failed;
        }
    }

    fs::remove(*package_path, error);
    if (run_command(
            {
                tar_tool.path,
                "--owner=0",
                "--group=0",
                "--numeric-owner",
                "-czf",
                package_path->string(),
                "-C",
                package_root.string(),
                "--",
                ".PKGINFO",
                "usr",
            },
            work_dir
        )
        != 0) {
        *error_message = "unable to assemble Pacman package";
        return command_error::task_failed;
    }
    return command_error::ok;
}

} // namespace ecosystem::release_detail
