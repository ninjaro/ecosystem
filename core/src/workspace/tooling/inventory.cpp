#include "internal.hpp"
#include "workspace/tooling.hpp"

#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

namespace ecosystem {
using namespace tooling_detail;

namespace {

    struct tool_requirement {
        std::string name;
        std::string label;
        string_list profiles;
        string_list version_args { "--version" };
        bool required = true;
    };

    const std::vector<tool_requirement>& requirements() {
        static const string_list desktop_profiles {
            "debug", "release", "kde",  "tests", "coverage",
            "leaks", "tidy",    "java", "ci",
        };
        // Doctor and the machine-readable inventory select from this same
        // table.
        static const std::vector<tool_requirement> requirements {
            { "cmake",
              "cmake",
              { "debug", "release", "kde", "android", "tests", "coverage",
                "leaks", "tidy", "java", "ci" } },
            { "clang++",
              "clang++",
              { "debug", "release", "kde", "tests", "coverage", "leaks", "tidy",
                "naming", "style", "java", "ci" } },
            { "clang", "clang", desktop_profiles, { "--version" }, false },
            { "ctest", "ctest", { "tests", "coverage", "leaks", "ci" } },
            { "clang-format", "clang-format", { "format", "ci" } },
            { "clang-tidy", "clang-tidy", { "tidy", "ci" } },
            { "doxygen", "doxygen", { "doxy", "sphinx" } },
            { "dot", "Graphviz dot", { "doxy", "sphinx" }, { "-V" } },
            { "sphinx-build", "sphinx-build", { "sphinx" } },
            { "latexmk", "latexmk", { "tex" }, { "-v" } },
            { "pdflatex", "pdflatex", { "tex" } },
            { "llvm-cov", "llvm-cov", { "coverage" } },
            { "llvm-profdata", "llvm-profdata", { "coverage" } },
            { "java", "java", { "java" }, { "-version" } },
            { "javac", "javac", { "java" }, { "-version" } },
            { "gradle", "gradle (or java/gradlew)", { "java" } },
        };
        return requirements;
    }

    std::string first_line(const std::string& value) {
        const auto trimmed = trim(value);
        const std::size_t separator = trimmed.find('\n');
        return trim(trimmed.substr(0U, separator));
    }

} // namespace

tool_status probe_tool(
    const std::string& name, const std::vector<std::string>& version_args
) {
    tool_status status;
    status.name = name;
    status.path = find_command_path(name);
    status.available = !status.path.empty();
    if (!status.available) {
        return status;
    }

    auto selected_args = version_args;
    if (selected_args.empty()) {
        selected_args = { "--version" };
        for (const auto& requirement : requirements())
            if (requirement.name == name)
                selected_args = requirement.version_args;
    }
    std::vector<std::string> args { status.path };
    args.insert(args.end(), selected_args.begin(), selected_args.end());
    const auto result = capture_command_result(args);
    status.version_exit_code = result.exit_code;
    if (result.exit_code == 0) {
        status.version = first_line(result.output);
        const auto filename = fs::path(status.path).filename();
        std::istringstream lines(result.output);
        std::string line;
        while (std::getline(lines, line)) {
            line = trim(line);
            if (line.find("version ") != std::string::npos
                || ((filename == "gradle" || filename == "gradlew")
                    && line.starts_with("Gradle "))) {
                status.version = line;
                break;
            }
        }
    } else {
        status.version_error = trim(result.output);
    }
    return status;
}

std::string find_gradle_command_path(const fs::path& project_root) {
    if (!project_root.empty()) {
        const auto wrapper
            = find_command_path((project_root / "java/gradlew").string());
        if (!wrapper.empty())
            return wrapper;
    }
    return find_command_path("gradle");
}

json toolchains_report(
    const std::optional<std::string>& profile, const fs::path& project_root
) {
    json report = json::object();
    for (const auto& requirement : requirements()) {
        if (profile
            && std::find(
                   requirement.profiles.begin(), requirement.profiles.end(),
                   *profile
               ) == requirement.profiles.end())
            continue;
        std::string command = requirement.name;
        if (command == "gradle" && !project_root.empty()) {
            const auto selected = find_gradle_command_path(project_root);
            if (!selected.empty())
                command = selected;
        }
        const auto status = probe_tool(command, requirement.version_args);
        report[requirement.name] = json::object(
            {
                { "label", requirement.label },
                { "profiles", requirement.profiles },
                { "required", requirement.required },
                { "available", status.available },
                { "path", status.path },
                { "version", status.version },
                { "version_exit_code", status.version_exit_code },
                { "version_error", status.version_error },
            }
        );
    }
    return report;
}

} // namespace ecosystem
