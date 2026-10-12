#include "workspace/repository.hpp"
#include "workspace/sync.hpp"

#include "workspace/tooling.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace ecosystem {
namespace repository_support {

std::string trim_copy(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

std::string normalize_generic(const fs::path& path) {
    return path.lexically_normal().generic_string();
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0U) == 0U;
}

std::string trim_trailing_slashes(const std::string& value) {
    std::size_t end = value.size();
    while (end > 0U && value[end - 1U] == '/') {
        --end;
    }
    return value.substr(0U, end);
}

bool is_git_failure(const std::string& output) {
    return starts_with(trim_copy(output), "fatal:");
}

bool is_ignored_local_directory(const std::string& directory_name) {
    return directory_name == ".git" || directory_name == ".ecosystem" || directory_name == ".idea"
        || directory_name == ".vscode" || directory_name == "build"
        || starts_with(directory_name, "build-") || starts_with(directory_name, "cmake-build")
        || directory_name == "CMakeFiles" || directory_name == ".qt";
}

bool is_generated_report_directory_name(const std::string& directory_name) {
    return directory_name == "cov" || directory_name == "bench" || directory_name == "java-report";
}

bool is_shell_extension(const fs::path& path) {
    const std::string extension = path.extension().generic_string();
    return extension == ".sh" || extension == ".bash";
}

bool has_shell_shebang(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::string first_line;
    std::getline(file, first_line);
    if (!starts_with(first_line, "#!")) {
        return false;
    }
    return first_line.find(" sh") != std::string::npos || first_line.find("/sh") != std::string::npos
        || first_line.find("bash") != std::string::npos || first_line.find("zsh") != std::string::npos
        || first_line.find("dash") != std::string::npos;
}

void append_issue(
    string_list* issues, std::set<std::string>* seen, const std::string& value
) {
    if (seen->insert(value).second) {
        issues->push_back(value);
    }
}

std::optional<std::vector<fs::path>> tracked_git_files(const fs::path& project_root) {
    if (!command_exists("git")) {
        return std::nullopt;
    }

    const std::string top_level
        = trim_copy(capture_command({ "git", "rev-parse", "--show-toplevel" }, project_root));
    if (top_level.empty() || is_git_failure(top_level)) {
        return std::nullopt;
    }

    const std::string prefix
        = trim_copy(capture_command({ "git", "rev-parse", "--show-prefix" }, project_root));
    if (is_git_failure(prefix)) {
        return std::nullopt;
    }

    const std::string listing = capture_command(
        { "git", "ls-files", "--cached", "--full-name", "--", "." },
        project_root
    );
    if (is_git_failure(listing)) {
        return std::nullopt;
    }

    const std::string project_entry = normalize_generic(
        project_root.lexically_normal().lexically_relative(fs::path(top_level).lexically_normal())
    );
    std::vector<fs::path> files;
    const auto local_paths = ignored_local_surface_paths();
    std::stringstream stream(listing);
    std::string line;
    while (std::getline(stream, line)) {
        const std::string trimmed = trim_copy(line);
        if (trimmed.empty()) {
            continue;
        }
        if (!project_entry.empty() && project_entry != "."
            && trim_trailing_slashes(trimmed) == project_entry) {
            return std::nullopt;
        }

        std::string relative = trimmed;
        if (!prefix.empty()) {
            if (!starts_with(relative, prefix)) {
                continue;
            }
            relative = relative.substr(prefix.size());
        }
        if (!relative.empty()) {
            const fs::path candidate = project_root / relative;
            std::error_code error;
            if ((fs::exists(candidate, error) && !error)
                || std::find(local_paths.begin(), local_paths.end(), fs::path(relative)) != local_paths.end()) {
                files.emplace_back(relative);
            }
        }
    }

    return files;
}

std::vector<fs::path> filesystem_files(const fs::path& project_root) {
    std::vector<fs::path> files;
    std::error_code error;
    if (!fs::exists(project_root, error) || error) {
        return files;
    }

    fs::recursive_directory_iterator iterator(project_root, error);
    const fs::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const fs::directory_entry& entry = *iterator;
        const fs::path relative = entry.path().lexically_relative(project_root);

        if (entry.is_directory(error)) {
            if (!error && is_ignored_local_directory(entry.path().filename().generic_string())) {
                iterator.disable_recursion_pending();
            }
        } else if (!error && entry.is_regular_file(error) && !relative.empty()) {
            files.push_back(relative);
        }

        error.clear();
        iterator.increment(error);
    }

    return files;
}

std::string scripts_directory_issue(const fs::path& relative_directory) {
    return normalize_generic(relative_directory) + "/: script directories are forbidden; move wrapper logic into the manifesto tool";
}

std::string local_overlay_issue(const fs::path& relative_directory) {
    return normalize_generic(relative_directory) + "/: local developer overlays must not be committed";
}

std::string makefile_issue(const fs::path& relative_path) {
    return normalize_generic(relative_path) + ": Makefiles are forbidden; use marx build and the slim facade CMakeLists.txt";
}

std::string doxygen_file_issue(const fs::path& relative_path) {
    return normalize_generic(relative_path) + ": committed Doxyfile is forbidden; use engels check doxy instead";
}

std::string shell_script_issue(const fs::path& relative_path) {
    return normalize_generic(relative_path) + ": committed shell wrappers are forbidden; use marx/engels commands instead";
}

std::string cmake_module_issue(const fs::path& relative_path) {
    return normalize_generic(relative_path) + ": extra CMake modules are forbidden; keep only the slim root facade CMakeLists.txt";
}

std::string nested_cmakelists_issue(const fs::path& relative_path) {
    return normalize_generic(relative_path) + ": nested CMakeLists.txt files are forbidden; keep only the slim repository-root facade";
}

std::string generated_coverage_issue(const fs::path& relative_directory) {
    return normalize_generic(relative_directory)
        + "/: generated coverage reports are forbidden; use engels check coverage";
}

std::string generated_benchmark_issue(const fs::path& relative_directory) {
    return normalize_generic(relative_directory)
        + "/: generated benchmark reports are forbidden; use marx benchmark";
}

std::string generated_java_report_issue(const fs::path& relative_directory) {
    return normalize_generic(relative_directory)
        + "/: generated Java reports are forbidden; use engels check java";
}

void collect_directory_issues(
    const fs::path& relative_path, string_list* issues, std::set<std::string>* seen
) {
    fs::path current;
    for (const fs::path& segment : relative_path.parent_path()) {
        current /= segment;
        const std::string directory_name = segment.generic_string();
        if (directory_name == "scripts") {
            append_issue(issues, seen, scripts_directory_issue(current));
        } else if (directory_name == ".ecosystem") {
            append_issue(issues, seen, local_overlay_issue(current));
        } else if (directory_name == "cov") {
            append_issue(issues, seen, generated_coverage_issue(current));
        } else if (directory_name == "bench") {
            append_issue(issues, seen, generated_benchmark_issue(current));
        } else if (directory_name == "java-report") {
            append_issue(issues, seen, generated_java_report_issue(current));
        }
    }
}

bool path_has_directory_segment(const fs::path& relative_path, const std::string& directory_name) {
    for (const fs::path& segment : relative_path.parent_path()) {
        if (segment.generic_string() == directory_name) {
            return true;
        }
    }
    return false;
}

bool path_has_generated_report_directory(const fs::path& relative_path) {
    for (const fs::path& segment : relative_path.parent_path()) {
        if (is_generated_report_directory_name(segment.generic_string())) {
            return true;
        }
    }
    return false;
}

}  // namespace repository_support

using namespace repository_support;

string_list forbidden_repository_entries(const fs::path& project_root) {
    const std::optional<std::vector<fs::path>> tracked_files = tracked_git_files(project_root);
    const std::vector<fs::path> files
        = tracked_files.has_value() ? *tracked_files : filesystem_files(project_root);

    string_list issues;
    std::set<std::string> seen;
    const auto local_paths = ignored_local_surface_paths();
    for (const fs::path& relative_path : files) {
        if (tracked_files.has_value()
            && std::find(local_paths.begin(), local_paths.end(), relative_path)
                != local_paths.end()) {
            append_issue(&issues, &seen, relative_path.generic_string()
                + ": ignored local generated configuration must not be tracked");
        }
        collect_directory_issues(relative_path, &issues, &seen);
        if (path_has_directory_segment(relative_path, "scripts")
            || path_has_directory_segment(relative_path, ".ecosystem")
            || path_has_generated_report_directory(relative_path)) {
            continue;
        }

        const std::string filename = relative_path.filename().generic_string();
        if (filename == "Makefile") {
            append_issue(&issues, &seen, makefile_issue(relative_path));
            continue;
        }
        if (filename == "Doxyfile") {
            append_issue(&issues, &seen, doxygen_file_issue(relative_path));
            continue;
        }
        if (filename == "CMakeLists.txt" && relative_path != fs::path("CMakeLists.txt")) {
            append_issue(&issues, &seen, nested_cmakelists_issue(relative_path));
            continue;
        }
        if (relative_path.extension() == ".cmake") {
            append_issue(&issues, &seen, cmake_module_issue(relative_path));
            continue;
        }
        if (is_shell_extension(relative_path) || has_shell_shebang(project_root / relative_path)) {
            append_issue(&issues, &seen, shell_script_issue(relative_path));
        }
    }
    return issues;
}

}  // namespace ecosystem
