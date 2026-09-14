#include "analysis/clang.hpp"
#include "analysis/personal.hpp"
#include "analysis/tidy.hpp"
#include "manifest.hpp"
#include "packages/package_catalog.hpp"
#include "packages/package_metadata.hpp"
#include "packages/package_registry.hpp"
#include "packages/package_rule.hpp"
#include "packages/package_status.hpp"
#include "packages/package_summary.hpp"
#include "packages/package_surface.hpp"
#include "workspace/benchmark.hpp"
#include "workspace/doctor.hpp"
#include "workspace/doxygen.hpp"
#include "workspace/mutation.hpp"
#include "workspace/project.hpp"
#include "workspace/release.hpp"
#include "workspace/repository.hpp"
#include "workspace/sync.hpp"
#include "workspace/template_text.hpp"
#include "workspace/tooling.hpp"
#include "workspace/workspace_scope.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using ecosystem::json;

namespace ecosystem_test_support {

class temp_dir {
public:
    temp_dir() {
        const fs::path base = fs::temp_directory_path();
        for (int attempt = 0; attempt < 128; ++attempt) {
            const fs::path candidate = base
                / ("ecosystem-tests-" + std::to_string(::getpid()) + "-"
                   + std::to_string(attempt));
            std::error_code error;
            if (fs::create_directories(candidate, error)) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("unable to create temporary directory");
    }

    ~temp_dir() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void require_true(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_contains(
    const std::string& value, const std::string& expected,
    const std::string& message
) {
    if (value.find(expected) == std::string::npos) {
        throw std::runtime_error(
            message + "\nmissing: " + expected + "\nactual: " + value
        );
    }
}

void require_not_contains(
    const std::string& value, const std::string& unexpected,
    const std::string& message
) {
    if (value.find(unexpected) != std::string::npos) {
        throw std::runtime_error(
            message + "\nunexpected: " + unexpected + "\nactual: " + value
        );
    }
}

std::string join_lines(const ecosystem::string_list& lines) {
    std::ostringstream stream;
    for (const std::string& line : lines) {
        stream << line << "\n";
    }
    return stream.str();
}

const ecosystem::package_section* find_package_section(
    const std::vector<ecosystem::package_section>& sections,
    const ecosystem::package_group group
) {
    for (const ecosystem::package_section& section : sections) {
        if (section.group == group) {
            return &section;
        }
    }
    return nullptr;
}

const ecosystem::package_status_section* find_package_status_section(
    const std::vector<ecosystem::package_status_section>& sections,
    const ecosystem::package_group group
) {
    for (const ecosystem::package_status_section& section : sections) {
        if (section.group == group) {
            return &section;
        }
    }
    return nullptr;
}

const ecosystem::package_line* find_package_line(
    const ecosystem::package_section& section, const std::string& label
) {
    for (const ecosystem::package_line& line : section.packages) {
        if (line.label == label) {
            return &line;
        }
    }
    return nullptr;
}

const ecosystem::component* find_component_value(
    const ecosystem::manifest& manifest_value, const std::string& component_id
) {
    for (const ecosystem::component& component_value :
         manifest_value.components) {
        if (component_value.id == component_id) {
            return &component_value;
        }
    }
    return nullptr;
}

const ecosystem::tracked_surface_file* find_tracked_surface_file(
    const std::vector<ecosystem::tracked_surface_file>& files,
    const std::string& relative_path
) {
    for (const ecosystem::tracked_surface_file& file_value : files) {
        if (file_value.relative_path.generic_string() == relative_path) {
            return &file_value;
        }
    }
    return nullptr;
}

const ecosystem::package_status_line* find_package_status_line(
    const ecosystem::package_status_section& section, const std::string& label
) {
    for (const ecosystem::package_status_line& line : section.packages) {
        if (line.label == label) {
            return &line;
        }
    }
    return nullptr;
}

std::string read_text(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("unable to open " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void write_text(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("unable to open " + path.string());
    }
    file << contents;
}

const ecosystem::source_analysis* find_source_analysis(
    const ecosystem::cxx_analysis_report& report, const std::string& file
) {
    for (const ecosystem::source_analysis& source : report.sources) {
        if (source.file == file) {
            return &source;
        }
    }
    return nullptr;
}

const ecosystem::component_analysis_summary* find_component_analysis_summary(
    const ecosystem::cxx_analysis_report& report,
    const std::string& component_id
) {
    for (const ecosystem::component_analysis_summary& summary :
         report.component_summaries) {
        if (summary.component_id == component_id) {
            return &summary;
        }
    }
    return nullptr;
}

void write_executable_script(
    const fs::path& path, const std::string& contents
) {
    write_text(path, contents);
    fs::permissions(
        path,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add
    );
}

std::string current_path_env() {
    const char* value = std::getenv("PATH");
    return value == nullptr ? std::string() : std::string(value);
}

void write_fake_configure_and_build_cmake(const fs::path& path) {
    write_executable_script(
        path,
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "printf '%s\\n' \"$*\" >> \"$FAKE_CMAKE_LOG\"\n"
        "if [ \"${1:-}\" = \"--version\" ]; then\n"
        "  printf 'cmake version 3.30.0\\n'\n"
        "  exit 0\n"
        "fi\n"
        "if [ \"${1:-}\" = \"--build\" ]; then\n"
        "  build_dir=\"$2\"\n"
        "  target=\"\"\n"
        "  shift 2\n"
        "  while [ $# -gt 0 ]; do\n"
        "    if [ \"$1\" = \"--target\" ]; then\n"
        "      target=\"$2\"\n"
        "      shift 2\n"
        "      continue\n"
        "    fi\n"
        "    shift\n"
        "  done\n"
        "  mkdir -p \"$build_dir\"\n"
        "  if [ -n \"$target\" ]; then\n"
        "    output_name=\"${target##*__}\"\n"
        "    printf '#!/usr/bin/env bash\\nexit 0\\n' "
        "> \"$build_dir/$output_name\"\n"
        "    chmod +x \"$build_dir/$output_name\"\n"
        "  fi\n"
        "  exit 0\n"
        "fi\n"
        "build_dir=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  if [ \"$1\" = \"-B\" ]; then\n"
        "    build_dir=\"$2\"\n"
        "    shift 2\n"
        "    continue\n"
        "  fi\n"
        "  shift\n"
        "done\n"
        "if [ -n \"$build_dir\" ]; then\n"
        "  mkdir -p \"$build_dir\"\n"
        "fi\n"
        "exit 0\n"
    );
}

void write_fake_gpg_tool(const fs::path& path) {
    write_executable_script(
        path,
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "printf '%s\\n' \"$*\" >> \"$FAKE_GPG_LOG\"\n"
        "if [ \"${1:-}\" = \"--version\" ]; then\n"
        "  printf 'gpg (fake) 1.0\\n'\n"
        "  exit 0\n"
        "fi\n"
        "mode=\"\"\n"
        "output=\"\"\n"
        "input=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  case \"$1\" in\n"
        "    --output)\n"
        "      output=\"$2\"\n"
        "      shift 2\n"
        "      ;;\n"
        "    --local-user)\n"
        "      printf 'key=%s\\n' \"$2\" >> \"$FAKE_GPG_LOG\"\n"
        "      shift 2\n"
        "      ;;\n"
        "    --clearsign)\n"
        "      mode=\"clearsign\"\n"
        "      shift\n"
        "      ;;\n"
        "    --detach-sign)\n"
        "      mode=\"detach\"\n"
        "      shift\n"
        "      ;;\n"
        "    --batch|--yes)\n"
        "      shift\n"
        "      ;;\n"
        "    *)\n"
        "      input=\"$1\"\n"
        "      shift\n"
        "      ;;\n"
        "  esac\n"
        "done\n"
        "mkdir -p \"$(dirname \"$output\")\"\n"
        "if [ \"$mode\" = \"clearsign\" ]; then\n"
        "  printf 'SIGNED\\n' > \"$output\"\n"
        "  cat \"$input\" >> \"$output\"\n"
        "  exit 0\n"
        "fi\n"
        "printf 'SIG %s\\n' \"$input\" > \"$output\"\n"
        "exit 0\n"
    );
}

void write_fake_configure_cmake_with_compile_database(const fs::path& path) {
    write_executable_script(
        path,
        "#!/bin/bash\n"
        "set -euo pipefail\n"
        "printf '%s\\n' \"$*\" >> \"$FAKE_CMAKE_LOG\"\n"
        "if [ \"${1:-}\" = \"--version\" ]; then\n"
        "  printf 'cmake version 3.30.0\\n'\n"
        "  exit 0\n"
        "fi\n"
        "build_dir=\"\"\n"
        "source_dir=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  if [ \"$1\" = \"-B\" ]; then\n"
        "    build_dir=\"$2\"\n"
        "    shift 2\n"
        "    continue\n"
        "  fi\n"
        "  if [ \"$1\" = \"-S\" ]; then\n"
        "    source_dir=\"$2\"\n"
        "    shift 2\n"
        "    continue\n"
        "  fi\n"
        "  shift\n"
        "done\n"
        "mkdir -p \"$build_dir\"\n"
        "cat > \"$build_dir/compile_commands.json\" <<EOF\n"
        "[\n"
        "  {\n"
        "    \"directory\": \"$(cd \"$source_dir/../..\" && pwd)\",\n"
        "    \"file\": \"$(cd \"$source_dir/../..\" && pwd)/src/main.cpp\",\n"
        "    \"arguments\": [\n"
        "      \"clang++\",\n"
        "      \"-std=c++20\",\n"
        "      \"-c\",\n"
        "      \"$(cd \"$source_dir/../..\" && pwd)/src/main.cpp\"\n"
        "    ]\n"
        "  }\n"
        "]\n"
        "EOF\n"
        "exit 0\n"
    );
}

void write_fake_clang_tidy_tool(const fs::path& path) {
    write_executable_script(
        path,
        "#!/bin/bash\n"
        "set -euo pipefail\n"
        "if [ -n \"${FAKE_CLANG_TIDY_LOG:-}\" ]; then\n"
        "  printf '%s\\n' \"$*\" >> \"$FAKE_CLANG_TIDY_LOG\"\n"
        "fi\n"
        "if [ \"${1:-}\" = \"--version\" ]; then\n"
        "  printf 'LLVM clang-tidy 20.1.0\\n'\n"
        "  exit 0\n"
        "fi\n"
        "if [ -n \"${FAKE_CLANG_TIDY_OUTPUT:-}\" ]; then\n"
        "  printf '%s\\n' \"$FAKE_CLANG_TIDY_OUTPUT\"\n"
        "fi\n"
        "exit \"${FAKE_CLANG_TIDY_EXIT_CODE:-0}\"\n"
    );
}

class scoped_env {
public:
    scoped_env(const std::string& name, const std::string& value)
        : name_(name) {
        const char* previous = std::getenv(name.c_str());
        if (previous != nullptr) {
            had_previous_ = true;
            previous_value_ = previous;
        }
        ::setenv(name.c_str(), value.c_str(), 1);
    }

    ~scoped_env() {
        if (had_previous_) {
            ::setenv(name_.c_str(), previous_value_.c_str(), 1);
        } else {
            ::unsetenv(name_.c_str());
        }
    }

private:
    std::string name_;
    bool had_previous_ = false;
    std::string previous_value_;
};

ecosystem::manifest sample_manifest() {
    ecosystem::component component;
    component.id = "sample";
    component.description = "Sample component.";
    component.root = ".";
    component.stack = json::object();
    component.tests = json::object({ { "selftest", true } });
    component.modules = {};
    component.file_units = { ecosystem::file_unit { "main", "source_only" } };
    component.artifacts = { ecosystem::artifact { "app", "exe", {}, {} } };

    ecosystem::manifest manifest_value;
    manifest_value.id = "sample";
    manifest_value.description = "Sample self-hosting project.";
    manifest_value.cpp_standard = 20;
    manifest_value.facade_entry_artifact = "sample:app";
    manifest_value.components = { component };
    return manifest_value;
}

ecosystem::manifest sample_build_manifest(const std::string& project_id) {
    ecosystem::component component;
    component.id = project_id;
    component.description = "Sample runnable component.";
    component.root = ".";
    component.stack = json::object();
    component.tests = json::object();
    component.modules = {};
    component.file_units = { ecosystem::file_unit { "main", "source_only" } };
    component.artifacts = { ecosystem::artifact { "app", "exe", {}, {} } };

    ecosystem::manifest manifest_value;
    manifest_value.id = project_id;
    manifest_value.description = "Sample runnable project.";
    manifest_value.cpp_standard = 20;
    manifest_value.facade_entry_artifact = project_id + ":app";
    manifest_value.components = { component };
    return manifest_value;
}

void write_sample_build_project(
    const fs::path& project_root, const std::string& project_id,
    const std::string& main_source = "int main() { return 0; }\n"
) {
    fs::create_directories(project_root / "src");

    const ecosystem::manifest manifest_value
        = sample_build_manifest(project_id);
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            project_root / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary build manifest"
    );
    write_text(project_root / "src/main.cpp", main_source);
}

ecosystem::manifest sample_library_manifest() {
    ecosystem::component core;
    core.id = "core";
    core.description = "Core library component.";
    core.root = ".";
    core.modules = { "sample/core" };
    core.artifacts = { ecosystem::artifact { "lib", "static_lib", {}, {} } };

    ecosystem::manifest manifest_value;
    manifest_value.id = "library_sample";
    manifest_value.description = "Sample project with a single library facade.";
    manifest_value.cpp_standard = 20;
    manifest_value.facade_entry_artifact = "core:lib";
    manifest_value.components = { core };
    return manifest_value;
}

ecosystem::manifest sample_external_project_manifest() {
    ecosystem::component external;
    external.id = "packing_library";
    external.description = "External packing library.";
    external.root = "core";
    external.stack = json::object(
        {
            { "external_project",
              json::object(
                  {
                      { "repository", "https://github.com/ninjaro/rothko" },
                      { "revision", "manifesto" },
                      { "package", "packing" },
                      { "artifact", "library:core" },
                  }
              ) },
        }
    );
    external.artifacts = { ecosystem::artifact {
        "core",
        "static_lib",
        "packing_core",
        {},
    } };

    ecosystem::component app;
    app.id = "app";
    app.description = "External library consumer.";
    app.root = ".";
    app.file_units = { ecosystem::file_unit { "main", "source_only" } };
    app.artifacts = { ecosystem::artifact {
        "app",
        "exe",
        {},
        { "packing_library:core" },
    } };

    ecosystem::manifest manifest_value;
    manifest_value.id = "external_sample";
    manifest_value.description = "Sample with an external component.";
    manifest_value.cpp_standard = 20;
    manifest_value.facade_entry_artifact = "app:app";
    manifest_value.components = { external, app };
    return manifest_value;
}

ecosystem::manifest sample_multi_library_manifest() {
    ecosystem::manifest manifest_value = sample_library_manifest();

    ecosystem::component support;
    support.id = "support";
    support.description = "Support library component.";
    support.root = ".";
    support.modules = { "support/native" };
    support.artifacts
        = { ecosystem::artifact { "helper", "shared_lib", {}, {} } };

    manifest_value.id = "multi_library_sample";
    manifest_value.description
        = "Sample project with multiple library targets.";
    manifest_value.components.push_back(support);
    return manifest_value;
}

ecosystem::manifest sample_facade_library_closure_manifest() {
    ecosystem::component core;
    core.id = "core";
    core.description = "Core reusable library.";
    core.root = ".";
    core.modules = { "sample/core" };
    core.artifacts = { ecosystem::artifact { "core", "static_lib", {}, {} } };

    ecosystem::component api;
    api.id = "api";
    api.description = "Facade API library.";
    api.root = ".";
    api.modules = { "sample/api" };
    api.artifacts = { ecosystem::artifact {
        "api",
        "shared_lib",
        {},
        { "core:core" },
    } };

    ecosystem::component spare;
    spare.id = "spare";
    spare.description = "Unrelated helper library.";
    spare.root = ".";
    spare.modules = { "sample/spare" };
    spare.artifacts
        = { ecosystem::artifact { "helper", "shared_lib", {}, {} } };

    ecosystem::manifest manifest_value;
    manifest_value.id = "facade_library_closure";
    manifest_value.description
        = "Sample project with a facade-library closure and spare library.";
    manifest_value.cpp_standard = 20;
    manifest_value.facade_entry_artifact = "api:api";
    manifest_value.components = { core, api, spare };
    return manifest_value;
}

ecosystem::manifest sample_facade_component_library_manifest() {
    ecosystem::component backend_core;
    backend_core.id = "backend_core";
    backend_core.description = "Backend reusable core.";
    backend_core.root = ".";
    backend_core.modules = { "backend/core" };
    backend_core.artifacts
        = { ecosystem::artifact { "core", "static_lib", {}, {} } };

    ecosystem::component frontend;
    frontend.id = "frontend";
    frontend.description = "Runnable facade with an adjacent UI library.";
    frontend.root = ".";
    frontend.modules = { "frontend/ui" };
    frontend.file_units = { ecosystem::file_unit { "main", "source_only" } };
    frontend.artifacts = {
        ecosystem::artifact {
            "ui",
            "static_lib",
            {},
            { "backend_core:core" },
        },
        ecosystem::artifact { "app", "qt_app", {}, { "frontend:ui" } },
    };

    ecosystem::component spare;
    spare.id = "spare";
    spare.description = "Unrelated helper library.";
    spare.root = ".";
    spare.modules = { "spare/helper" };
    spare.artifacts
        = { ecosystem::artifact { "helper", "shared_lib", {}, {} } };

    ecosystem::manifest manifest_value;
    manifest_value.id = "facade_component_library";
    manifest_value.description
        = "Sample project with a runnable facade and adjacent library.";
    manifest_value.cpp_standard = 20;
    manifest_value.facade_entry_artifact = "frontend:app";
    manifest_value.components = { backend_core, frontend, spare };
    return manifest_value;
}

ecosystem::manifest sample_interface_library_manifest() {
    ecosystem::component api;
    api.id = "api";
    api.description = "Header-only API component.";
    api.root = ".";
    api.modules = { "sample/api" };
    api.artifacts = { ecosystem::artifact { "api", "interface_lib", {}, {} } };

    ecosystem::manifest manifest_value;
    manifest_value.id = "interface_library_sample";
    manifest_value.description = "Sample project with a header-only facade.";
    manifest_value.cpp_standard = 20;
    manifest_value.facade_entry_artifact = "api:api";
    manifest_value.components = { api };
    return manifest_value;
}

ecosystem::manifest sample_dual_run_manifest() {
    ecosystem::component app;
    app.id = "app";
    app.description = "Default runnable app.";
    app.root = ".";
    app.stack = json::object();
    app.tests = json::object();
    app.modules = {};
    app.file_units = { ecosystem::file_unit { "main", "source_only" } };
    app.artifacts = { ecosystem::artifact { "app", "exe", {}, {} } };

    ecosystem::component tool;
    tool.id = "tool";
    tool.description = "Secondary CLI entry.";
    tool.root = ".";
    tool.stack = json::object();
    tool.tests = json::object();
    tool.modules = {};
    tool.file_units = { ecosystem::file_unit { "cli_main", "source_only" } };
    tool.artifacts = { ecosystem::artifact { "cli", "exe", {}, {} } };

    ecosystem::manifest manifest_value;
    manifest_value.id = "dual_run_sample";
    manifest_value.description = "Sample project with two runnable artifacts.";
    manifest_value.cpp_standard = 20;
    manifest_value.facade_entry_artifact = "app:app";
    manifest_value.components = { app, tool };
    return manifest_value;
}

ecosystem::manifest sample_shared_runtime_manifest() {
    ecosystem::component core;
    core.id = "core";
    core.description = "Shared runtime support library.";
    core.root = ".";
    core.modules = { "sample/core" };
    core.artifacts = { ecosystem::artifact { "lib", "shared_lib", {}, {} } };

    ecosystem::component app;
    app.id = "app";
    app.description = "Runnable facade app.";
    app.root = ".";
    app.modules = {};
    app.file_units = { ecosystem::file_unit { "main", "source_only" } };
    app.artifacts
        = { ecosystem::artifact { "app", "exe", {}, { "core:lib" } } };

    ecosystem::component spare;
    spare.id = "spare";
    spare.description = "Unrelated support library.";
    spare.root = ".";
    spare.modules = { "spare/support" };
    spare.artifacts
        = { ecosystem::artifact { "helper", "shared_lib", {}, {} } };

    ecosystem::manifest manifest_value;
    manifest_value.id = "shared_runtime_sample";
    manifest_value.description
        = "Sample project with a runnable facade and linked shared library.";
    manifest_value.cpp_standard = 20;
    manifest_value.facade_entry_artifact = "app:app";
    manifest_value.components = { core, app, spare };
    return manifest_value;
}

ecosystem::manifest sample_leak_check_manifest() {
    ecosystem::component core;
    core.id = "core";
    core.description = "Core library component.";
    core.root = ".";
    core.modules = { "sample/core" };
    core.artifacts = { ecosystem::artifact { "lib", "static_lib", {}, {} } };

    ecosystem::component tests;
    tests.id = "tests";
    tests.description = "Dedicated test runner.";
    tests.root = ".";
    tests.tests = json::object({ { "selftest", true } });
    tests.modules = {};
    tests.file_units
        = { ecosystem::file_unit { "tests/main_tests", "source_only" } };
    tests.artifacts = { ecosystem::artifact {
        "tests",
        "exe",
        "",
        { "core:lib" },
    } };

    ecosystem::manifest manifest_value;
    manifest_value.id = "leak_sample";
    manifest_value.description = "Sample project for leak-check coverage.";
    manifest_value.cpp_standard = 20;
    manifest_value.facade_entry_artifact = "core:lib";
    manifest_value.components = { core, tests };
    return manifest_value;
}

ecosystem::manifest sample_java_binding_manifest() {
    ecosystem::component api;
    api.id = "api";
    api.description = "Shared API library.";
    api.root = ".";
    api.modules = { "sample/api" };
    api.artifacts = { ecosystem::artifact { "api", "shared_lib", {}, {} } };

    ecosystem::component jni;
    jni.id = "jni";
    jni.description = "JNI bridge library.";
    jni.root = ".";
    jni.stack = json::object({ { "jni", json::array({ "required" }) } });
    jni.modules = { "sample/jni" };
    jni.artifacts = { ecosystem::artifact {
        "jni",
        "shared_lib",
        {},
        { "api:api" },
    } };

    ecosystem::manifest manifest_value;
    manifest_value.id = "java_sample";
    manifest_value.description = "Sample project for Java-check coverage.";
    manifest_value.cpp_standard = 20;
    manifest_value.facade_entry_artifact = "api:api";
    manifest_value.components = { api, jni };
    return manifest_value;
}

ecosystem::manifest sample_cxxopts_manifest() {
    ecosystem::component cli;
    cli.id = "cli";
    cli.description = "CLI component using cxxopts.";
    cli.root = ".";
    cli.stack = json::object({ { "cxxopts", json::array({ "cxxopts" }) } });
    cli.modules = { "cli/tool" };
    cli.artifacts = { ecosystem::artifact { "app", "exe", {}, {} } };

    ecosystem::manifest manifest_value;
    manifest_value.id = "cxxopts_sample";
    manifest_value.description = "Sample project for cxxopts package coverage.";
    manifest_value.cpp_standard = 20;
    manifest_value.facade_entry_artifact = "cli:app";
    manifest_value.components = { cli };
    return manifest_value;
}

ecosystem::manifest sample_json_manifest() {
    ecosystem::manifest manifest_value = sample_manifest();
    manifest_value.id = "json_sample";
    manifest_value.description
        = "Sample project with a single JSON dependency.";
    manifest_value.components.front().stack
        = json::object({ { "json", json::array({ "nlohmann_json" }) } });
    return manifest_value;
}

ecosystem::manifest sample_scoped_probe_manifest() {
    ecosystem::manifest manifest_value = sample_json_manifest();
    manifest_value.components.front().tests.clear();
    manifest_value.id = "scoped_probe";
    manifest_value.description
        = "Sample project for artifact-scoped doctor probe refresh.";

    ecosystem::component kde_side;
    kde_side.id = "kde_side";
    kde_side.description = "Unrelated KDE-only component.";
    kde_side.root = ".";
    kde_side.stack = json::object({ { "kde", json::array({ "CoreAddons" }) } });
    kde_side.tests = json::object();
    kde_side.modules = { "kde_side" };
    kde_side.artifacts
        = { ecosystem::artifact { "lib", "static_lib", {}, {} } };

    manifest_value.components.push_back(kde_side);
    return manifest_value;
}

ecosystem::manifest sample_qttest_manifest() {
    ecosystem::manifest manifest_value = sample_manifest();
    manifest_value.id = "qttest_sample";
    manifest_value.description
        = "Sample project with QtTest-only package intent.";
    manifest_value.components.front().stack
        = json::object({ { "qt", json::array({ "Core", "Widgets" }) } });
    manifest_value.components.front().tests = json::object(
        {
            { "selftest", true },
            { "qttest", true },
        }
    );
    return manifest_value;
}

ecosystem::manifest sample_dependency_manifest() {
    ecosystem::component core;
    core.id = "core";
    core.description = "Core library with Qt, JSON, and optional OpenCV.";
    core.root = ".";
    core.stack = json::object(
        {
            { "json", json::array({ "nlohmann_json" }) },
            { "qt", json::array({ "Core", "Widgets" }) },
            { "opencv", json::array({ "optional" }) },
        }
    );
    core.tests = json::object();
    core.modules = { "sample/core" };
    core.file_units = {};
    core.artifacts = { ecosystem::artifact { "lib", "static_lib", {}, {} } };

    ecosystem::component app;
    app.id = "app";
    app.description = "Executable facade.";
    app.root = ".";
    app.stack = json::object();
    app.tests = json::object();
    app.modules = {};
    app.file_units = { ecosystem::file_unit { "main", "source_only" } };
    app.artifacts
        = { ecosystem::artifact { "app", "exe", {}, { "core:lib" } } };

    ecosystem::component desktop;
    desktop.id = "desktop";
    desktop.description = "Optional desktop-facing integrations.";
    desktop.root = ".";
    desktop.stack = json::object(
        {
            { "kde", json::array({ "CoreAddons", "KDEGames6" }) },
            { "jni", json::array({ "required" }) },
            { "llvm_clang", json::array({ "libclang" }) },
        }
    );
    desktop.tests = json::object();
    desktop.modules = { "desktop/support" };
    desktop.file_units = {};
    desktop.artifacts
        = { ecosystem::artifact { "support", "shared_lib", {}, {} } };

    ecosystem::component tests;
    tests.id = "tests";
    tests.description = "GTest suite.";
    tests.root = ".";
    tests.stack = json::object();
    tests.tests = json::object({ { "gtest", true } });
    tests.modules = {};
    tests.file_units
        = { ecosystem::file_unit { "tests/test_main", "source_only" } };
    tests.artifacts
        = { ecosystem::artifact { "tests", "exe", {}, { "core:lib" } } };

    ecosystem::component benchmarks;
    benchmarks.id = "benchmarks";
    benchmarks.description = "Benchmark suite.";
    benchmarks.root = ".";
    benchmarks.stack
        = json::object({ { "eigen", json::array({ "optional" }) } });
    benchmarks.tests = json::object();
    benchmarks.benchmarks = json::object({ { "google_benchmark", true } });
    benchmarks.modules = {};
    benchmarks.file_units
        = { ecosystem::file_unit { "benchmarks/bench_all", "source_only" } };
    benchmarks.artifacts
        = { ecosystem::artifact { "bench", "exe", {}, { "core:lib" } } };

    ecosystem::manifest manifest_value;
    manifest_value.id = "deps";
    manifest_value.description = "Dependency-heavy sample project.";
    manifest_value.cpp_standard = 20;
    manifest_value.facade_entry_artifact = "app:app";
    manifest_value.components = { core, app, desktop, tests, benchmarks };
    return manifest_value;
}

void write_sample_workspace_project(
    const fs::path& workspace_root, const std::string& project_id
) {
    const fs::path project_root = workspace_root / project_id;
    write_sample_build_project(project_root, project_id);
}

void write_sample_dual_run_workspace_project(
    const fs::path& workspace_root, const std::string& project_id
) {
    const fs::path project_root = workspace_root / project_id;
    ecosystem::manifest manifest_value = sample_dual_run_manifest();
    manifest_value.id = project_id;
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            project_root / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary dual-run workspace manifest"
    );

    write_text(
        project_root / "src/main.cpp",
        "#include <iostream>\nint main() { std::cout << \"" + project_id
            + "-app\\n\"; return 0; }\n"
    );
    write_text(
        project_root / "src/cli_main.cpp",
        "#include <iostream>\nint main() { std::cout << \"" + project_id
            + "-tool\\n\"; return 0; }\n"
    );
}

void write_sample_workspace_config(const fs::path& workspace_root) {
    write_text(
        workspace_root / "manifesto.workspace.json",
        "{\n"
        "  \"groups\": {\n"
        "    \"core\": [\"alpha\", \"beta\"],\n"
        "    \"apps\": [\"gamma\"]\n"
        "  }\n"
        "}\n"
    );
}

void write_sample_dependency_project(const fs::path& project_root) {
    ecosystem::manifest manifest_value = sample_dependency_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            project_root / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary dependency manifest"
    );

    write_text(
        project_root / "include/sample/core.hpp",
        "#pragma once\nint core_value();\n"
    );
    write_text(
        project_root / "src/sample/core.cpp",
        "#include \"sample/core.hpp\"\nint core_value() { return 7; }\n"
    );
    write_text(
        project_root / "src/main.cpp",
        "#include \"sample/core.hpp\"\nint main() { return core_value(); }\n"
    );
    write_text(
        project_root / "include/desktop/support.hpp",
        "#pragma once\nint desktop_support();\n"
    );
    write_text(
        project_root / "src/desktop/support.cpp",
        "#include \"desktop/support.hpp\"\nint desktop_support() { return "
        "1; }\n"
    );
    write_text(
        project_root / "tests/test_main.cpp", "int main() { return 0; }\n"
    );
    write_text(
        project_root / "benchmarks/bench_all.cpp", "int main() { return 0; }\n"
    );
}

void write_sample_dual_run_project(const fs::path& project_root) {
    const ecosystem::manifest manifest_value = sample_dual_run_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            project_root / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary dual-run manifest"
    );

    write_text(
        project_root / "src/main.cpp",
        "#include <iostream>\nint main() { std::cout << \"app-run\\n\"; "
        "return 0; }\n"
    );
    write_text(
        project_root / "src/cli_main.cpp",
        "#include <iostream>\nint main(int argc, char** argv) {\n"
        "    std::cout << \"tool-run\";\n"
        "    for (int index = 1; index < argc; ++index) {\n"
        "        std::cout << ' ' << argv[index];\n"
        "    }\n"
        "    std::cout << \"\\n\";\n"
        "    return 0;\n"
        "}\n"
    );
}

void write_sample_leak_check_project(const fs::path& project_root) {
    ecosystem::manifest manifest_value = sample_leak_check_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            project_root / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary leak-check manifest"
    );

    write_text(
        project_root / "include/sample/core.hpp",
        "#pragma once\nint meaning();\n"
    );
    write_text(
        project_root / "src/sample/core.cpp",
        "#include \"sample/core.hpp\"\nint meaning() { return 42; }\n"
    );
    write_text(
        project_root / "tests/main_tests.cpp",
        "#include \"sample/core.hpp\"\nint main() { return meaning() == "
        "42 ? 0 : 1; }\n"
    );
}

void write_sample_java_binding_project(const fs::path& project_root) {
    const ecosystem::manifest manifest_value = sample_java_binding_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            project_root / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary java-check manifest"
    );

    write_text(
        project_root / "include/sample/api.hpp",
        "#pragma once\nint api_value();\n"
    );
    write_text(
        project_root / "src/sample/api.cpp",
        "#include \"sample/api.hpp\"\nint api_value() { return 7; }\n"
    );
    write_text(
        project_root / "include/sample/jni.hpp",
        "#pragma once\nint jni_value();\n"
    );
    write_text(
        project_root / "src/sample/jni.cpp",
        "#include \"sample/jni.hpp\"\nint jni_value() { return 11; }\n"
    );
    write_text(
        project_root / "java/build.gradle",
        "plugins { id 'java' }\n"
        "repositories { mavenCentral() }\n"
        "tasks.withType(Test).configureEach {\n"
        "    useJUnitPlatform()\n"
        "}\n"
    );
    write_text(
        project_root / "java/settings.gradle",
        "rootProject.name = 'java-sample'\n"
    );
}

void write_sample_sphinx_project(const fs::path& project_root) {
    write_sample_build_project(project_root, "docs_sample");
    write_text(project_root / "docs/index.md", "# Docs Sample\n");
}

fs::path first_recursive_file_with_suffix(
    const fs::path& root, const std::string& suffix
) {
    std::error_code error;
    if (!fs::exists(root, error) || error) {
        return {};
    }

    fs::recursive_directory_iterator iterator(root, error);
    const fs::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error)
            && iterator->path().filename().string().ends_with(suffix)) {
            return iterator->path();
        }
        error.clear();
        iterator.increment(error);
    }
    return {};
}

fs::path
first_recursive_file_named(const fs::path& root, const std::string& filename) {
    std::error_code error;
    if (!fs::exists(root, error) || error) {
        return {};
    }

    fs::recursive_directory_iterator iterator(root, error);
    const fs::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error)
            && iterator->path().filename() == filename) {
            return iterator->path();
        }
        error.clear();
        iterator.increment(error);
    }
    return {};
}

void write_sample_json_project(const fs::path& project_root) {
    ecosystem::manifest manifest_value = sample_json_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            project_root / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary json dependency manifest"
    );
    write_text(project_root / "src/main.cpp", "int main() { return 0; }\n");
}

void write_sample_scoped_probe_project(const fs::path& project_root) {
    ecosystem::manifest manifest_value = sample_scoped_probe_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            project_root / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary scoped probe manifest"
    );
    write_text(project_root / "src/main.cpp", "int main() { return 0; }\n");
    write_text(
        project_root / "src/kde_side.cpp",
        "int kde_side_value() { return 0; }\n"
    );
    write_text(
        project_root / "include/kde_side.hpp",
        "#pragma once\nint kde_side_value();\n"
    );
}

void write_sample_cmake_cache(
    const fs::path& project_root, const std::string& profile,
    const std::string& contents
) {
    const ecosystem::manifest_report report
        = ecosystem::load_manifest(project_root / "manifest.json");
    require_true(
        report.errors.empty(), "sample cache helper must load the manifest"
    );
    require_true(
        report.value.has_value(), "sample cache helper must parse the manifest"
    );

    std::string error_message;
    require_true(
        ecosystem::ensure_local_developer_surface(
            project_root, *report.value, &error_message
        ) == ecosystem::command_error::ok,
        "sample cache helper must materialize the local developer surface"
    );
    const fs::path cache_path
        = ecosystem::local_build_cache_path(project_root, profile);
    std::error_code error;
    fs::create_directories(cache_path.parent_path(), error);
    write_text(cache_path, contents);
}

void age_file_by_seconds(
    const fs::path& path, const std::chrono::seconds delta
) {
    const fs::file_time_type current_time = fs::last_write_time(path);
    fs::last_write_time(path, current_time - delta);
}

std::string relative_build_cache_path(
    const fs::path& project_root, const std::string& profile
) {
    return ecosystem::local_build_cache_path(project_root, profile)
        .lexically_relative(project_root)
        .generic_string();
}

std::string
relative_build_dir(const fs::path& project_root, const std::string& profile) {
    return ecosystem::local_build_dir(project_root, profile)
        .lexically_relative(project_root)
        .generic_string();
}

std::string relative_doctor_probe_cache_path(
    const fs::path& project_root,
    const std::optional<ecosystem::artifact_ref>& requested_artifact,
    const std::string& profile
) {
    const ecosystem::build_tree_layout layout
        = ecosystem::describe_build_tree_layout(profile);
    return (ecosystem::local_doctor_dir(project_root)
            / ecosystem::doctor_probe_scope_key(requested_artifact)
            / layout.platform / layout.configuration / layout.variant
            / "CMakeCache.txt")
        .lexically_relative(project_root)
        .generic_string();
}

struct cli_result {
    int exit_code = 0;
    std::string output;
};

cli_result run_cli_with_binary(
    const fs::path& binary_path, const fs::path& working_directory,
    const std::string& arguments
) {
    const std::string command = "cd '" + working_directory.string() + "' && '"
        + binary_path.string() + "' " + arguments + " 2>&1";
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        throw std::runtime_error("unable to launch frontend binary");
    }

    cli_result result;
    char buffer[256];
    while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), pipe)
           != nullptr) {
        result.output += buffer;
    }
    const int status = ::pclose(pipe);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
    return result;
}

fs::path test_cli_build_dir() { return fs::path(ECOS_TEST_BUILD_DIR); }

fs::path engels_binary_path() { return test_cli_build_dir() / "engels"; }

fs::path marx_binary_path() { return test_cli_build_dir() / "marx"; }

cli_result run_engels_cli(
    const fs::path& working_directory, const std::string& arguments
) {
    return run_cli_with_binary(
        engels_binary_path(), working_directory, arguments
    );
}

cli_result
run_marx_cli(const fs::path& working_directory, const std::string& arguments) {
    return run_cli_with_binary(
        marx_binary_path(), working_directory, arguments
    );
}

cli_result
run_cli(const fs::path& working_directory, const std::string& arguments) {
    const std::string command = arguments.substr(0, arguments.find(' '));
    if (command == "list" || command == "check" || command == "doctor"
        || command == "report") {
        return run_engels_cli(working_directory, arguments);
    }
    return run_marx_cli(working_directory, arguments);
}

void require_sync_success(
    const fs::path& project_root, const std::string& message
) {
    const cli_result sync_result = run_cli(project_root, "sync");
    require_true(
        sync_result.exit_code == 0, message + "\nactual: " + sync_result.output
    );
}

void test_file_writers_report_late_io_failures() {
    for (const bool authored : { false, true }) {
        for (const std::size_t size : { 1024U, 32768U }) {
            temp_dir root;
            const fs::path path = root.path() / "output";
            const pid_t child = ::fork();
            require_true(
                child >= 0, "must fork the isolated I/O failure probe"
            );
            if (child == 0) {
                std::signal(SIGXFSZ, SIG_IGN);
                const rlimit limit { 64, 64 };
                if (::setrlimit(RLIMIT_FSIZE, &limit) != 0) {
                    ::_exit(2);
                }
                std::string error;
                auto value = sample_manifest();
                value.description = std::string(size, 'x');
                const bool saved = authored
                    ? ecosystem::save_manifest(path, value, &error)
                    : ecosystem::write_text_file(
                          path, value.description, &error
                      );
                ::_exit(
                    !saved
                            && error.find("unable to write " + path.string())
                                != std::string::npos
                        ? 0
                        : 1
                );
            }
            int status = 0;
            require_true(
                ::waitpid(child, &status, 0) == child,
                "must wait for the write probe"
            );
            require_true(
                WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "buffered and direct write failures must report failure with "
                "the path"
            );
            require_true(
                fs::file_size(path) == 64,
                "failure injection must exercise a partial write after a "
                "successful open"
            );
        }
    }
}

void test_self_manifest_loads() {
    const ecosystem::manifest_report report = ecosystem::load_manifest(
        fs::path(ECOS_TEST_SOURCE_DIR) / "manifest.json"
    );
    require_true(
        report.errors.empty(),
        "self manifest must load without validation errors"
    );
    require_true(report.value.has_value(), "self manifest must be parsed");
    require_true(
        report.value->facade_entry_artifact == "marx:marx",
        "self manifest facade entry must point to Marx"
    );
    std::set<std::string> binaries;
    for (const auto& owner : report.value->components) {
        for (const auto& item : owner.artifacts) {
            if (item.kind == "exe" || item.kind == "qt_app") {
                binaries.insert(
                    ecosystem::artifact_output_name(*report.value, owner, item)
                );
            }
        }
    }
    require_true(
        binaries == std::set<std::string> { "marx", "engels" },
        "public local tooling binaries must remain exactly Marx and Engels"
    );
    require_true(
        report.value->install_artifacts
            == ecosystem::string_list { "marx:marx", "engels:engels" },
        "both actors must be distributed together"
    );
    const auto cmake = ecosystem::generate_cmakelists(
        *report.value, fs::path(ECOS_TEST_SOURCE_DIR)
    );
    require_not_contains(
        cmake, "add_executable(manifesto",
        "no third runtime target may be generated"
    );
    require_contains(
        cmake, "install(TARGETS marx__marx", "Marx must be installed"
    );
    require_contains(
        cmake, "install(TARGETS engels__engels", "Engels must be installed"
    );
}

void test_authored_artifacts_round_trip_and_mutate() {
    temp_dir root;
    json authored
        = { { "id", "owned" },
            { "description", "Explicit artifact intent" },
            { "version", "1.2.3" },
            { "facade", "core:app" },
            { "artifacts",
              json::array(
                  { { { "id", "core:lib" },
                      { "kind", "static_lib" },
                      { "owns", json::array({ "math" }) } },
                    { { "id", "core:app" },
                      { "kind", "exe" },
                      { "owns", json::array() },
                      { "entry", "src/launch.cpp" },
                      { "dependencies", json::array({ "core:lib" }) } } }
              ) } };
    write_text(root.path() / "manifest.json", authored.dump(2) + "\n");
    write_text(root.path() / "include/math.hpp", "int answer();\n");
    write_text(root.path() / "src/math.cpp", "int answer() { return 42; }\n");
    write_text(
        root.path() / "src/launch.cpp",
        "#include \"math.hpp\"\nint main() { return answer() == 42 ? 0 : 1; }\n"
    );
    auto loaded = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        loaded.errors.empty() && loaded.value.has_value(),
        join_lines(loaded.errors)
    );
    require_true(
        loaded.value->cpp_standard == 20,
        "default language standard must not need repeated authorship"
    );
    require_true(
        ecosystem::to_json(*loaded.value) == authored,
        "only authored scope intent must round-trip"
    );
    write_text(
        root.path() / "include/math/extra.hpp",
        "// inferred after the next load\n"
    );
    loaded = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        loaded.errors.empty()
            && loaded.value->components.front().ownership->headers.size() == 2,
        "new companions must be discovered without manifest edits"
    );
    const auto mutated
        = run_marx_cli(root.path(), "mutate add module core:lib added");
    require_true(
        mutated.exit_code == 0,
        "qualified artifact ownership must scaffold: " + mutated.output
    );
    const auto saved = json::parse(read_text(root.path() / "manifest.json"));
    require_true(
        !saved.contains("components")
            && saved.at("artifacts").at(0).at("owns")
                == json::array({ "math", "added" }),
        "mutation must persist scopes without a module/file inventory"
    );
    require_true(
        saved.at("artifacts").at(1) == authored.at("artifacts").at(1),
        "mutation must preserve other artifacts and declared dependencies"
    );
    require_true(
        fs::exists(root.path() / "include/added.hpp")
            && fs::exists(root.path() / "src/added.cpp"),
        "new module companions must be scaffolded"
    );
    auto updated = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(updated.errors.empty(), join_lines(updated.errors));
    const auto templated = ecosystem::add_file_unit(
        root.path(), &*updated.value, "core:lib", "include/template",
        "header_template_impl"
    );
    require_true(templated.errors.empty(), join_lines(templated.errors));
    std::string save_error;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", *updated.value, &save_error
        ),
        save_error
    );
    write_text(
        root.path() / "src/launch.cpp",
        "#include \"math.hpp\"\n#include \"template.hpp\"\nint main() { return "
        "answer() == 42 ? 0 : 1; }\n"
    );
    write_text(root.path() / "include/unowned.hpp", "#error unowned header\n");
    const auto built = run_marx_cli(root.path(), "build debug");
    require_true(
        built.exit_code == 0, "authored core+app must build: " + built.output
    );
    const auto run = run_marx_cli(root.path(), "run debug");
    require_true(
        run.exit_code == 0,
        "authored entry and library dependency must run: " + run.output
    );
    auto core_only = ecosystem::to_json(*updated.value);
    core_only["facade"] = "core:lib";
    core_only["artifacts"].erase(core_only["artifacts"].begin() + 1);
    write_text(root.path() / "manifest.json", core_only.dump(2) + "\n");
    require_sync_success(root.path(), "core-only authored facade must sync");
    const fs::path visitor = root.path() / "visitor";
    const fs::path prefix = root.path() / "installed";
    require_true(
        ecosystem::run_command(
            { "cmake", "-S", root.path().string(), "-B", visitor.string() },
            root.path()
        ) == 0,
        "core-only visitor facade must configure"
    );
    require_true(
        ecosystem::run_command(
            { "cmake", "--build", visitor.string(), "--parallel", "2" },
            root.path()
        ) == 0,
        "core-only visitor facade must build"
    );
    require_true(
        ecosystem::run_command(
            { "cmake", "--install", visitor.string(), "--prefix",
              prefix.string() },
            root.path()
        ) == 0,
        "core-only facade must install owned headers"
    );
    require_true(
        fs::exists(prefix / "include/math.hpp")
            && fs::exists(prefix / "include/math/extra.hpp")
            && fs::exists(prefix / "include/template.tpp")
            && !fs::exists(prefix / "include/unowned.hpp"),
        "installation must include selected nested/template companions and "
        "exclude unowned headers"
    );
    auto app_only = authored;
    app_only["facade"] = "solo:app";
    app_only["artifacts"] = json::array(
        { { { "id", "solo:app" },
            { "kind", "exe" },
            { "owns", json::array() },
            { "entry", "src/standalone.cpp" } } }
    );
    write_text(root.path() / "manifest.json", app_only.dump(2) + "\n");
    write_text(
        root.path() / "src/standalone.cpp",
        "#include <cstdio>\nint main() { std::puts(\"standalone-owned\"); }\n"
    );
    require_true(
        run_marx_cli(root.path(), "build debug").exit_code == 0,
        "app-only owned entry must build without other artifacts"
    );
    const auto standalone = run_marx_cli(root.path(), "run debug");
    require_true(standalone.exit_code == 0, standalone.output);
    require_contains(
        standalone.output, "standalone-owned",
        "app-only execution must use the declared entry"
    );
}

void test_authored_artifacts_reject_obsolete_and_ambiguous_intent() {
    temp_dir root;
    const json base = { { "id", "owned" },
                        { "description", "Owned app" },
                        { "facade", "app:app" },
                        { "artifacts",
                          json::array(
                              { { { "id", "app:app" },
                                  { "kind", "exe" },
                                  { "owns", json::array() },
                                  { "entry", "src/start.cc" } } }
                          ) } };
    auto check = [&](const json& value, const std::string& diagnostic) {
        write_text(root.path() / "manifest.json", value.dump());
        const auto loaded
            = ecosystem::load_manifest(root.path() / "manifest.json");
        require_true(
            loaded.has_manifest, "invalid authored projects must remain visible"
        );
        require_contains(
            join_lines(loaded.errors), diagnostic,
            "invalid authored intent must fail before generation"
        );
        require_true(
            !fs::exists(root.path() / "CMakeLists.txt"),
            "loading invalid intent must not generate state"
        );
    };
    for (const std::string field :
         { "components", "modules", "file_units", "relations",
           "android_sdk_path", "release_state" }) {
        auto value = base;
        value[field] = json::array();
        check(value, "unsupported field '" + field + "'");
    }
    for (const std::string field :
         { "modules", "file_units", "stack", "link" }) {
        auto value = base;
        value["artifacts"][0][field] = json::array();
        check(value, "unsupported field '" + field + "'");
    }
    auto value = base;
    value["artifacts"][0].erase("entry");
    check(value, "requires an explicit entry");
    value = base;
    value["artifacts"][0]["owns"] = ".";
    check(value, "owns must be an array");
    value = base;
    value["artifacts"][0]["entry"] = "src/start.hpp";
    check(value, "must name a C++ source file");
    value = base;
    value["artifacts"][0]["dependencies"] = json::array({ "missing:lib" });
    check(value, "unresolved artifact link");
    value = base;
    value["artifacts"].push_back(
        { { "id", "lib:lib" },
          { "kind", "static_lib" },
          { "owns", json::array({ "." }) } }
    );
    check(value, "overlapping artifact ownership");
    value = base;
    value["artifacts"][0]["owns"] = json::array({ "../escape" });
    check(value, "safe project-relative scopes");
    value = base;
    value["artifacts"][0]["entry"] = "../start.cpp";
    check(value, "safe root-relative file");
    value = base;
    value["artifacts"][0]["root"] = "../outside";
    check(value, "safe project-relative path");
    fs::create_directories(root.path() / "src/empty");
    fs::create_directory_symlink("empty", root.path() / "src/alias");
    value = base;
    value["artifacts"].push_back(
        { { "id", "first:lib" },
          { "kind", "static_lib" },
          { "owns", json::array({ "src/empty" }) } }
    );
    value["artifacts"].push_back(
        { { "id", "second:lib" },
          { "kind", "static_lib" },
          { "owns", json::array({ "src/alias" }) } }
    );
    check(value, "overlapping artifact ownership through filesystem paths");
    value = base;
    auto& imported = value["artifacts"][0];
    imported["kind"] = "static_lib";
    imported["name"] = "remote";
    imported.erase("entry");
    imported["packages"]
        = { { "external_project",
              { { "repository", "https://example.invalid/provider.git" },
                { "revision", "main" },
                { "package", "provider" },
                { "artifact", "core:lib" } } } };
    imported["owns"] = json::array({ "math" });
    check(value, "external artifacts must not declare local owns");
    imported["owns"] = json::array();
    imported["id"] = "app:external";
    value["facade"] = "app:external";
    write_text(root.path() / "manifest.json", value.dump());
    const auto external
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(external.errors.empty(), join_lines(external.errors));
    const auto cmake
        = ecosystem::generate_cmakelists(*external.value, root.path());
    require_contains(
        cmake, "find_package(provider CONFIG REQUIRED)",
        "providers must enter through their installed package"
    );
    require_contains(
        cmake, "add_library(app__external ALIAS provider::core__lib)",
        "the authored imported identity must remain stable"
    );
}

void test_owned_test_targets_are_independent() {
    temp_dir root;
    write_text(root.path() / "manifest.json", R"({
        "id":"parallel_tests", "description":"Independent artifact test owners",
        "facade":"core:alpha",
        "artifacts":[
            {"id":"core:alpha","kind":"static_lib","owns":["alpha"],"tests":{"selftest":true}},
            {"id":"core:beta","kind":"static_lib","owns":["beta"],"tests":{"selftest":true}}
        ]
    })");
    for (const auto* name : { "alpha", "beta" }) {
        write_text(
            root.path() / (std::string("src/") + name + ".cpp"),
            std::string("int ") + name + "() { return 42; }\n"
        );
        write_text(
            root.path() / (std::string("tests/") + name + "_tests.cpp"),
            std::string("int ") + name + "(); int main() { return " + name
                + "() == 42 ? 0 : 1; }\n"
        );
    }
    auto loaded = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(loaded.errors.empty(), join_lines(loaded.errors));
    auto collision = *loaded.value;
    collision.components.back().artifacts.front().name = "core__alpha__tests";
    collision.components.back().artifacts.front().kind = "exe";
    collision.components.back().ownership->entry = "src/entry.cpp";
    require_contains(
        join_lines(ecosystem::validate_manifest(collision)),
        "artifact output collision:",
        "new automatic test filenames must remain reserved"
    );

    const auto all = run_cli(root.path(), "check tests");
    require_true(
        all.exit_code == 0,
        "both test owners must build and run:\n" + all.output
    );
    for (const auto* name : { "core__alpha__tests", "core__beta__tests" })
        require_contains(
            all.output, name,
            "each artifact must have an independent test identity"
        );
    const auto build
        = root.path() / ecosystem::local_build_dir(root.path(), "debug");
    const auto commands
        = json::parse(read_text(build / "compile_commands.json"));
    bool found_defaults = false;
    for (const auto& command : commands) {
        const auto text = command.at("command").get<std::string>();
        if (text.find("core__alpha__tests.dir") != std::string::npos) {
            require_contains(
                text, "-Wall", "generated tests must receive common warnings"
            );
            require_contains(
                text, "-std=c++20",
                "generated tests must receive the project standard"
            );
            found_defaults = true;
        }
    }
    require_true(found_defaults, "test compile commands must be inspectable");
    write_text(
        root.path() / "tests/beta_tests.cpp", "int main() { return 1; }\n"
    );
    const auto alpha = run_cli(root.path(), "check tests core:alpha");
    require_true(
        alpha.exit_code == 0,
        "selection must exclude the failing adjacent test owner:\n"
            + alpha.output
    );
    require_not_contains(
        alpha.output, "core__beta__tests",
        "selection must use the full artifact identity"
    );
    const auto beta = run_cli(root.path(), "check tests core:beta");
    require_true(beta.exit_code != 0, "selecting the failing owner must fail");
    require_contains(
        beta.output, "core__beta__tests",
        "failure must identify the selected owner"
    );

    if (ecosystem::probe_tool("llvm-profdata").available
        && ecosystem::probe_tool("llvm-cov").available) {
        const auto coverage = run_cli(root.path(), "check coverage core:alpha");
        require_true(
            coverage.exit_code == 0,
            "coverage must use the same selected test identity:\n"
                + coverage.output
        );
        const auto report = json::parse(
            read_text(root.path() / ".ecosystem/reports/coverage.json")
        );
        require_true(
            !report.at("data").empty(),
            "LLVM coverage must contain measured test data"
        );
    }
}

void test_owned_scope_discovery_and_build() {
    require_true(fs::equivalent(ecosystem::locate_template_path({"cmake/generated_tests_block.tpl"}),
        fs::path(ECOS_TEST_SOURCE_DIR) / "templates/cmake/generated_tests_block.tpl"),
        "default templates must come from the generator's own version");
    temp_dir root;
    write_text(root.path() / "include/math.hpp", "int answer();\n");
    write_text(root.path() / "include/math.tpp", "// companion\n");
    write_text(
        root.path() / "src/math.cpp",
        "#include \"math.hpp\"\nint answer() { return 42; }\n"
    );
    write_text(
        root.path() / "src/launch.cc",
        "#include \"math.hpp\"\nint main() { return answer() == 42 ? 0 : 1; }\n"
    );
    write_text(
        root.path() / "tests/math_tests.cpp",
        "#include \"math.hpp\"\nint main() { return answer() == 42 ? 0 : 1; }\n"
    );
    write_text(
        root.path() / "tests/unrelated.cpp", "#error outside the owned scope\n"
    );
    write_text(
        root.path() / "src/unrelated.cpp", "#error outside the owned scope\n"
    );
    write_text(root.path() / "include/unrelated.hpp", "#error not installed\n");
    auto value = sample_library_manifest();
    auto& library = value.components.front();
    library.modules.clear();
    library.ownership.emplace();
    library.ownership->scopes = { "math" };
    library.tests = json::object({ { "selftest", true } });
    auto app = library;
    app.ownership.emplace();
    app.ownership->entry = "src/launch.cc";
    app.tests.clear();
    app.artifacts = { { "app", "exe", "owned_app", { "core:lib" } } };
    value.components.push_back(app);
    value.facade_entry_artifact = "core:app";
    require_true(
        ecosystem::discover_owned_files(&value, root.path()).empty(),
        "explicit ownership must discover its companions"
    );
    require_true(
        value.components.front().ownership->sources.size() == 1
            && value.components.front().ownership->headers.size() == 2,
        "source/header/template companions must be inferred"
    );
    require_true(
        value.components.front().ownership->tests.size() == 1,
        "test discovery must stay within owned scope"
    );
    require_true(
        ecosystem::resolve_artifact(
            value, ecosystem::artifact_ref { "core", "app" }
        )
            .has_value(),
        "multiple artifacts in a namespace must resolve independently"
    );
    const std::string developer
        = ecosystem::generate_developer_cmakelists(value, root.path());
    require_not_contains(
        developer, "unrelated", "unowned sources and tests must not enter CMake"
    );
    write_text(root.path() / ".ecosystem/source/CMakeLists.txt", developer);
    require_true(
        ecosystem::run_command(
            { "cmake", "-S", (root.path() / ".ecosystem/source").string(), "-B",
              (root.path() / "build").string(), "-DECOSYSTEM_BUILD_TESTS=ON" },
            root.path()
        ) == 0,
        "owned developer surface must configure"
    );
    require_true(
        ecosystem::run_command(
            { "cmake", "--build", (root.path() / "build").string(),
              "--parallel", "2" },
            root.path()
        ) == 0,
        "entry names must not rely on a main filename heuristic"
    );
    require_true(
        ecosystem::run_command(
            { (root.path() / "build/owned_app").string() }, root.path()
        ) == 0,
        "declared library dependency must link and run"
    );
    require_true(
        ecosystem::run_command(
            { "ctest", "--test-dir", (root.path() / "build").string(),
              "--output-on-failure" },
            root.path()
        ) == 0,
        "owned test companions must run"
    );
    value.facade_entry_artifact = "core:lib";
    const auto synced = ecosystem::sync_project(root.path(), value);
    require_true(synced.errors.empty(), join_lines(synced.errors));
    const std::string facade = read_text(root.path() / "CMakeLists.txt");
    require_not_contains(
        facade, "launch.cc",
        "library facade must not acquire an adjacent executable"
    );
    require_not_contains(
        facade, "install(DIRECTORY",
        "owned headers must be installed by selection"
    );
    auto overlapping = value;
    overlapping.components.back().ownership->scopes = { "math" };
    require_contains(
        join_lines(ecosystem::discover_owned_files(&overlapping, root.path())),
        "overlapping artifact ownership",
        "two artifacts must not own the same files"
    );
    auto independent = value;
    independent.facade_entry_artifact = "core:app";
    independent.components.back().artifacts.front().link.clear();
    require_not_contains(
        ecosystem::generate_cmakelists(independent, root.path()),
        "add_library(core__lib",
        "adjacent libraries must not become implicit dependencies"
    );
}

void test_manifest_rejects_unsafe_paths_and_invalid_dependency_graphs() {
    for (const std::string path : { "../outside", "/tmp/outside", "C:/outside",
                                    "bad;cmake", "bad\npath" }) {
        auto value = sample_manifest();
        value.components.front().root = path;
        require_contains(
            join_lines(ecosystem::validate_manifest(value)), "component.root",
            "owned roots must remain inside the project"
        );
        value = sample_manifest();
        value.components.front().modules = { path };
        require_contains(
            join_lines(ecosystem::validate_manifest(value)), "component module",
            "module paths must be safe"
        );
        value = sample_manifest();
        value.components.front().file_units = { { path, "source_only" } };
        require_contains(
            join_lines(ecosystem::validate_manifest(value)), "file_unit.id",
            "exceptional paths must be safe"
        );
        value = sample_manifest();
        value.components.front().artifacts.front().name = path;
        require_contains(
            join_lines(ecosystem::validate_manifest(value)), "output name",
            "output names must not inject paths or CMake lists"
        );
    }
    auto value = sample_manifest();
    const std::string primary = value.facade_entry_artifact;
    value.components.front().artifacts.front().link = { "ghost:lib" };
    require_contains(
        join_lines(ecosystem::validate_manifest(value)),
        "unresolved artifact link", "links must resolve before generation"
    );
    temp_dir root;
    std::string error;
    require_true(
        ecosystem::save_manifest(root.path() / "manifest.json", value, &error),
        error
    );
    const auto rejected = run_marx_cli(root.path(), "sync");
    require_true(
        rejected.exit_code != 0 && !fs::exists(root.path() / "CMakeLists.txt"),
        "an invalid dependency graph must not write generated files"
    );
    auto other = value.components.front();
    other.id = "other";
    other.artifacts.front().id = "lib";
    other.artifacts.front().link.clear();
    value.components.push_back(other);
    value.components.front().artifacts.front().link
        = { "other:lib", "other:lib" };
    require_contains(
        join_lines(ecosystem::validate_manifest(value)),
        "duplicate artifact link", "duplicate links must fail"
    );
    value.components.front().artifacts.front().link = { "other:lib" };
    value.components.back().artifacts.front().link = { primary };
    require_contains(
        join_lines(ecosystem::validate_manifest(value)),
        "artifact dependency cycle:",
        "cycles must fail with an attributable chain"
    );
    value.components.back().artifacts.front().link.clear();
    value.components.back().artifacts.front().kind = "exe";
    require_contains(
        join_lines(ecosystem::validate_manifest(value)),
        "must reference a library",
        "independent executables must not be linked as libraries"
    );
}

void test_owned_paths_reject_symlink_escapes() {
    for (const std::string relative :
         { "src", "src/main.cpp", "include", "tests/external.cpp",
           "tests/nested", "benchmarks/external.cpp", "assets/data",
           "android/res", ".ecosystem" }) {
        temp_dir root;
        const fs::path project = root.path() / "project";
        const fs::path outside = root.path() / "project-other";
        fs::create_directories(outside);
        auto value = sample_manifest();
        value.install_assets = true;
        value.android_package_source_dir = "android";
        std::string error;
        require_true(
            ecosystem::save_manifest(project / "manifest.json", value, &error),
            error
        );
        write_text(outside / "sentinel", "untouched");
        fs::create_directories((project / relative).parent_path());
        fs::create_symlink(outside, project / relative);
        const auto loaded = ecosystem::load_manifest(project / "manifest.json");
        require_true(loaded.has_manifest, "unsafe manifests must stay visible");
        require_contains(
            join_lines(loaded.errors), "outside project", relative
        );
        const auto synced = ecosystem::sync_project(project, value);
        require_true(
            !synced.errors.empty() && synced.written_files.empty(),
            "sync must preflight owned paths"
        );
        require_true(
            !fs::exists(project / "CMakeLists.txt"),
            "failed preflight must not generate files"
        );
        require_true(
            read_text(outside / "sentinel") == "untouched",
            "outside inputs must remain unchanged"
        );
    }

    temp_dir root;
    const fs::path project = root.path() / "project";
    fs::create_directories(project / "src");
    const fs::path missing = root.path() / "missing.cpp";
    fs::create_symlink(missing, project / "src/main.cpp");
    require_contains(
        join_lines(
            ecosystem::validate_manifest_paths(sample_manifest(), project)
        ),
        "unable to resolve owned symlink",
        "dangling links must not be accepted as future destinations"
    );
    require_true(
        !fs::exists(missing),
        "validation must not create a dangling link target"
    );
}

void test_owned_paths_allow_internal_symlinks_and_missing_files() {
    temp_dir root;
    const fs::path project = root.path() / "project";
    fs::create_directories(project / "actual/src");
    fs::create_directories(project / "tests/nested");
    fs::create_directory_symlink("actual/src", project / "src");
    fs::create_directory_symlink("..", project / "tests/nested/loop");
    fs::create_directory_symlink(project, root.path() / "alias");
    auto value = sample_manifest();
    require_true(
        ecosystem::validate_manifest_paths(value, root.path() / "alias")
            .empty(),
        "an aliased project root, internal directory links and cycles must be "
        "handled"
    );
    const auto added = ecosystem::add_module(
        root.path() / "alias", &value, "sample", "nested/feature"
    );
    require_true(
        added.errors.empty() && added.changed_manifest, join_lines(added.errors)
    );
    require_true(
        fs::exists(project / "actual/src/nested/feature.cpp"),
        "safe future files must be scaffolded"
    );
    require_true(
        ecosystem::validate_manifest_paths(
            sample_manifest(), root.path() / "future"
        )
            .empty(),
        "a project that has not been scaffolded yet must be valid"
    );
}

void test_owned_paths_preflight_mutations_without_changes() {
    for (const std::string operation : { "component", "module", "unit" }) {
        temp_dir root;
        auto value = sample_manifest();
        const json before = ecosystem::to_json(value);
        const fs::path project = root.path() / "project";
        ecosystem::mutation_report report;
        if (operation == "component") {
            report = ecosystem::add_component(project, &value, "../escape");
        } else if (operation == "module") {
            report = ecosystem::add_module(
                project, &value, "sample", "../../../escape"
            );
        } else {
            report = ecosystem::add_file_unit(
                project, &value, "sample", "../../../escape", "source_pair_h"
            );
        }
        require_true(
            !report.errors.empty() && !report.changed_manifest
                && report.written_files.empty(),
            operation
        );
        require_true(
            ecosystem::to_json(value) == before && !fs::exists(project),
            "invalid mutation must leave model and filesystem unchanged"
        );
    }

    for (const std::string operation :
         { "component", "module", "files", "unit" }) {
        temp_dir root;
        const fs::path project = root.path() / "project";
        const fs::path outside = root.path() / "outside.cpp";
        const std::string relative = operation == "component" ? "extra/src"
            : operation == "unit" ? "include/feature.tpp"
                                  : "src/feature.cpp";
        fs::create_directories((project / relative).parent_path());
        fs::create_symlink(outside, project / relative);
        auto value = sample_manifest();
        if (operation == "files") {
            value.components.front().modules.push_back("feature");
        }
        const json before = ecosystem::to_json(value);
        std::string error;
        require_true(
            ecosystem::save_manifest(project / "manifest.json", value, &error),
            error
        );
        const std::string manifest_before
            = read_text(project / "manifest.json");
        ecosystem::mutation_report report;
        if (operation == "component") {
            report = ecosystem::add_component(project, &value, "extra");
        } else if (operation == "module") {
            report
                = ecosystem::add_module(project, &value, "sample", "feature");
        } else if (operation == "files") {
            report = ecosystem::add_files(project, value, "sample", "feature");
        } else {
            report = ecosystem::add_file_unit(
                project, &value, "sample", "include/feature",
                "header_template_impl"
            );
        }
        require_contains(
            join_lines(report.errors), "unable to resolve owned symlink",
            operation
        );
        require_true(
            !report.changed_manifest && report.written_files.empty(),
            "rejected mutation must report no changes"
        );
        require_true(
            ecosystem::to_json(value) == before,
            "rejected mutation must preserve the caller's model"
        );
        require_true(
            read_text(project / "manifest.json") == manifest_before,
            "rejected mutation must preserve authored bytes"
        );
        require_true(
            !fs::exists(outside) && !fs::exists(project / "include/feature.hpp")
                && !fs::exists(project / "extra/include"),
            "all scaffold destinations must be checked before writes"
        );
    }
}

void test_owned_paths_preflight_sync_and_manifest_destinations() {
    for (const std::string relative :
         { ".github/workflows/tests.yml", ".github/actions/setup-ecosystem",
           ".github/actions/setup-ecosystem/action.yml", "CMakeLists.txt" }) {
        temp_dir root;
        const fs::path project = root.path() / "project";
        const fs::path outside = root.path() / "outside";
        fs::create_directories(project);
        write_text(outside / "action.yml", "untouched");
        if (relative != "CMakeLists.txt") {
            write_text(project / "CMakeLists.txt", "existing surface");
        }
        fs::create_directories((project / relative).parent_path());
        fs::create_symlink(
            relative.ends_with("setup-ecosystem") ? outside
                                                  : outside / "action.yml",
            project / relative
        );
        const auto report = ecosystem::sync_project(project, sample_manifest());
        require_contains(
            join_lines(report.errors), "outside project", relative
        );
        require_true(
            report.written_files.empty() && report.removed_files.empty(),
            "sync must preflight generation and obsolete paths together"
        );
        require_true(
            read_text(outside / "action.yml") == "untouched",
            "sync must preserve outside files"
        );
        require_true(
            !fs::exists(project / ".gitignore"),
            "sync must reject all destinations before writing the first file"
        );
        if (relative != "CMakeLists.txt") {
            require_true(
                read_text(project / "CMakeLists.txt") == "existing surface",
                "a later unsafe path must not partially update existing "
                "surfaces"
            );
        }
    }

    temp_dir root;
    const fs::path project = root.path() / "project";
    fs::create_directories(project);
    write_text(root.path() / "outside.json", "untouched");
    fs::create_symlink(root.path() / "outside.json", project / "manifest.json");
    std::string error;
    require_true(
        !ecosystem::save_manifest(
            project / "manifest.json", sample_manifest(), &error
        ),
        "manifest save must reject an outside destination"
    );
    require_contains(
        error, "outside project",
        "manifest save must identify the escaped destination"
    );
    require_true(
        read_text(root.path() / "outside.json") == "untouched",
        "manifest save must not truncate outside data"
    );
    require_contains(
        join_lines(ecosystem::load_manifest(project / "manifest.json").errors),
        "outside project",
        "manifest load must reject escaped authored input before parsing it"
    );
}

void test_manifest_rejects_output_and_target_collisions() {
    auto value = sample_dual_run_manifest();
    for (auto& owner : value.components) {
        owner.artifacts.front().name = "same";
    }
    require_contains(
        join_lines(ecosystem::validate_manifest(value)),
        "artifact output collision:",
        "different IDs must not overwrite the same output"
    );
    value.components.back().artifacts.front().kind = "shared_lib";
    require_true(
        ecosystem::validate_manifest(value).empty(),
        "executables and libraries may share a stem when their files differ"
    );
    value.components.front().artifacts.front().name = "libsame.so";
    require_contains(
        join_lines(ecosystem::validate_manifest(value)), "libsame.so",
        "collision checks must use actual output filenames across kinds"
    );
    value = sample_dual_run_manifest();
    value.components.front().id = "a__b";
    value.components.front().artifacts.front().id = "c";
    value.components.back().id = "a";
    value.components.back().artifacts.front().id = "b__c";
    value.facade_entry_artifact = "a__b:c";
    require_contains(
        join_lines(ecosystem::validate_manifest(value)),
        "CMake target collision:",
        "namespace encoding must not alias distinct artifacts"
    );
}

void test_manifest_rejects_generated_test_collisions() {
    auto value = sample_manifest();
    value.components.front().artifacts.front().id = "tests";
    value.facade_entry_artifact = "sample:tests";
    require_contains(
        join_lines(ecosystem::validate_manifest(value)),
        "CMake target collision: sample:tests and generated tests for sample",
        "authored targets must not alias generated test targets"
    );
    value.components.front().tests.clear();
    require_true(
        ecosystem::validate_manifest(value).empty(),
        "the tests id is available without generated tests"
    );
    value.components.front().tests = json::object({ { "selftest", true } });
    value.components.front().file_units
        = { { "tests/test_main", "source_only" } };
    require_true(
        ecosystem::validate_manifest(value).empty(),
        "explicit test-only artifacts must not reserve a second target"
    );

    for (const std::string output : { "app__tests", "app__tests.exe" }) {
        value = sample_dual_run_manifest();
        value.components.front().tests = json::object({ { "selftest", true } });
        value.components.back().artifacts.front().name = output;
        require_contains(
            join_lines(ecosystem::validate_manifest(value)),
            "artifact output collision: tool:cli and generated tests for app",
            "generated executable filenames must be reserved across components "
            "and platforms"
        );
        temp_dir root;
        const auto report = ecosystem::sync_project(root.path(), value);
        require_true(
            !report.errors.empty() && report.written_files.empty(),
            "test collisions must fail before generation writes"
        );
    }

    value = sample_library_manifest();
    value.components.front().tests = json::object({ { "selftest", true } });
    value.components.front().artifacts.front().name = "core__tests";
    require_true(
        ecosystem::validate_manifest(value).empty(),
        "a library may share the test stem when its output filename differs"
    );
    require_contains(
        ecosystem::generate_developer_cmakelists(value),
        "add_executable(core__tests",
        "the generator must use the reserved test target"
    );
}

void test_installed_libraries_are_relocatable() {
    for (const auto* kind : { "static_lib", "shared_lib" }) {
        temp_dir root;
        const auto provider = root.path() / "provider";
        json authored = {
            { "id", "portable" },
            { "description", "Relocatable library package" },
            { "version", "1.2.3" },
            { "facade", "core:lib" },
            { "artifacts",
              json::array(
                  { { { "id", "core:headers" },
                      { "kind", "interface_lib" },
                      { "owns", { "api" } },
                      { "packages", { { "cxxopts", { "cxxopts" } } } } },
                    { { "id", "core:base" },
                      { "kind", "static_lib" },
                      { "owns", { "base" } } },
                    { { "id", "core:lib" },
                      { "kind", kind },
                      { "owns", { "wrapper" } },
                      { "dependencies", { "core:base", "core:headers" } } } }
              ) }
        };
        authored["artifacts"][1]["tests"] = { { "gtest", true } };
        const auto gtest = root.path() / "packages/GTest";
        write_text(
            gtest / "GTestConfig.cmake", R"(if(NOT TARGET GTest::gtest_main)
  add_library(GTest::gtest_main INTERFACE IMPORTED)
endif()
set(GTest_FOUND TRUE)
)"
        );
        if (std::string(kind) == "shared_lib") {
            authored["facade"] = "app:app";
            authored["install_artifacts"] = { "core:lib" };
            authored["artifacts"].push_back(
                { { "id", "app:app" },
                  { "kind", "exe" },
                  { "owns", json::array() },
                  { "entry", "src/main.cpp" } }
            );
            write_text(provider / "src/main.cpp", "int main() { return 0; }\n");
        }
        write_text(provider / "manifest.json", authored.dump(2));
        write_text(
            provider / "include/api.hpp",
            "#pragma once\n#include <span>\n#include <cxxopts.hpp>\ninline int "
            "size(std::span<int> values) { return "
            "static_cast<int>(values.size()); }\n"
        );
        write_text(
            provider / "include/base.hpp", "#pragma once\nint base();\n"
        );
        write_text(provider / "src/base.cpp", "int base() { return 41; }\n");
        write_text(
            provider / "include/wrapper.hpp",
            "#pragma once\n#include \"api.hpp\"\nint answer();\n"
        );
        write_text(
            provider / "src/wrapper.cpp",
            "#include \"wrapper.hpp\"\n#include \"base.hpp\"\nint answer() { "
            "return base() + 1; }\n"
        );
        write_text(
            provider / "include/unowned.hpp",
            "#error not part of the public package\n"
        );
        const auto loaded
            = ecosystem::load_manifest(provider / "manifest.json");
        require_true(loaded.errors.empty(), join_lines(loaded.errors));
        const auto synced = ecosystem::sync_project(provider, *loaded.value);
        require_true(synced.errors.empty(), join_lines(synced.errors));
        const auto build = root.path() / "provider-build";
        const auto prefix = root.path() / "installed";
        auto run = [&](const std::vector<std::string>& args) {
            require_true(
                ecosystem::run_command(args, root.path()) == 0,
                "independent install/consumer command must succeed"
            );
        };
        run({ "cmake", "-S", provider.string(), "-B", build.string(),
              "-DGTest_DIR=" + gtest.string(),
              "-DCMAKE_INSTALL_INCLUDEDIR=include/portable",
              "-DCMAKE_INSTALL_LIBDIR=lib64" });
        run({ "cmake", "--build", build.string(), "--parallel", "2" });
        run({ "cmake", "--install", build.string(), "--prefix",
              prefix.string() });
        require_true(
            !fs::exists(prefix / "include/portable/unowned.hpp"),
            "export must retain explicit header ownership"
        );
        const auto relocated = root.path() / "relocated";
        fs::rename(prefix, relocated);
        fs::rename(provider, root.path() / "hidden-source");
        fs::rename(build, root.path() / "hidden-build");
        for (const auto& file :
             fs::directory_iterator(relocated / "lib64/cmake/portable")) {
            const auto text = read_text(file.path());
            for (const auto& forbidden : { provider, build, prefix })
                require_not_contains(
                    text, forbidden.string(),
                    "installed metadata must not retain source/build/install "
                    "locations"
                );
        }
        const auto consumer = root.path() / "consumer";
        write_text(
            consumer / "CMakeLists.txt", R"(cmake_minimum_required(VERSION 3.20)
project(consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(portable 1.2 CONFIG REQUIRED)
find_package(portable 1.2 CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE portable::core__lib)
)"
        );
        write_text(
            consumer / "main.cpp",
            "#include <wrapper.hpp>\nint main() { int data[2]{}; "
            "cxxopts::Options options(\"consumer\"); return answer() == 42 && "
            "size(data) == 2 ? 0 : 1; }\n"
        );
        run({ "cmake", "-S", consumer.string(), "-B",
              (root.path() / "consumer-build").string(),
              "-DGTest_DIR=" + gtest.string(),
              "-Dportable_DIR="
                  + (relocated / "lib64/cmake/portable").string() });
        run({ "cmake", "--build", (root.path() / "consumer-build").string(),
              "--parallel", "2" });
        run({ (root.path() / "consumer-build/consumer").string() });
    }

    temp_dir root;
    auto external = sample_external_project_manifest();
    external.components.back().artifacts.front().kind = "static_lib";
    external.components.back().file_units.clear();
    external.components.back().modules = { "wrapper" };
    const auto exported = ecosystem::sync_project(root.path(), external);
    require_true(
        exported.errors.empty(),
        "libraries linked to installed providers must export"
    );
    const auto cmake = read_text(root.path() / "CMakeLists.txt");
    require_contains(
        cmake, "find_dependency(packing CONFIG)",
        "consumer package metadata must rediscover the installed provider"
    );
    require_contains(
        cmake, "install(EXPORT ",
        "normal consumer package export must remain enabled"
    );
    require_not_contains(
        cmake, "IMPORTED_LOCATION",
        "installed dependencies must not expose a private build path"
    );
}

void test_install_artifacts_build_and_install_independent_executables() {
    temp_dir root;
    write_sample_dual_run_project(root.path());
    auto value = sample_dual_run_manifest();
    value.install_artifacts = { "app:app", "tool:cli" };
    std::string error;
    require_true(
        ecosystem::save_manifest(root.path() / "manifest.json", value, &error),
        error
    );
    const auto loaded = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        loaded.errors.empty()
            && loaded.value->install_artifacts == value.install_artifacts,
        "install intent must round-trip"
    );
    require_true(
        ecosystem::sync_project(root.path(), value).errors.empty(),
        "multi-artifact facade must sync"
    );
    const fs::path build = root.path() / "build";
    const fs::path prefix = root.path() / "install";
    require_true(
        ecosystem::run_command(
            { "cmake", "-S", root.path().string(), "-B", build.string() },
            root.path()
        ) == 0,
        "independent executable targets must configure without executable link "
        "dependencies"
    );
    require_true(
        ecosystem::run_command(
            { "cmake", "--build", build.string(), "--parallel", "2" },
            root.path()
        ) == 0,
        "both install artifacts must build"
    );
    require_true(
        ecosystem::run_command(
            { "cmake", "--install", build.string(), "--prefix",
              prefix.string() },
            root.path()
        ) == 0,
        "both install artifacts must install"
    );
    std::set<std::string> installed;
    for (const auto& entry : fs::directory_iterator(prefix / "bin")) {
        installed.insert(entry.path().filename().string());
    }
    require_true(
        installed == std::set<std::string> { "app", "cli" },
        "install must contain exactly the declared executable outputs"
    );
    value.install_artifacts = { "unknown:missing", "unknown:missing" };
    const auto errors = join_lines(ecosystem::validate_manifest(value));
    require_contains(
        errors, "does not resolve",
        "unresolved install intent must fail validation"
    );
    require_contains(
        errors, "duplicate", "duplicate install intent must fail validation"
    );
}

void test_visitor_facade_tracks_selected_artifact() {
    temp_dir root;
    const auto project = root.path() / "visitor project";
    write_sample_dual_run_project(project);
    write_text(
        project / "src/cli_main.cpp",
        "#include <iostream>\nint main(int argc, char** argv) {\n"
        "std::cout << \"worker\";\n"
        "for (int i = 1; i < argc; ++i) std::cout << ' ' << argv[i];\n"
        "std::cout << '\\n'; return argc > 1 ? 7 : 0; }\n"
    );
    auto value = sample_dual_run_manifest();
    value.install_artifacts = { "app:app", "tool:cli" };
    value.components[0].artifacts[0].name = "editor";
    value.components[1].artifacts[0].name = "worker";
    const auto build = project / "build";
    auto configure = [&]() {
        std::string error;
        require_true(
            ecosystem::save_manifest(project / "manifest.json", value, &error),
            error
        );
        require_true(
            ecosystem::sync_project(project, value).errors.empty(),
            "visitor surface must sync"
        );
        require_true(
            ecosystem::run_command(
                { "cmake", "-S", ".", "-B", "build" }, project
            ) == 0,
            "visitor configure must need no actor or internal artifact name"
        );
    };
    auto compile = [&]() {
        require_true(
            ecosystem::run_command({ "cmake", "--build", "build" }, project)
                == 0,
            "ordinary visitor build must materialize the facade"
        );
    };
    configure();
    compile();
    const auto cmake = read_text(project / "CMakeLists.txt");
    require_contains(
        cmake, "add_executable(app__app",
        "facade must preserve the canonical target"
    );
    require_contains(
        cmake, "OUTPUT_NAME editor",
        "facade must preserve the canonical output name"
    );
    require_not_contains(
        cmake, "add_executable(mvp", "facade must not duplicate the application"
    );
    require_true(
        fs::is_symlink(build / "mvp")
            && fs::equivalent(build / "mvp", build / "editor"),
        "mvp must point at the selected real executable"
    );
    require_true(
        !fs::read_symlink(build / "mvp").is_absolute(),
        "facade links must survive build-directory relocation"
    );
    const auto first = run_cli_with_binary(build / "mvp", project, "");
    require_true(
        first.exit_code == 0, "the three-command visitor smoke must succeed"
    );
    require_contains(
        first.output, "app-run", "only the selected executable must run"
    );

    const auto editor_time = fs::last_write_time(build / "editor");
    const auto worker_time = fs::last_write_time(build / "worker");
    value.facade_entry_artifact = "tool:cli";
    configure();
    compile();
    require_true(
        fs::equivalent(build / "mvp", build / "worker"),
        "selection changes must refresh the link without relinking"
    );
    fs::rename(build / "mvp", build / "removed-facade");
    compile();
    require_true(
        fs::is_symlink(build / "mvp"),
        "ordinary rebuild must restore a removed facade"
    );
    require_true(
        fs::last_write_time(build / "editor") == editor_time
            && fs::last_write_time(build / "worker") == worker_time,
        "facade maintenance must not rebuild unchanged executables"
    );
    const auto arguments
        = run_cli_with_binary(build / "mvp", project, "'two words'");
    require_true(
        arguments.exit_code == 7,
        "facade must preserve the real executable exit status"
    );
    require_contains(
        arguments.output, "worker two words",
        "facade must preserve application arguments"
    );
    const auto moved = project / "relocated build";
    fs::rename(build, moved);
    require_true(
        run_cli_with_binary(moved / "mvp", project, "").exit_code == 0,
        "relative facade must run after moving the build directory"
    );
    fs::rename(moved, build);

    // Switching from an alias to a real output named mvp must not leave a link
    // through which the linker can overwrite the previously selected output.
    value.components[1].artifacts[0].name = "mvp";
    configure();
    compile();
    require_true(
        !fs::is_symlink(build / "mvp"),
        "a canonical mvp output must not link to itself or overwrite its "
        "predecessor"
    );
    const auto native_time = fs::last_write_time(build / "mvp");
    configure();
    compile();
    require_true(
        fs::last_write_time(build / "mvp") == native_time,
        "reconfigure must preserve a canonical mvp executable"
    );
    require_true(
        fs::last_write_time(build / "worker") == worker_time,
        "changing facade output must preserve the previous executable"
    );

    value.components[1].artifacts[0].name = "worker";
    configure();
    compile();
    const auto api = sample_interface_library_manifest();
    value.components.push_back(api.components.front());
    value.facade_entry_artifact = api.facade_entry_artifact;
    write_text(project / "include/sample/api.hpp", "#pragma once\n");
    configure();
    compile();
    require_true(
        !fs::exists(build / "mvp") && !fs::is_symlink(build / "mvp"),
        "a library facade must retire the prior visitor link without inventing "
        "an app"
    );
}

void test_visitor_facade_handles_runtime_output_layouts() {
    for (const bool multi : { false, true }) {
        if (multi && !ecosystem::probe_tool("ninja").available)
            continue;
        temp_dir root;
        write_sample_build_project(root.path(), "sample");
        auto value = sample_build_manifest("sample");
        value.components.front().artifacts.front().name = "mvp";
        require_true(
            ecosystem::sync_project(root.path(), value).errors.empty(),
            "layout fixture must sync"
        );
        const auto build = root.path() / "build";
        std::vector<std::string> args {
            "cmake",
            "-S",
            ".",
            "-B",
            "build",
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY="
                + (build / "runtime outputs").string()
        };
        if (multi)
            args.insert(args.end(), { "-G", "Ninja Multi-Config" });
        require_true(
            ecosystem::run_command(args, root.path()) == 0,
            "custom runtime layout must configure"
        );
        for (const std::string& config : multi
                 ? std::vector<std::string> { "Debug", "Release" }
                 : std::vector<std::string> { "Debug" }) {
            require_true(
                ecosystem::run_command(
                    { "cmake", "--build", "build", "--config", config },
                    root.path()
                ) == 0,
                "configured facade must build"
            );
            const auto actual
                = build / "runtime outputs" / (multi ? config : "") / "mvp";
            require_true(
                fs::is_symlink(build / "mvp")
                    && fs::equivalent(build / "mvp", actual),
                "facade must follow the target file in the selected "
                "configuration"
            );
            require_true(
                run_cli_with_binary(build / "mvp", root.path(), "").exit_code
                    == 0,
                "custom-layout facade must run"
            );
        }
        require_true(
            ecosystem::run_command(
                { "cmake", "--build", "build", "--target", "clean" },
                root.path()
            ) == 0,
            "clean must succeed"
        );
        require_true(
            !fs::is_symlink(build / "mvp"),
            "clean must remove generated facade files"
        );
    }
}

void test_visitor_facade_rejects_conflicting_output_names() {
    for (const std::string name : { "mvp", "mvp.exe" }) {
        auto value = sample_dual_run_manifest();
        value.components[1].artifacts[0].name = name;
        const auto errors = join_lines(ecosystem::validate_manifest(value));
        require_contains(
            errors, "visitor facade output collision",
            "other artifacts must not overwrite the selected facade"
        );
        require_contains(
            errors, "tool:cli",
            "facade collision must name the conflicting artifact"
        );
        require_contains(
            errors, "app:app",
            "facade collision must identify the selected artifact"
        );
        value.facade_entry_artifact = "tool:cli";
        require_true(
            ecosystem::validate_manifest(value).empty(),
            "the selected artifact may already have the canonical mvp filename"
        );
    }
}

void test_sync_matches_tracked_surfaces() {
    const ecosystem::manifest_report report = ecosystem::load_manifest(
        fs::path(ECOS_TEST_SOURCE_DIR) / "manifest.json"
    );
    require_true(
        report.errors.empty(), "self manifest must load before sync comparison"
    );
    require_true(report.value.has_value(), "self manifest must be available");

    ecosystem::string_list errors;
    const std::vector<ecosystem::tracked_surface_file> generated_files
        = ecosystem::generate_tracked_surface_files(
            *report.value, fs::path(ECOS_TEST_SOURCE_DIR), &errors
        );
    require_true(
        errors.empty(),
        "tracked surface generation must succeed for self-hosting"
    );

    for (const ecosystem::tracked_surface_file& file_value : generated_files) {
        require_true(
            read_text(fs::path(ECOS_TEST_SOURCE_DIR) / file_value.relative_path)
                == file_value.contents,
            "generated tracked surface must match tracked file: "
                + file_value.relative_path.generic_string()
        );
    }
}

void test_package_surface_includes_qttest_component() {
    const ecosystem::manifest manifest_value = sample_qttest_manifest();
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(manifest_value, std::nullopt);
    require_true(
        dependencies.qt.values.contains("Test"),
        "shared package surface summary must derive Qt Test from tests.qttest"
    );

    temp_dir root;
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());
    require_contains(
        generated_cmake, "find_package(Qt6 6.0 CONFIG REQUIRED COMPONENTS",
        "generated facade must include the shared Qt package block"
    );
    require_contains(
        generated_cmake, "        Test\n",
        "generated facade must include the derived Qt Test component"
    );
}

void test_tracked_facade_limits_surface_to_entry_artifact_closure() {
    temp_dir root;
    const ecosystem::manifest manifest_value = sample_dependency_manifest();
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake, "add_library(core__lib STATIC",
        "tracked facade must include the linked library closure"
    );
    require_contains(
        generated_cmake, "add_executable(app__app",
        "tracked facade must include the facade entry artifact"
    );
    require_not_contains(
        generated_cmake, "desktop__support",
        "tracked facade must exclude unrelated non-entry components"
    );
    require_not_contains(
        generated_cmake, "tests__tests",
        "tracked facade must exclude test-only components"
    );
    require_not_contains(
        generated_cmake, "benchmarks__bench",
        "tracked facade must exclude benchmark-only components"
    );
    require_not_contains(
        generated_cmake, "ECOSYSTEM_BUILD_TESTS",
        "tracked facade must not expose developer-only test toggles"
    );
}

void test_external_project_generates_imported_library() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_external_project_manifest();
    require_true(
        ecosystem::validate_manifest(manifest_value).empty(),
        "valid external project intent must pass manifest validation"
    );

    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());
    require_contains(
        generated_cmake, "find_package(packing CONFIG REQUIRED)",
        "external components must consume installed CMake packages"
    );
    require_contains(
        generated_cmake,
        "add_library(packing_library__core ALIAS packing::library__core)",
        "the local authored identity must alias the provider target and all "
        "its usage requirements"
    );
    for (const auto* forbidden : { "ExternalProject_Add", "IMPORTED_LOCATION",
                                   "_binary_dir", "copy_directory" }) {
        require_not_contains(
            generated_cmake, forbidden,
            "consumers must not synthesize provider build layouts"
        );
    }

    manifest_value.components.front().modules = { "packing/geometry" };
    const ecosystem::string_list errors
        = ecosystem::validate_manifest(manifest_value);
    require_true(
        std::any_of(
            errors.begin(), errors.end(),
            [](const std::string& error) {
                return error.find("must not redeclare repository-owned")
                    != std::string::npos;
            }
        ),
        "external source modules must remain owned by their repository"
    );
}

void write_source_dependency_fixture(const fs::path& root) {
    const auto provider = root / "provider";
    const auto consumer = root / "consumer";
    write_text(provider / "manifest.json", R"({
        "id":"numbers", "description":"Reusable provider", "cpp_standard":20,
        "version":"1.0.0", "facade":"math:library",
        "artifacts":[{"id":"math:library", "kind":"static_lib", "owns":["answer"]}]
    })");
    write_text(
        provider / "include/answer.hpp",
        "#pragma once\n#include <span>\nint answer(std::span<const int> "
        "values);\n"
    );
    write_text(
        provider / "src/answer.cpp",
        "#include <answer.hpp>\nint answer(std::span<const int>) { return 42; "
        "}\n"
    );
    write_text(consumer / "manifest.json", R"({
        "id":"client", "description":"Independent consumer", "cpp_standard":17, "facade":"app:main",
        "artifacts":[
          {"id":"imported:math", "kind":"static_lib", "owns":[], "packages":{"external_project":{
            "repository":"https://example.invalid/numbers.git", "revision":"selected-revision",
            "package":"numbers", "artifact":"math:library"}}},
          {"id":"app:main", "kind":"exe", "owns":[], "entry":"src/main.cpp", "dependencies":["imported:math"]}
        ]
    })");
    write_text(
        consumer / "src/main.cpp",
        "#include <answer.hpp>\nint main() { return answer({}) == 42 ? 0 : 1; "
        "}\n"
    );
}

void test_source_dependency_local_override_builds_and_installs_before_consumer() {
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto provider = root.path() / "provider";
    const auto consumer = root.path() / "consumer";
    scoped_env override_path("NUMBERS_SOURCE_DIR", provider.string());
    auto result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0,
        "provider must install before consumer configuration: " + result.output
    );
    require_true(
        run_marx_cli(consumer, "build debug imported:math").exit_code == 0,
        "an explicitly requested imported artifact must complete provider "
        "preparation"
    );
    require_true(
        run_marx_cli(consumer, "run debug").exit_code == 0,
        "installed include directories, C++20 features and library linkage "
        "must reach the C++17 consumer"
    );
    require_true(
        !fs::exists(provider / ".ecosystem")
            && !fs::exists(provider / "CMakeLists.txt"),
        "a local override must remain source-owned; preparation state belongs "
        "to the consumer"
    );
    const auto cmake = read_text(consumer / ".ecosystem/source/CMakeLists.txt");
    require_contains(
        cmake, "find_package(numbers CONFIG REQUIRED)",
        "consumer must find the installed package"
    );
    require_not_contains(
        cmake, provider.string(),
        "consumer generation must not bind provider private sources"
    );
    write_text(
        provider / "src/answer.cpp",
        "#include <answer.hpp>\nint answer(std::span<const int>) { return 43; "
        "}\n"
    );
    write_text(
        consumer / "src/main.cpp",
        "#include <answer.hpp>\nint main() { return answer({}) == 43 ? 0 : 1; "
        "}\n"
    );
    result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0
            && run_marx_cli(consumer, "run debug").exit_code == 0,
        "mutable override changes must rebuild and reinstall before consumer "
        "relinking: "
            + result.output
    );
    write_text(provider / "src/answer.cpp", "#error provider-build-failed\n");
    result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 5,
        "provider failure must stop the consumer operation"
    );
    require_contains(
        result.output, "provider-build-failed",
        "provider failures must retain compiler diagnostics"
    );
}

void test_source_dependency_repository_selection_is_stable_and_explicit() {
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto provider = root.path() / "provider";
    const auto consumer = root.path() / "consumer";
    const auto git = [&](const ecosystem::string_list& args,
                         const fs::path& cwd) {
        auto command = ecosystem::string_list { "git" };
        command.insert(command.end(), args.begin(), args.end());
        const auto result = ecosystem::capture_command_result(command, cwd);
        require_true(
            result.exit_code == 0,
            "isolated Git operation must succeed: " + result.output
        );
        auto text = result.output;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        return text;
    };
    const auto commit = [&](const int answer) {
        write_text(
            provider / "src/answer.cpp",
            "#include <answer.hpp>\nint answer(std::span<const int>) { return "
                + std::to_string(answer) + "; }\n"
        );
        git({ "add", "." }, provider);
        git({ "-c", "user.name=Fixture", "-c",
              "user.email=fixture@example.invalid", "commit", "-qm",
              "provider change" },
            provider);
        return git({ "rev-parse", "HEAD" }, provider);
    };
    git({ "init", "-q", "-b", "main" }, provider);
    commit(42);
    const auto selected_commit = commit(43);
    auto manifest = json::parse(read_text(consumer / "manifest.json"));
    auto& dependency = manifest["artifacts"][0]["packages"]["external_project"];
    dependency["repository"] = provider.string();
    dependency["revision"] = "main";
    write_text(consumer / "manifest.json", manifest.dump(2));
    const auto expect = [&](const int value) {
        write_text(
            consumer / "src/main.cpp",
            "#include <answer.hpp>\nint main() { return answer({}) == "
                + std::to_string(value) + " ? 0 : 1; }\n"
        );
    };
    expect(43);
    auto result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0
            && run_marx_cli(consumer, "run debug").exit_code == 0,
        "repository selection must build a separately installed provider: "
            + result.output
    );
    fs::path selection;
    for (const auto& entry : fs::recursive_directory_iterator(
             consumer / ".ecosystem/dependencies"
         )) {
        if (entry.path().filename() == "selection.json")
            selection = entry.path();
    }
    require_true(
        !selection.empty(),
        "source resolution must record its exact local commit"
    );
    require_true(
        json::parse(read_text(selection)).at("commit") == selected_commit,
        "a named revision must resolve to the actual checked out commit"
    );
    const auto next_commit = commit(44);
    fs::rename(provider, root.path() / "offline-provider");
    result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0
            && run_marx_cli(consumer, "run debug").exit_code == 0,
        "ordinary rebuilds must use the pinned checkout without contacting the "
        "changed remote: "
            + result.output
    );
    const auto managed_source = selection.parent_path() / "source";
    write_text(
        managed_source / "src/answer.cpp", "#error changed managed checkout\n"
    );
    result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 5,
        "dirty managed sources must fail before building the consumer"
    );
    require_contains(
        result.output, "managed source checkout changed",
        "dirty source failure must explain the override path"
    );
    git({ "restore", "src/answer.cpp" }, managed_source);
    fs::rename(root.path() / "offline-provider", provider);
    dependency["revision"] = next_commit;
    write_text(consumer / "manifest.json", manifest.dump(2));
    expect(44);
    result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0
            && run_marx_cli(consumer, "run debug").exit_code == 0,
        "an explicit revision change must select and install the requested "
        "commit: "
            + result.output
    );
    require_true(
        json::parse(read_text(selection)).at("commit") == selected_commit,
        "new intent must not mutate the previous cached selection"
    );
    {
        scoped_env override_path("PROVIDER_SOURCE_DIR", provider.string());
        write_text(
            provider / "src/answer.cpp",
            "#include <answer.hpp>\nint answer(std::span<const int>) { return "
            "45; }\n"
        );
        expect(45);
        result = run_marx_cli(consumer, "build debug");
        require_true(
            result.exit_code == 0
                && run_marx_cli(consumer, "run debug").exit_code == 0,
            "a mutable override must use the same installed package contract: "
                + result.output
        );
    }
    expect(44);
    result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0
            && run_marx_cli(consumer, "run debug").exit_code == 0,
        "removing an override must restore the selected immutable repository "
        "state"
    );
}

void test_source_dependency_consumer_exports_are_relocatable() {
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto provider = root.path() / "provider";
    const auto consumer = root.path() / "consumer";
    auto manifest = json::parse(read_text(consumer / "manifest.json"));
    manifest["facade"] = "core:wrapper";
    manifest["artifacts"][1]
        = { { "id", "core:wrapper" },
            { "kind", "static_lib" },
            { "owns", json::array({ "wrapper" }) },
            { "dependencies", json::array({ "imported:math" }) } };
    write_text(consumer / "manifest.json", manifest.dump(2));
    write_text(
        consumer / "include/wrapper.hpp",
        "#pragma once\n#include <answer.hpp>\nint wrapped_answer();\n"
    );
    write_text(
        consumer / "src/wrapper.cpp",
        "#include <wrapper.hpp>\nint wrapped_answer() { return answer({}); }\n"
    );
    scoped_env override_path("NUMBERS_SOURCE_DIR", provider.string());
    const auto result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 0,
        "consumer library must build against the installed provider: "
            + result.output
    );
    const auto prefix = root.path() / "installed-client";
    auto run = [&](const ecosystem::string_list& args) {
        const auto command
            = ecosystem::capture_command_result(args, root.path());
        require_true(
            command.exit_code == 0,
            "relocated consumer operation failed: " + command.output
        );
    };
    run({ "cmake", "--install",
          ecosystem::local_build_dir(consumer, "debug").string(), "--prefix",
          prefix.string() });
    fs::path provider_prefix;
    for (const auto& entry : fs::recursive_directory_iterator(
             consumer / ".ecosystem/dependencies"
         )) {
        if (entry.path().filename() == "numbersConfig.cmake"
            && entry.path().string().find("/install/") != std::string::npos) {
            provider_prefix = entry.path()
                                  .parent_path()
                                  .parent_path()
                                  .parent_path()
                                  .parent_path();
        }
    }
    require_true(
        !provider_prefix.empty(), "provider install metadata must exist"
    );
    const auto relocated_provider = root.path() / "relocated-provider";
    fs::copy(provider_prefix, relocated_provider, fs::copy_options::recursive);
    fs::rename(provider, root.path() / "hidden-provider");
    fs::rename(consumer, root.path() / "hidden-consumer");
    const auto plain = root.path() / "plain";
    write_text(plain / "CMakeLists.txt", R"(cmake_minimum_required(VERSION 3.20)
project(plain LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(client CONFIG REQUIRED)
add_executable(plain main.cpp)
target_link_libraries(plain PRIVATE client::core__wrapper)
)");
    write_text(
        plain / "main.cpp",
        "#include <wrapper.hpp>\nint main() { return wrapped_answer() == 42 ? "
        "0 : 1; }\n"
    );
    const auto build = root.path() / "plain-build";
    run({ "cmake", "-S", plain.string(), "-B", build.string(),
          "-DCMAKE_PREFIX_PATH=" + prefix.string() + ";"
              + relocated_provider.string() });
    run({ "cmake", "--build", build.string() });
    run({ (build / "plain").string() });
    for (const auto& entry : fs::recursive_directory_iterator(prefix)) {
        if (entry.path().extension() != ".cmake")
            continue;
        const auto text = read_text(entry.path());
        for (const auto& private_path : { provider, consumer, provider_prefix })
            require_not_contains(
                text, private_path.string(),
                "consumer exports must not retain any private provider "
                "source/build/install location"
            );
    }
}

void test_source_dependency_shared_and_interface_usage_requirements() {
    for (const auto* kind : { "shared_lib", "interface_lib" }) {
        temp_dir root;
        write_source_dependency_fixture(root.path());
        const auto provider = root.path() / "provider";
        const auto consumer = root.path() / "consumer";
        auto provider_manifest
            = json::parse(read_text(provider / "manifest.json"));
        provider_manifest["artifacts"][0]["kind"] = kind;
        auto consumer_manifest
            = json::parse(read_text(consumer / "manifest.json"));
        consumer_manifest["artifacts"][0]["kind"] = kind;
        write_text(provider / "manifest.json", provider_manifest.dump(2));
        write_text(consumer / "manifest.json", consumer_manifest.dump(2));
        if (std::string(kind) == "interface_lib") {
            fs::remove(provider / "src/answer.cpp");
            write_text(
                provider / "include/answer.hpp",
                "#pragma once\n#include <span>\ninline int "
                "answer(std::span<const int>) { return 42; }\n"
            );
        }
        scoped_env override_path("NUMBERS_SOURCE_DIR", provider.string());
        const auto result = run_marx_cli(consumer, "build debug");
        require_true(
            result.exit_code == 0
                && run_marx_cli(consumer, "run debug").exit_code == 0,
            std::string("installed usage requirements must work for ") + kind
                + ": " + result.output
        );
    }
}

void test_source_dependency_rejects_invalid_provider_contracts_before_consumer_configure() {
    const std::vector<std::pair<
        std::string, std::function<void(const fs::path&, const fs::path&)>>>
        cases {
            { "invalid provider manifest",
              [](const auto& provider, const auto&) {
                  write_text(provider / "manifest.json", "{ invalid JSON\n");
              } },
            { "provider package identity mismatch",
              [](const auto& provider, const auto&) {
                  auto value
                      = json::parse(read_text(provider / "manifest.json"));
                  value["id"] = "different";
                  write_text(provider / "manifest.json", value.dump(2));
              } },
            { "provider artifact missing or kind mismatch",
              [](const auto&, const auto& consumer) {
                  auto value
                      = json::parse(read_text(consumer / "manifest.json"));
                  value["artifacts"][0]["packages"]["external_project"]
                       ["artifact"] = "math:absent";
                  write_text(consumer / "manifest.json", value.dump(2));
              } },
            { "provider artifact missing or kind mismatch",
              [](const auto&, const auto& consumer) {
                  auto value
                      = json::parse(read_text(consumer / "manifest.json"));
                  value["artifacts"][0]["kind"] = "shared_lib";
                  write_text(consumer / "manifest.json", value.dump(2));
              } },
            { "provider artifact is not exported",
              [](const auto& provider, const auto&) {
                  auto value
                      = json::parse(read_text(provider / "manifest.json"));
                  value["facade"] = "extra:interface";
                  value["artifacts"].push_back(
                      { { "id", "extra:interface" },
                        { "kind", "interface_lib" },
                        { "owns", json::array({ "extra" }) } }
                  );
                  write_text(provider / "include/extra.hpp", "#pragma once\n");
                  write_text(provider / "manifest.json", value.dump(2));
              } },
            { "outside project",
              [](const auto&, const auto& consumer) {
                  const auto outside = consumer.parent_path() / "outside";
                  fs::create_directories(outside);
                  fs::create_directories(consumer / ".ecosystem");
                  fs::create_directory_symlink(
                      outside, consumer / ".ecosystem/dependencies"
                  );
              } }
        };
    for (const auto& [message, mutate] : cases) {
        temp_dir root;
        write_source_dependency_fixture(root.path());
        const auto provider = root.path() / "provider";
        const auto consumer = root.path() / "consumer";
        mutate(provider, consumer);
        scoped_env override_path("NUMBERS_SOURCE_DIR", provider.string());
        const auto result = run_marx_cli(consumer, "build debug");
        require_true(
            result.exit_code == 5,
            "invalid provider contract must fail preparation: " + result.output
        );
        require_contains(
            result.output, message,
            "preparation failure must explain the violated contract"
        );
        require_true(
            !fs::exists(ecosystem::local_build_cache_path(consumer, "debug")),
            "provider contract failures must happen before consumer configure"
        );
        if (fs::exists(root.path() / "outside"))
            require_true(
                fs::is_empty(root.path() / "outside"),
                "dependency state preflight must not write through an escaping "
                "symlink"
            );
    }
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto provider = root.path() / "provider";
    const auto consumer = root.path() / "consumer";
    {
        scoped_env relative("NUMBERS_SOURCE_DIR", "../provider");
        const auto result = run_marx_cli(consumer, "build debug");
        require_true(
            result.exit_code == 5, "ambiguous relative overrides must fail"
        );
        require_contains(
            result.output, "must be an absolute path",
            "relative override failure must be actionable"
        );
    }
    const auto real_cmake = ecosystem::find_command_path("cmake");
    const auto tools = root.path() / "tools";
    write_executable_script(
        tools / "cmake",
        "#!/bin/sh\nif [ \"$1\" = --install ]; then\n  echo "
        "provider-install-failed >&2\n  exit 9\nfi\nexec "
        "\"$MANIFESTO_TEST_REAL_CMAKE\" \"$@\"\n"
    );
    scoped_env cmake("MANIFESTO_TEST_REAL_CMAKE", real_cmake);
    scoped_env path("PATH", tools.string() + ":" + current_path_env());
    scoped_env override_path("NUMBERS_SOURCE_DIR", provider.string());
    const auto result = run_marx_cli(consumer, "build debug");
    require_true(
        result.exit_code == 5,
        "provider install failure must fail the consumer operation"
    );
    require_contains(
        result.output, "provider-install-failed",
        "install failure must preserve the native output"
    );
    require_true(
        !fs::exists(ecosystem::local_build_cache_path(consumer, "debug")),
        "failed installation cannot configure a consumer against incomplete or "
        "stale output"
    );
}

void test_source_dependency_rejects_ambiguous_authored_metadata() {
    const auto valid = sample_external_project_manifest();
    for (const auto& [field, value] :
         std::vector<std::pair<std::string, json>> {
             { "package", "" },
             { "package", "unsafe)" },
             { "artifact", "missing-colon" },
             { "artifact", "bad/path:library" },
             { "artifact", "math:bad)" },
             { "repository", "--upload-pack=unexpected" },
             { "repository", "repo\nname" },
             { "revision", "--unexpected" },
             { "revision", "ref\nname" },
             { "component", "old" },
             { "local_source_dir", "/silently-ignored" },
             { "package", 5 } }) {
        auto broken = valid;
        broken.components.front().stack["external_project"][field] = value;
        require_true(
            !ecosystem::validate_manifest(broken).empty(),
            "invalid provider metadata must fail before generation: " + field
        );
    }
}

void test_library_facade_installs_library_target_and_headers() {
    temp_dir root;
    const ecosystem::manifest manifest_value = sample_library_manifest();
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake, "include(GNUInstallDirs)",
        "generated facade must use standard install-directory variables"
    );
    require_contains(
        generated_cmake, "install(TARGETS core__lib",
        "library-first facade must install the facade library target"
    );
    require_contains(
        generated_cmake, "ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}",
        "library-first facade must install archives under the standard lib dir"
    );
    require_contains(
        generated_cmake,
        "install(DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/include/\" "
        "DESTINATION "
        "${CMAKE_INSTALL_INCLUDEDIR})",
        "library-first facade must install its public headers"
    );
}

void test_interface_library_facade_installs_headers_without_target() {
    temp_dir root;
    const ecosystem::manifest manifest_value
        = sample_interface_library_manifest();
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake,
        "install(DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/include/\" "
        "DESTINATION "
        "${CMAKE_INSTALL_INCLUDEDIR})",
        "header-only facade must still install its public headers"
    );
    require_contains(
        generated_cmake, "install(TARGETS api__api EXPORT ",
        "header-only facade must export its interface usage requirements"
    );
}

void test_runnable_facade_skips_linked_static_library_install_surface() {
    temp_dir root;
    const ecosystem::manifest manifest_value = sample_dependency_manifest();
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake, "install(TARGETS app__app",
        "runnable facade must install its runnable entry target"
    );
    require_not_contains(
        generated_cmake, "install(TARGETS core__lib",
        "runnable facade must not install linked static libraries"
    );
    require_not_contains(
        generated_cmake,
        "install(DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/include/\" "
        "DESTINATION "
        "${CMAKE_INSTALL_INCLUDEDIR})",
        "runnable facade must not install headers for skipped static libraries"
    );
}

void test_runnable_facade_installs_linked_shared_library_closure() {
    temp_dir root;
    const ecosystem::manifest manifest_value = sample_shared_runtime_manifest();
    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake, "install(TARGETS app__app",
        "runnable facade must install its runnable entry target"
    );
    require_contains(
        generated_cmake, "install(TARGETS core__lib",
        "runnable facade must install linked shared-library closure targets"
    );
    require_not_contains(
        generated_cmake, "install(TARGETS spare__helper",
        "runnable facade must not install unrelated shared libraries"
    );
}

void test_project_assets_are_staged_and_installed() {
    temp_dir root;
    std::error_code error;
    fs::create_directories(root.path() / "assets", error);
    require_true(!error, "asset fixture directory must be created");

    ecosystem::manifest manifest_value = sample_dependency_manifest();
    manifest_value.id = "sample_assets";
    manifest_value.install_assets = true;
    manifest_value.components.at(1).stack
        = json::object({ { "qt", json::array({ "Core", "Widgets" }) } });
    manifest_value.components.at(1).artifacts.front().kind = "qt_app";
    const std::string generated_cmake
        = ecosystem::generate_developer_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake, "ecosystem_stage_assets(app__app)",
        "runnable facade must stage project assets beside the build output"
    );
    require_contains(
        generated_cmake, "ecosystem_embed_assets(app__app)",
        "Qt applications must embed opted-in assets for packaged runtimes"
    );
    require_not_contains(
        generated_cmake, "ecosystem_embed_assets(benchmarks__bench)",
        "non-Qt executables must not receive Qt-generated resource objects"
    );
    require_not_contains(
        generated_cmake, "ecosystem_embed_assets(tests__tests)",
        "non-Qt test executables must not receive Qt-generated resource objects"
    );
    require_contains(
        generated_cmake,
        "DESTINATION ${CMAKE_INSTALL_DATADIR}/sample_assets/assets",
        "facade install must preserve project assets under its data directory"
    );
    require_contains(
        generated_cmake, "qt_add_resources(${target_name}",
        "the generated Qt-only embedding helper must use qt_add_resources"
    );
    require_contains(
        generated_cmake, "PREFIX \"/sample_assets\"",
        "embedded assets must use a project-owned resource prefix"
    );
}

void test_android_application_id_is_manifest_owned() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_manifest();
    manifest_value.version = "2.3.4";
    manifest_value.android_application_id = "org.ninjaro.sample";
    manifest_value.android_package_source_dir = "android";
    manifest_value.components.front().stack["android"]
        = json::array({ "qt_android" });
    manifest_value.components.front().artifacts.front().kind = "qt_app";

    const std::string generated_cmake
        = ecosystem::generate_cmakelists(manifest_value, root.path());
    require_contains(
        generated_cmake, "QT_ANDROID_PACKAGE_NAME org.ninjaro.sample",
        "Android package identity must come from manifest metadata"
    );
    require_contains(
        generated_cmake,
        "if (COMMAND qt_policy)\n"
        "        qt_policy(SET QTP0002 NEW)",
        "Android package properties must opt into safe Qt path JSON handling "
        "when the Qt policy API is available"
    );
    require_contains(
        generated_cmake, "QT_ANDROID_VERSION_NAME \"${PROJECT_VERSION}\"",
        "Android version name must come from manifest-owned project version"
    );
    require_contains(
        generated_cmake, "QT_ANDROID_MIN_SDK_VERSION 28",
        "Android Qt targets must declare the Qt-supported minimum SDK"
    );
    require_contains(
        generated_cmake,
        "QT_ANDROID_PACKAGE_SOURCE_DIR "
        "\"${CMAKE_CURRENT_SOURCE_DIR}/android\"",
        "tracked facade must resolve Android package sources from the "
        "project root"
    );
    require_contains(
        generated_cmake, "project(sample VERSION 2.3.4",
        "project version must come from manifest metadata"
    );
    require_contains(
        generated_cmake, "ECOSYSTEM_PROJECT_VERSION=\"${PROJECT_VERSION}\"",
        "targets must receive the manifest-owned project version"
    );

    const std::string developer_cmake
        = ecosystem::generate_developer_cmakelists(manifest_value, root.path());
    require_contains(
        developer_cmake,
        "QT_ANDROID_PACKAGE_SOURCE_DIR "
        "\"${ECOSYSTEM_PROJECT_ROOT}/android\"",
        "developer surface must resolve Android package sources from the "
        "project root"
    );

    const ecosystem::json serialized = ecosystem::to_json(manifest_value);
    require_true(
        serialized.at("android_package_source_dir") == "android",
        "Android package source directory must survive serialization"
    );

    std::string save_error;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &save_error
        ),
        "Android package metadata manifest must serialize"
    );
    const ecosystem::manifest_report loaded
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        loaded.errors.empty() && loaded.value.has_value()
            && loaded.value->android_package_source_dir == "android",
        "Android package source directory must survive a manifest round trip"
    );

    manifest_value.android_package_source_dir = "../outside";
    const ecosystem::string_list errors
        = ecosystem::validate_manifest(manifest_value);
    require_true(
        std::find_if(
            errors.begin(), errors.end(),
            [](const std::string& error) {
                return error.find("android_package_source_dir")
                    != std::string::npos;
            }
        ) != errors.end(),
        "Android package source directory must not escape the project root"
    );

    manifest_value.android_package_source_dir = ".";
    const ecosystem::string_list root_errors
        = ecosystem::validate_manifest(manifest_value);
    require_true(
        std::find_if(
            root_errors.begin(), root_errors.end(),
            [](const std::string& error) {
                return error.find("android_package_source_dir")
                    != std::string::npos;
            }
        ) != root_errors.end(),
        "Android package source directory must not copy the project root"
    );

    manifest_value.android_package_source_dir = "android;unsafe";
    const ecosystem::string_list unsafe_errors
        = ecosystem::validate_manifest(manifest_value);
    require_true(
        std::find_if(
            unsafe_errors.begin(), unsafe_errors.end(),
            [](const std::string& error) {
                return error.find("android_package_source_dir")
                    != std::string::npos;
            }
        ) != unsafe_errors.end(),
        "Android package source directory must be safe for CMake interpolation"
    );
}

void test_developer_surface_materializes_full_build_graph() {
    temp_dir root;
    const ecosystem::manifest manifest_value = sample_dependency_manifest();
    const std::string generated_cmake
        = ecosystem::generate_developer_cmakelists(manifest_value, root.path());

    require_contains(
        generated_cmake,
        "get_filename_component(ECOSYSTEM_PROJECT_ROOT "
        "\"${CMAKE_CURRENT_SOURCE_DIR}/../..\" ABSOLUTE)",
        "developer surface must anchor generated sources back to "
        "the project root"
    );
    require_contains(
        generated_cmake, "desktop__support",
        "developer surface must include non-facade project components"
    );
    require_contains(
        generated_cmake, "if (ECOSYSTEM_BUILD_TESTS)",
        "developer surface must retain test toggles for ecosystem-owned builds"
    );
    require_contains(
        generated_cmake, "if (ECOSYSTEM_BUILD_BENCHMARKS)",
        "developer surface must retain benchmark toggles for "
        "ecosystem-owned builds"
    );
    require_contains(
        generated_cmake, "${ECOSYSTEM_PROJECT_ROOT}/src/main.cpp",
        "developer surface must reference project-owned sources "
        "through the local root anchor"
    );
}

void test_forbidden_repository_entries_report_legacy_scaffolding() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    write_text(
        root.path() / "scripts/build.sh", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_text(root.path() / "Makefile", "all:\n\t@true\n");
    write_text(root.path() / "Doxyfile", "PROJECT_NAME = legacy\n");
    write_text(root.path() / "cli/run.sh", "#!/usr/bin/env bash\nexit 0\n");
    write_text(root.path() / "cov/index.html", "<html></html>\n");
    write_text(root.path() / "bench/bench_log.txt", "bench\n");
    write_text(root.path() / "java-report/index.html", "<html></html>\n");
    write_text(root.path() / "cmake/legacy.cmake", "# legacy\n");
    write_text(
        root.path() / "frontend/CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.20)\nproject(legacy)\n"
    );

    const ecosystem::string_list issues
        = ecosystem::forbidden_repository_entries(root.path());
    std::ostringstream rendered;
    for (const std::string& issue : issues) {
        rendered << issue << "\n";
    }

    require_contains(
        rendered.str(), "scripts/: script directories are forbidden",
        "repository scan must flag committed script directories"
    );
    require_contains(
        rendered.str(), "Makefile: Makefiles are forbidden",
        "repository scan must flag committed Makefiles"
    );
    require_contains(
        rendered.str(), "Doxyfile: committed Doxyfile is forbidden",
        "repository scan must flag committed Doxyfiles"
    );
    require_contains(
        rendered.str(), "cli/run.sh: committed shell wrappers are forbidden",
        "repository scan must flag committed shell wrappers outside "
        "scripts directories"
    );
    require_contains(
        rendered.str(), "cov/: generated coverage reports are forbidden",
        "repository scan must flag committed coverage reports"
    );
    require_contains(
        rendered.str(), "bench/: generated benchmark reports are forbidden",
        "repository scan must flag committed benchmark reports"
    );
    require_contains(
        rendered.str(), "java-report/: generated Java reports are forbidden",
        "repository scan must flag committed Java report output"
    );
    require_contains(
        rendered.str(), "cmake/legacy.cmake: extra CMake modules are forbidden",
        "repository scan must flag committed auxiliary CMake modules"
    );
    require_contains(
        rendered.str(),
        "frontend/CMakeLists.txt: nested CMakeLists.txt files are forbidden",
        "repository scan must flag nested CMake entry files"
    );
}

void test_forbidden_repository_entries_fall_back_for_gitlink_roots() {
    if (!ecosystem::command_exists("git")) {
        return;
    }

    temp_dir root;
    const fs::path workspace_root = root.path() / "workspace";
    const fs::path project_root = workspace_root / "sample";
    write_sample_build_project(project_root, "sample");
    write_text(
        project_root / "scripts/build.sh", "#!/usr/bin/env bash\nexit 0\n"
    );

    require_true(
        ecosystem::run_command({ "git", "init", "-q" }, workspace_root) == 0,
        "gitlink repository regression test must initialize a temporary "
        "git repo"
    );
    require_true(
        ecosystem::run_command(
            {
                "git",
                "update-index",
                "--add",
                "--cacheinfo",
                "160000,1111111111111111111111111111111111111111,sample",
            },
            workspace_root
        ) == 0,
        "gitlink repository regression test must add a synthetic gitlink entry"
    );

    const ecosystem::string_list issues
        = ecosystem::forbidden_repository_entries(project_root);
    std::ostringstream rendered;
    for (const std::string& issue : issues) {
        rendered << issue << "\n";
    }

    require_contains(
        rendered.str(), "scripts/: script directories are forbidden",
        "gitlink-backed repository scans must fall back to the "
        "filesystem when git lists only the project entry"
    );
}

void test_component_package_link_targets_include_qttest_target() {
    const ecosystem::manifest manifest_value = sample_qttest_manifest();
    const ecosystem::component& component_value
        = manifest_value.components.front();
    const std::vector<std::string> targets
        = ecosystem::component_package_link_targets(component_value);

    require_true(
        std::find(targets.begin(), targets.end(), "Qt6::Core") != targets.end(),
        "package link targets must include declared Qt component targets"
    );
    require_true(
        std::find(targets.begin(), targets.end(), "Qt6::Widgets")
            != targets.end(),
        "package link targets must include all declared Qt component targets"
    );
    require_true(
        std::find(targets.begin(), targets.end(), "Qt6::Test") != targets.end(),
        "package link targets must derive Qt Test from tests.qttest"
    );
}

void test_component_package_link_targets_include_cxxopts_target() {
    const ecosystem::manifest manifest_value = sample_cxxopts_manifest();
    const ecosystem::component& component_value
        = manifest_value.components.front();
    const std::vector<std::string> targets
        = ecosystem::component_package_link_targets(component_value);

    require_true(
        std::find(targets.begin(), targets.end(), "cxxopts::cxxopts")
            != targets.end(),
        "package link targets must include the cxxopts package target"
    );
}

void test_component_package_link_targets_follow_descriptor_rules() {
    const ecosystem::manifest manifest_value = sample_dependency_manifest();

    const std::vector<std::string> core_targets
        = ecosystem::component_package_link_targets(
            manifest_value.components[0]
        );
    require_true(
        std::find(
            core_targets.begin(), core_targets.end(),
            "nlohmann_json::nlohmann_json"
        ) != core_targets.end(),
        "descriptor-driven link targets must include JSON package targets"
    );
    require_true(
        std::find(core_targets.begin(), core_targets.end(), "Qt6::Core")
            != core_targets.end(),
        "descriptor-driven link targets must include Qt component targets"
    );
    require_true(
        std::find(core_targets.begin(), core_targets.end(), "Qt6::Widgets")
            != core_targets.end(),
        "descriptor-driven link targets must include all declared Qt "
        "component targets"
    );
    require_true(
        std::find(
            core_targets.begin(), core_targets.end(),
            "ecosystem_optional_opencv"
        ) != core_targets.end(),
        "descriptor-driven link targets must include optional OpenCV "
        "support targets"
    );

    const std::vector<std::string> desktop_targets
        = ecosystem::component_package_link_targets(
            manifest_value.components[2]
        );
    require_true(
        std::find(
            desktop_targets.begin(), desktop_targets.end(),
            "ecosystem_kde_support"
        ) != desktop_targets.end(),
        "descriptor-driven link targets must include shared KDE support targets"
    );
    require_true(
        std::find(
            desktop_targets.begin(), desktop_targets.end(),
            "ecosystem_jni_support"
        ) != desktop_targets.end(),
        "descriptor-driven link targets must include JNI support targets"
    );
    require_true(
        std::find(
            desktop_targets.begin(), desktop_targets.end(),
            "ecosystem_llvm_clang_support"
        ) != desktop_targets.end(),
        "descriptor-driven link targets must include LLVM/Clang support targets"
    );
    require_true(
        std::find(desktop_targets.begin(), desktop_targets.end(), "KDEGames6")
            == desktop_targets.end(),
        "descriptor-driven link targets must keep KDEGames inside the "
        "shared KDE support target"
    );

    const std::vector<std::string> test_targets
        = ecosystem::component_package_link_targets(
            manifest_value.components[3]
        );
    require_true(
        std::find(test_targets.begin(), test_targets.end(), "GTest::gtest_main")
            != test_targets.end(),
        "descriptor-driven link targets must derive GTest support from "
        "tests.gtest"
    );

    const std::vector<std::string> benchmark_targets
        = ecosystem::component_package_link_targets(
            manifest_value.components[4]
        );
    require_true(
        std::find(
            benchmark_targets.begin(), benchmark_targets.end(),
            "ecosystem_optional_eigen"
        ) != benchmark_targets.end(),
        "descriptor-driven link targets must include optional Eigen "
        "support targets"
    );
    require_true(
        std::find(
            benchmark_targets.begin(), benchmark_targets.end(),
            "benchmark::benchmark_main"
        ) != benchmark_targets.end(),
        "descriptor-driven link targets must derive benchmark support "
        "from benchmark flags"
    );
}

void test_summarize_dependencies_follows_descriptor_sources() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );

    require_true(
        ecosystem::dependency_summary_has_entries(dependencies),
        "descriptor-driven summaries must report enabled packages"
    );
    require_true(
        dependencies.json.enabled
            && dependencies.json.values.contains("nlohmann_json"),
        "descriptor-driven summaries must collect JSON stack values"
    );
    require_true(
        dependencies.llvm_clang.enabled
            && dependencies.llvm_clang.values.contains("libclang"),
        "descriptor-driven summaries must collect LLVM/Clang stack values"
    );
    require_true(
        dependencies.kde.enabled
            && dependencies.kde.values.contains("CoreAddons"),
        "descriptor-driven summaries must keep non-KDEGames KDE values"
    );
    require_true(
        !dependencies.kde.values.contains("KDEGames6"),
        "descriptor-driven summaries must exclude KDEGames6 from KF6 "
        "component values"
    );
    require_true(
        dependencies.kdegames.enabled
            && dependencies.kdegames.owners.contains("desktop"),
        "descriptor-driven summaries must promote KDEGames6 into its "
        "own package slot"
    );
    require_true(
        dependencies.gtest.enabled
            && dependencies.gtest.owners.contains("tests"),
        "descriptor-driven summaries must derive GTest from tests.gtest"
    );
    require_true(
        dependencies.benchmark.enabled
            && dependencies.benchmark.owners.contains("benchmarks"),
        "descriptor-driven summaries must derive benchmark support from "
        "benchmarks.google_benchmark"
    );
}

void test_summarize_dependencies_collects_cxxopts_stack_values() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_cxxopts_manifest(), std::nullopt
        );

    require_true(
        dependencies.cxxopts.enabled
            && dependencies.cxxopts.values.contains("cxxopts"),
        "descriptor-driven summaries must collect cxxopts stack values"
    );
}

void test_declared_package_sections_group_kde_by_profile() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );

    const std::vector<ecosystem::package_section> debug_sections
        = ecosystem::declared_package_sections(dependencies, false);
    const ecosystem::package_section* debug_required = find_package_section(
        debug_sections, ecosystem::package_group::required
    );
    const ecosystem::package_section* debug_kde = find_package_section(
        debug_sections, ecosystem::package_group::profile_kde
    );
    require_true(
        debug_required != nullptr,
        "declared package catalog must include a required section"
    );
    require_true(
        debug_kde != nullptr,
        "declared package catalog must include a kde profile section"
    );
    require_true(
        find_package_line(*debug_required, "KF6 CONFIG components") == nullptr,
        "non-kde declared package sections must keep KDE requirements "
        "out of the required section"
    );
    require_true(
        find_package_line(*debug_kde, "KF6 CONFIG components") != nullptr,
        "non-kde declared package sections must expose KDE requirements "
        "in the kde profile section"
    );

    const std::vector<ecosystem::package_section> kde_sections
        = ecosystem::declared_package_sections(dependencies, true);
    const ecosystem::package_section* kde_required = find_package_section(
        kde_sections, ecosystem::package_group::required
    );
    require_true(
        kde_required != nullptr,
        "kde declared package sections must include a required section"
    );
    require_true(
        find_package_line(*kde_required, "KF6 CONFIG components") != nullptr,
        "kde declared package sections must move KDE requirements into "
        "the required section"
    );
    require_true(
        find_package_section(
            kde_sections, ecosystem::package_group::profile_kde
        ) == nullptr,
        "kde declared package sections must not emit an empty kde "
        "profile section"
    );
}

void test_configured_package_sections_report_component_and_profile_state() {
    ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );
    dependencies.qt.values.insert("Test");

    ecosystem::cmake_cache_snapshot cache;
    cache.path = "CMakeCache.txt";
    cache.entries["nlohmann_json_DIR"] = "/deps/json";
    cache.entries["LLVM_DIR"] = "/deps/llvm";
    cache.entries["Clang_DIR"] = "/deps/clang";
    cache.entries["Qt6Core_DIR"] = "/deps/qtcore";
    cache.entries["Qt6Widgets_DIR"] = "/deps/qtwidgets";
    cache.entries["JAVA_INCLUDE_PATH"] = "/deps/jni/include";
    cache.entries["JAVA_JVM_LIBRARY"] = "/deps/jni/libjvm.so";
    cache.entries["cxxopts_DIR"] = "/deps/cxxopts";
    cache.entries["Eigen3_DIR"] = "/deps/eigen";
    cache.entries["GTest_DIR"] = "/deps/gtest";
    cache.entries["ECOSYSTEM_PROFILE_KDE"] = "OFF";

    const std::vector<ecosystem::package_status_section> sections
        = ecosystem::configured_package_sections(dependencies, cache);

    const ecosystem::package_status_section* required
        = find_package_status_section(
            sections, ecosystem::package_group::required
        );
    const ecosystem::package_status_section* kde = find_package_status_section(
        sections, ecosystem::package_group::profile_kde
    );
    require_true(
        required != nullptr,
        "configured package catalog must include a required section"
    );
    require_true(
        kde != nullptr,
        "configured package catalog must include a kde profile section"
    );

    const ecosystem::package_status_line* qt
        = find_package_status_line(*required, "Qt6 6.0 CONFIG components");
    require_true(
        qt != nullptr,
        "configured package catalog must include a Qt package line"
    );
    require_true(
        qt->status == "detected Core Widgets; missing Test",
        "configured package catalog must report detected and missing Qt "
        "components together"
    );

    const ecosystem::package_status_line* kde_components
        = find_package_status_line(*kde, "KF6 CONFIG components");
    require_true(
        kde_components != nullptr,
        "configured package catalog must include a KDE package line"
    );
    require_true(
        kde_components->status == "profile disabled in this configure data",
        "configured package catalog must preserve disabled-profile "
        "state for KDE packages"
    );
}

void test_package_descriptors_bind_dependency_ids() {
    ecosystem::dependency_summary dependencies;
    dependencies.qt.enabled = true;
    dependencies.qt.values.insert("Core");
    dependencies.kdegames.enabled = true;

    const ecosystem::package_descriptor* qt_descriptor
        = ecosystem::find_package_descriptor(ecosystem::package_kind::qt);
    const ecosystem::package_descriptor* kdegames_descriptor
        = ecosystem::find_package_descriptor(ecosystem::package_kind::kdegames);
    require_true(qt_descriptor != nullptr, "descriptor lookup must resolve Qt");
    require_true(
        kdegames_descriptor != nullptr,
        "descriptor lookup must resolve KDEGames"
    );

    const ecosystem::dependency_entry& qt_entry
        = ecosystem::package_dependency_entry(dependencies, *qt_descriptor);
    const ecosystem::dependency_entry& kdegames_entry
        = ecosystem::package_dependency_entry(
            dependencies, *kdegames_descriptor
        );
    require_true(
        qt_entry.enabled && qt_entry.values.contains("Core"),
        "descriptor dependency-id lookup must return the matching Qt "
        "dependency entry"
    );
    require_true(
        kdegames_entry.enabled,
        "descriptor dependency ids must keep "
        "distinct package entries for KDEGames"
    );
}

void test_package_descriptors_bind_rule_types() {
    const ecosystem::package_descriptor* qt_descriptor
        = ecosystem::find_package_descriptor(ecosystem::package_kind::qt);
    const ecosystem::package_descriptor* benchmark_descriptor
        = ecosystem::find_package_descriptor(
            ecosystem::package_kind::benchmark
        );
    require_true(
        qt_descriptor != nullptr,
        "descriptor lookup must resolve Qt for rule checks"
    );
    require_true(
        benchmark_descriptor != nullptr,
        "descriptor lookup must resolve benchmark for rule checks"
    );

    require_true(
        qt_descriptor->summary_rules.size() == 2,
        "Qt descriptor must keep both stack and test-derived summary rules"
    );
    require_true(
        qt_descriptor->summary_rules[0].source
            == ecosystem::package_summary_source::component_stack,
        "Qt descriptor must keep stack-driven summary sourcing"
    );
    require_true(
        qt_descriptor->summary_rules[1].source
            == ecosystem::package_summary_source::component_tests_flag,
        "Qt descriptor must keep test-driven summary sourcing for QtTest"
    );
    require_true(
        qt_descriptor->summary_rules[1].effect
                == ecosystem::package_summary_effect::fixed_value
            && qt_descriptor->summary_rules[1].value == "Test",
        "Qt descriptor must keep the fixed QtTest summary value"
    );
    require_true(
        qt_descriptor->link_rules.size() == 1
            && qt_descriptor->link_rules[0].source
                == ecosystem::package_link_target_source::
                    dependency_values_prefix
            && qt_descriptor->link_rules[0].value == "Qt6::",
        "Qt descriptor must keep prefix-based link-target rules"
    );
    require_true(
        qt_descriptor->status_rule.source
            == ecosystem::package_status_source::
                dependency_values_prefix_suffix,
        "Qt descriptor must keep value-derived configured-status rules"
    );
    require_true(
        benchmark_descriptor->summary_rules.size() == 1
            && benchmark_descriptor->summary_rules[0].source
                == ecosystem::package_summary_source::component_benchmarks_flag
            && benchmark_descriptor->summary_rules[0].effect
                == ecosystem::package_summary_effect::owner_only,
        "benchmark descriptor must keep benchmark-flag summary rules"
    );
}

void test_registered_packages_keep_stable_order() {
    const std::vector<ecosystem::package_kind> expected {
        ecosystem::package_kind::nlohmann_json,
        ecosystem::package_kind::llvm_clang,
        ecosystem::package_kind::qt,
        ecosystem::package_kind::kde,
        ecosystem::package_kind::kdegames,
        ecosystem::package_kind::opencv,
        ecosystem::package_kind::eigen,
        ecosystem::package_kind::jni,
        ecosystem::package_kind::cxxopts,
        ecosystem::package_kind::gtest,
        ecosystem::package_kind::benchmark,
    };

    std::vector<ecosystem::package_kind> actual;
    for (const ecosystem::package_descriptor& descriptor :
         ecosystem::registered_packages()) {
        actual.push_back(descriptor.kind);
    }

    require_true(
        actual == expected,
        "shared package registry must keep a stable package order"
    );
}

void test_enabled_surface_blocks_deduplicate_kde_block() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );

    const std::vector<ecosystem::package_surface_block> blocks
        = ecosystem::enabled_surface_blocks(dependencies);
    const std::vector<ecosystem::package_surface_block> expected {
        ecosystem::package_surface_block::nlohmann_json,
        ecosystem::package_surface_block::llvm_clang,
        ecosystem::package_surface_block::qt,
        ecosystem::package_surface_block::kde,
        ecosystem::package_surface_block::opencv,
        ecosystem::package_surface_block::eigen,
        ecosystem::package_surface_block::jni,
        ecosystem::package_surface_block::gtest,
        ecosystem::package_surface_block::benchmark,
    };

    require_true(
        blocks == expected,
        "surface block selection must follow descriptor order and "
        "collapse KDE packages into one block"
    );
}

void test_dependency_entry_for_id_returns_matching_entry() {
    ecosystem::dependency_summary dependencies;
    dependencies.cxxopts.enabled = true;
    dependencies.cxxopts.values.insert("cxxopts");

    const ecosystem::dependency_entry& entry
        = ecosystem::dependency_entry_for_id(
            dependencies, ecosystem::dependency_id::cxxopts
        );
    require_true(
        entry.enabled, "dependency id access must resolve the requested entry"
    );
    require_true(
        entry.values.contains("cxxopts"),
        "dependency id access must preserve the requested dependency values"
    );
}

void test_package_surface_block_order_is_stable() {
    const std::vector<ecosystem::package_surface_block> expected {
        ecosystem::package_surface_block::nlohmann_json,
        ecosystem::package_surface_block::llvm_clang,
        ecosystem::package_surface_block::qt,
        ecosystem::package_surface_block::kde,
        ecosystem::package_surface_block::opencv,
        ecosystem::package_surface_block::eigen,
        ecosystem::package_surface_block::jni,
        ecosystem::package_surface_block::cxxopts,
        ecosystem::package_surface_block::gtest,
        ecosystem::package_surface_block::benchmark,
    };
    require_true(
        ecosystem::package_surface_block_order() == expected,
        "shared surface block order must stay stable through the "
        "dedicated block module"
    );
}

void test_package_group_heading_is_stable() {
    require_true(
        ecosystem::package_group_heading(ecosystem::package_group::required)
            == "required",
        "shared package groups must keep the required heading stable"
    );
    require_true(
        ecosystem::package_group_heading(ecosystem::package_group::profile_kde)
            == "profile kde",
        "shared package groups must keep the profile-kde heading stable"
    );
    require_true(
        ecosystem::package_group_heading(ecosystem::package_group::optional)
            == "optional",
        "shared package groups must keep the optional heading stable"
    );
}

void test_configured_status_for_package_reports_profile_disabled_kde() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );

    ecosystem::cmake_cache_snapshot cache;
    cache.path = "CMakeCache.txt";
    cache.entries["ECOSYSTEM_PROFILE_KDE"] = "OFF";

    const std::string status = ecosystem::configured_status_for_package(
        ecosystem::package_kind::kde, dependencies, cache
    );
    require_true(
        status == "profile disabled in this configure data",
        "descriptor helper must preserve disabled-profile KDE status"
    );
}

void test_configured_status_for_package_requires_all_jni_cache_keys() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );

    ecosystem::cmake_cache_snapshot cache;
    cache.path = "CMakeCache.txt";
    cache.entries["JAVA_INCLUDE_PATH"] = "/deps/jni/include";

    const std::string status = ecosystem::configured_status_for_package(
        ecosystem::package_kind::jni, dependencies, cache
    );
    require_true(
        status == "missing",
        "descriptor-driven configured status must require every JNI cache key"
    );
}

void test_configured_status_for_package_detects_cxxopts_dir() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_cxxopts_manifest(), std::nullopt
        );

    ecosystem::cmake_cache_snapshot cache;
    cache.path = "CMakeCache.txt";
    cache.entries["cxxopts_DIR"] = "/deps/cxxopts";

    const std::string status = ecosystem::configured_status_for_package(
        ecosystem::package_kind::cxxopts, dependencies, cache
    );
    require_true(
        status == "detected",
        "descriptor-driven configured status must "
        "detect cxxopts from cxxopts_DIR"
    );
}

void test_find_package_surface_rule_returns_qt_rule() {
    const ecosystem::package_surface_rule* rule
        = ecosystem::find_package_surface_rule(
            ecosystem::package_surface_block::qt
        );
    require_true(
        rule != nullptr, "surface-rule lookup must return the Qt rule"
    );
    require_true(
        rule->source == ecosystem::package_surface_source::component_rule,
        "surface-rule lookup must preserve the Qt rule source"
    );
    require_true(
        rule->component_rule.find_package_open_line
            == "find_package(Qt6 6.0 CONFIG REQUIRED COMPONENTS",
        "surface-rule lookup must preserve the Qt package rule data"
    );
}

void test_render_package_surface_block_keeps_header_only_json_cross_compilable() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );
    const ecosystem::package_surface_options options;

    const std::string block = ecosystem::render_package_surface_block(
        ecosystem::package_surface_block::nlohmann_json, dependencies, options
    );
    require_contains(
        block, "if (ANDROID)",
        "header-only JSON discovery must distinguish Android cross-compiles"
    );
    require_contains(
        block,
        "find_package(nlohmann_json 3.12 CONFIG REQUIRED "
        "NO_CMAKE_FIND_ROOT_PATH)",
        "Android must be allowed to use the architecture-independent host "
        "nlohmann_json package"
    );
    require_contains(
        block,
        "file(COPY "
        "\"${ECOSYSTEM_NLOHMANN_JSON_INCLUDE_DIR}/nlohmann\"",
        "Android must stage host header-only JSON files inside the "
        "cross-compile build tree"
    );
    require_contains(
        block,
        "target_include_directories(nlohmann_json::nlohmann_json INTERFACE "
        "\"${ECOSYSTEM_NLOHMANN_JSON_STAGE_DIR}\")",
        "the imported JSON target must expose only the staged Android-safe "
        "include directory"
    );
    require_contains(
        block, "find_package(nlohmann_json 3.12 CONFIG REQUIRED)",
        "desktop JSON discovery must keep its existing package requirement"
    );
}

void test_render_package_surface_block_renders_kde_support() {
    ecosystem::manifest manifest_value = sample_dependency_manifest();
    manifest_value.components.at(2).stack["kde"].push_back("KIOCore");
    manifest_value.components.at(2).stack["kde"].push_back("XmlGui");
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(manifest_value, std::nullopt);

    ecosystem::package_surface_options options;
    options.support_targets = true;
    options.emit_profile_option_lines = true;

    const std::string block = ecosystem::render_package_surface_block(
        ecosystem::package_surface_block::kde, dependencies, options
    );
    require_contains(
        block, "option(ECOSYSTEM_PROFILE_KDE",
        "descriptor surface helper must emit the KDE profile option "
        "when requested"
    );
    require_contains(
        block, "find_package(KF6CoreAddons CONFIG REQUIRED)",
        "descriptor surface helper must emit independently discoverable KF6 "
        "package requirements"
    );
    require_contains(
        block, "find_package(KF6XmlGui CONFIG REQUIRED)",
        "descriptor surface helper must preserve every requested KF6 package"
    );
    require_contains(
        block, "find_package(KF6KIO CONFIG REQUIRED)",
        "descriptor surface helper must map KIO targets to their shared KF6KIO "
        "package"
    );
    require_not_contains(
        block, "find_package(KF6KIOCore",
        "descriptor surface helper must not treat a KIO target as a package"
    );
    require_not_contains(
        block, "find_package(KF6 CONFIG REQUIRED COMPONENTS",
        "descriptor surface helper must not require a nonexistent KF6 "
        "umbrella package"
    );
    require_contains(
        block, "find_package(KDEGames6 REQUIRED)",
        "descriptor surface helper must emit KDEGames6 requirements"
    );
    require_contains(
        block, "add_library(ecosystem_kde_support INTERFACE)",
        "descriptor surface helper must emit the shared KDE support "
        "target when requested"
    );
    require_contains(
        block, "target_link_libraries(ecosystem_kde_support INTERFACE",
        "profile surface rules must emit the shared KDE support target body"
    );
    require_contains(
        block, "            KF6::CoreAddons",
        "profile surface rules must emit derived KF6 support target links"
    );
    require_contains(
        block, "            KDEGames6\n",
        "profile surface rules must preserve gated KDEGames6 support links"
    );
}

void test_render_package_surface_block_renders_qt_support() {
    const ecosystem::manifest manifest_value = sample_qttest_manifest();
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(manifest_value, std::nullopt);

    ecosystem::package_surface_options options;
    options.qt_automation = true;

    const std::string block = ecosystem::render_package_surface_block(
        ecosystem::package_surface_block::qt, dependencies, options
    );
    require_contains(
        block, "set(QT_DEFAULT_MAJOR_VERSION 6)",
        "component surface rules must emit the shared Qt preamble"
    );
    require_contains(
        block, "find_package(Qt6 6.0 CONFIG REQUIRED COMPONENTS",
        "component surface rules must emit the Qt package lookup"
    );
    require_contains(
        block, "        Test\n",
        "component surface rules must include the derived Qt Test component"
    );
    require_contains(
        block, "set(CMAKE_AUTOMOC ON)",
        "component surface rules must emit Qt automation lines when requested"
    );
}

void test_render_package_surface_block_renders_llvm_support() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );

    ecosystem::package_surface_options options;
    options.support_targets = true;

    const std::string block = ecosystem::render_package_surface_block(
        ecosystem::package_surface_block::llvm_clang, dependencies, options
    );
    require_contains(
        block, "find_package(LLVM CONFIG REQUIRED)",
        "surface block dispatch must render LLVM package requirements"
    );
    require_contains(
        block, "find_package(Clang CONFIG REQUIRED)",
        "surface block dispatch must render Clang package requirements"
    );
    require_contains(
        block, "add_library(ecosystem_llvm_clang_support INTERFACE)",
        "surface block dispatch must render the LLVM/Clang support target"
    );
    require_contains(
        block,
        "target_include_directories(ecosystem_llvm_clang_support SYSTEM "
        "INTERFACE",
        "surface block dispatch must render LLVM/Clang include directories"
    );
}

void test_render_package_surface_block_renders_opencv_support() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_dependency_manifest(), std::nullopt
        );

    ecosystem::package_surface_options options;
    options.support_targets = true;

    const std::string block = ecosystem::render_package_surface_block(
        ecosystem::package_surface_block::opencv, dependencies, options
    );
    require_contains(
        block, "find_package(OpenCV QUIET)",
        "shared static surface rules must emit OpenCV package lookup"
    );
    require_contains(
        block, "add_library(ecosystem_optional_opencv INTERFACE)",
        "shared static surface rules must emit the OpenCV support target"
    );
    require_contains(
        block, "if (OpenCV_FOUND)",
        "shared static surface rules must preserve the OpenCV "
        "availability guard"
    );
    require_contains(
        block,
        "target_include_directories(ecosystem_optional_opencv INTERFACE "
        "${OpenCV_INCLUDE_DIRS})",
        "shared static surface rules must emit OpenCV include directories"
    );
}

void test_render_package_surface_block_renders_cxxopts_support() {
    const ecosystem::dependency_summary dependencies
        = ecosystem::summarize_dependencies(
            sample_cxxopts_manifest(), std::nullopt
        );

    const std::string block = ecosystem::render_package_surface_block(
        ecosystem::package_surface_block::cxxopts, dependencies,
        ecosystem::package_surface_options {}
    );
    require_contains(
        block, "find_package(cxxopts CONFIG REQUIRED)",
        "shared static surface rules must emit the cxxopts package lookup"
    );
    require_contains(
        block, "find_package(cxxopts CONFIG REQUIRED NO_CMAKE_FIND_ROOT_PATH)",
        "Android cross-compiles must be allowed to use the "
        "architecture-independent host cxxopts package"
    );
    require_contains(
        block,
        "file(COPY \"${ECOSYSTEM_CXXOPTS_INCLUDE_DIR}/cxxopts.hpp\"",
        "Android must stage the host cxxopts header inside the cross-compile "
        "build tree"
    );
    require_contains(
        block,
        "target_include_directories(cxxopts::cxxopts INTERFACE "
        "\"${ECOSYSTEM_CXXOPTS_STAGE_DIR}\")",
        "the imported cxxopts target must expose the staged Android-safe "
        "include directory"
    );
}

ecosystem::string_list workspace_conformance_errors(const fs::path& root) {
    ecosystem::string_list errors;
    const auto workspace = ecosystem::discover_workspace(root);
    if (!workspace) {
        return { "no managed workspace found: " + root.string() };
    }
    errors = workspace->errors;
    for (const auto& project : workspace->projects) {
        const auto label
            = project.root.lexically_relative(root).generic_string();
        if (!project.valid()) {
            for (const auto& error : project.errors)
                errors.push_back(label + ": " + error);
            continue;
        }
        ecosystem::string_list render_errors;
        const auto files = ecosystem::generate_tracked_surface_files(
            *project.manifest_value, project.root, &render_errors
        );
        for (const auto& error : render_errors)
            errors.push_back(label + ": " + error);
        for (const auto& file : files) {
            const auto path = project.root / file.relative_path;
            try {
                if (read_text(path) != file.contents)
                    errors.push_back("stale surface: " + path.string());
            } catch (const std::exception& error) {
                errors.push_back(error.what());
            }
        }
    }
    return errors;
}

void test_workspace_conformance_reports_all_projects() {
    temp_dir root;
    for (const auto* name : { "alpha", "beta" }) {
        write_sample_workspace_project(root.path(), name);
        require_sync_success(root.path() / name, "must sync fixture project");
    }
    require_true(
        workspace_conformance_errors(root.path()).empty(),
        "fresh isolated workspace must conform"
    );
    write_text(root.path() / "broken/manifest.json", "{ invalid JSON\n");
    write_text(root.path() / "alpha/CMakeLists.txt", "stale\n");
    write_text(root.path() / "beta/.clang-format", "stale\n");
    const auto errors = join_lines(workspace_conformance_errors(root.path()));
    for (const auto* issue :
         { "broken", "alpha/CMakeLists.txt", "beta/.clang-format" })
        require_contains(
            errors, issue,
            "conformance must report every broken project and surface"
        );
    require_true(
        read_text(root.path() / "alpha/CMakeLists.txt") == "stale\n",
        "conformance must be read-only"
    );
}

void test_tracked_surface_generation_uses_github_vars_file() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    write_text(
        root.path() / "manifesto.github.vars.json",
        "{\n"
        "  \"manifesto_repository\": \"example/ecosystem\",\n"
        "  \"manifesto_ref\": \"release/preview\",\n"
        "  \"manifesto_build_parallelism\": \"3\",\n"
        "  \"sphinx_theme\": \"furo\",\n"
        "  \"sphinx_theme_package\": \"furo\",\n"
        "  \"checkout_action\": \"actions/checkout@checkout-pin\",\n"
        "  \"install_qt_action\": \"vendor/qt@qt-pin\",\n"
        "  \"github_script_action\": \"actions/github-script@script-pin\",\n"
        "  \"codeql_action_ref\": \"codeql-pin\",\n"
        "  \"upload_artifact_action\": "
        "\"actions/upload-artifact@upload-pin\",\n"
        "  \"download_artifact_action\": "
        "\"actions/download-artifact@download-pin\",\n"
        "  \"configure_pages_action\": "
        "\"actions/configure-pages@configure-pin\",\n"
        "  \"upload_pages_artifact_action\": "
        "\"actions/upload-pages-artifact@pages-upload-pin\",\n"
        "  \"deploy_pages_action\": \"actions/deploy-pages@pages-deploy-pin\"\n"
        "}\n"
    );

    const ecosystem::manifest_report report
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        report.errors.empty(),
        "sample manifest must load before tracked-surface generation"
    );
    require_true(
        report.value.has_value(),
        "sample manifest must parse before tracked-surface generation"
    );

    ecosystem::string_list errors;
    const std::vector<ecosystem::tracked_surface_file> generated_files
        = ecosystem::generate_tracked_surface_files(
            *report.value, root.path(), &errors
        );
    require_true(
        errors.empty(), "tracked surface generation must accept a vars file"
    );

    const ecosystem::tracked_surface_file* tests_workflow
        = find_tracked_surface_file(
            generated_files, ".github/workflows/tests.yml"
        );
    require_true(
        tests_workflow != nullptr,
        "tracked surface generation must include the tests workflow"
    );
    require_contains(
        tests_workflow->contents, "uses: ./.github/actions/setup-manifesto",
        "tests workflow must use the generated local setup action"
    );
    require_contains(
        tests_workflow->contents, "uses: actions/checkout@checkout-pin",
        "tests workflow must use the configured immutable checkout action"
    );
    require_contains(
        tests_workflow->contents, "uses: actions/upload-artifact@upload-pin",
        "tests workflow must use the configured immutable upload action"
    );
    require_contains(
        tests_workflow->contents, "manifesto-repository: \"example/ecosystem\"",
        "tests workflow must pass the configured ecosystem repository"
    );
    require_contains(
        tests_workflow->contents, "manifesto-ref: \"release/preview\"",
        "tests workflow must pass the configured ecosystem ref"
    );
    require_contains(
        tests_workflow->contents, "workflow_dispatch:",
        "tests workflow must stay branch-friendly through manual dispatch"
    );
    require_contains(
        tests_workflow->contents, "cancel-in-progress: true",
        "tests workflow must cancel superseded branch runs"
    );
    require_contains(
        tests_workflow->contents, "uses: ./.github/actions/run-manifesto-stage",
        "tests workflow must reuse the shared stage runner"
    );
    require_contains(
        tests_workflow->contents,
        "uses: ./.github/actions/publish-manifesto-report",
        "tests workflow must publish the shared CI report surface"
    );
    require_contains(
        tests_workflow->contents,
        "\"${{ steps.manifesto.outputs.engels-binary }}\" check ci",
        "CI must invoke the canonical local aggregate"
    );
    for (const auto* operation :
         { "check naming", "report cxx", "check tidy", "check leaks",
           "check sphinx", "build release", "package", "sync", "git diff" }) {
        require_not_contains(
            tests_workflow->contents, operation,
            "default CI must delegate policy without repairing or expanding it"
        );
    }

    const ecosystem::tracked_surface_file* html_workflow
        = find_tracked_surface_file(
            generated_files, ".github/workflows/html.yml"
        );
    require_true(
        html_workflow != nullptr,
        "tracked surface generation must include the deploy workflow"
    );
    require_contains(
        html_workflow->contents, "check sphinx --theme furo",
        "deploy workflow must forward the configured sphinx theme"
    );
    require_contains(
        html_workflow->contents, "uses: actions/configure-pages@configure-pin",
        "deploy workflow must use the configured Pages setup action"
    );
    require_contains(
        html_workflow->contents,
        "uses: actions/upload-pages-artifact@pages-upload-pin",
        "deploy workflow must use the configured Pages upload action"
    );
    require_contains(
        html_workflow->contents, "uses: actions/deploy-pages@pages-deploy-pin",
        "deploy workflow must use the configured Pages deployment action"
    );
    require_not_contains(
        html_workflow->contents, "Checkout manifesto tool",
        "deploy workflow must stay thin and rely on the shared setup action"
    );
    require_contains(
        html_workflow->contents, "sphinx-theme-package: furo",
        "deploy workflow must pass the configured sphinx theme package"
    );
    require_contains(
        html_workflow->contents,
        "uses: ./.github/actions/publish-manifesto-report",
        "deploy workflow must publish the shared Pages report surface"
    );
    require_contains(
        html_workflow->contents,
        "\"${{ steps.manifesto.outputs.engels-binary }}\" check coverage",
        "deploy workflow must stage manifesto-owned coverage checks"
    );

    const ecosystem::tracked_surface_file* setup_action
        = find_tracked_surface_file(
            generated_files, ".github/actions/setup-manifesto/action.yml"
        );
    require_true(
        setup_action != nullptr,
        "tracked surface generation must include the local setup action"
    );
    const auto native_dependencies = setup_action->contents.substr(
        setup_action->contents.find("packages=("),
        setup_action->contents.find("if [ \"${{ inputs.install-docs }}\"")
            - setup_action->contents.find("packages=(")
    );
    require_contains(
        native_dependencies, "doxygen",
        "ordinary native documentation regressions require Doxygen without "
        "enabling optional presentation"
    );
    require_contains(
        native_dependencies, "graphviz",
        "ordinary native documentation regressions require the shared graph "
        "renderer"
    );
    require_contains(
        setup_action->contents,
        "repository: ${{ inputs.manifesto-repository }}",
        "generated setup action must stay reusable through workflow inputs"
    );
    require_contains(
        setup_action->contents, "uses: vendor/qt@qt-pin",
        "generated setup action must use the configured immutable Qt action"
    );
    require_contains(
        setup_action->contents,
        "python3 -m pip install --user sphinx myst-parser "
        "\"${{ inputs.sphinx-theme-package }}\"",
        "generated setup action must install the workflow-selected "
        "Sphinx theme package"
    );
    require_contains(
        setup_action->contents, "--parallel \"$BUILD_PARALLELISM\"",
        "generated setup action must cap build parallelism through inputs"
    );
    require_contains(
        setup_action->contents, "marx-binary=$build_root/marx",
        "generated setup action must expose the bootstrap Marx binary"
    );
    require_contains(
        setup_action->contents, "engels-binary=$build_root/engels",
        "bootstrap must also expose Engels"
    );
    require_not_contains(
        setup_action->contents, "manifesto-binary",
        "bootstrap must not expose a third actor"
    );
    require_not_contains(
        tests_workflow->contents, "manifesto-binary",
        "workflows must use explicit owners"
    );
    require_not_contains(
        setup_action->contents, "build debug engels:engels",
        "generated setup action must not require a second frontend bootstrap "
        "step"
    );

    const ecosystem::tracked_surface_file* stage_action
        = find_tracked_surface_file(
            generated_files, ".github/actions/run-manifesto-stage/action.yml"
        );
    require_true(
        stage_action != nullptr,
        "tracked surface generation must include the shared stage runner"
    );
    require_contains(
        stage_action->contents, "status as passed, failed, or skipped",
        "shared stage runner must expose non-fatal stage status reporting"
    );

    const ecosystem::tracked_surface_file* report_action
        = find_tracked_surface_file(
            generated_files,
            ".github/actions/publish-manifesto-report/action.yml"
        );
    require_true(
        report_action != nullptr,
        "tracked surface generation must include the shared report publisher"
    );
    require_contains(
        report_action->contents, "issues.updateComment",
        "shared report publisher must update sticky pull request comments"
    );
    require_contains(
        report_action->contents, "issues.create({",
        "shared report publisher must create default-branch issues when "
        "needed"
    );
    require_contains(
        report_action->contents, "uses: actions/github-script@script-pin",
        "report publisher must use the configured immutable script action"
    );

    const ecosystem::tracked_surface_file* codeql_workflow
        = find_tracked_surface_file(
            generated_files, ".github/workflows/codeql.yml"
        );
    require_true(
        codeql_workflow != nullptr,
        "tracked surface generation must include the CodeQL workflow"
    );
    require_contains(
        codeql_workflow->contents, "uses: github/codeql-action/init@codeql-pin",
        "CodeQL workflow must use the configured immutable action ref"
    );

    const ecosystem::tracked_surface_file* clang_format
        = find_tracked_surface_file(generated_files, ".clang-format");
    require_true(
        clang_format != nullptr,
        "tracked surface generation must include the shared format file"
    );
    require_contains(
        clang_format->contents, "ColumnLimit:     80",
        "tracked format file must retain the shipped style defaults"
    );
}

void test_tracked_surface_generation_supports_remote_setup_action() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    write_text(
        root.path() / "manifesto.github.vars.json",
        "{\n"
        "  \"manifesto_repository\": \"example/ecosystem\",\n"
        "  \"manifesto_ref\": \"release/preview\",\n"
        "  \"manifesto_setup_action\": "
        "\"example/ecosystem/.github/actions/setup-manifesto@release/"
        "preview\",\n"
        "  \"manifesto_build_parallelism\": \"3\",\n"
        "  \"sphinx_theme\": \"furo\",\n"
        "  \"sphinx_theme_package\": \"furo\"\n"
        "}\n"
    );

    const ecosystem::manifest_report report
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        report.errors.empty(),
        "sample manifest must load before remote-action tracked-surface "
        "generation"
    );
    require_true(
        report.value.has_value(),
        "sample manifest must parse before remote-action tracked-surface "
        "generation"
    );

    ecosystem::string_list errors;
    const std::vector<ecosystem::tracked_surface_file> generated_files
        = ecosystem::generate_tracked_surface_files(
            *report.value, root.path(), &errors
        );
    require_true(
        errors.empty(),
        "tracked surface generation must accept a remote setup action"
    );

    const ecosystem::tracked_surface_file* tests_workflow
        = find_tracked_surface_file(
            generated_files, ".github/workflows/tests.yml"
        );
    require_true(
        tests_workflow != nullptr,
        "remote-action tracked surface generation must include the tests "
        "workflow"
    );
    require_contains(
        tests_workflow->contents,
        "uses: "
        "example/ecosystem/.github/actions/setup-manifesto@release/preview",
        "tests workflow must use the configured remote setup action"
    );
    require_not_contains(
        tests_workflow->contents, "uses: ./.github/actions/setup-manifesto",
        "tests workflow must not keep the local setup action when a remote "
        "action is configured"
    );

    const ecosystem::tracked_surface_file* html_workflow
        = find_tracked_surface_file(
            generated_files, ".github/workflows/html.yml"
        );
    require_true(
        html_workflow != nullptr,
        "remote-action tracked surface generation must include the deploy "
        "workflow"
    );
    require_not_contains(
        html_workflow->contents, "Checkout manifesto tool",
        "deploy workflow must stay thin when a remote setup action is used"
    );
    require_contains(
        tests_workflow->contents, "build-parallelism: 3",
        "tests workflow must forward configured build parallelism"
    );
    require_true(
        find_tracked_surface_file(
            generated_files, ".github/actions/run-manifesto-stage/action.yml"
        ) != nullptr,
        "remote-action tracked surface generation must keep the shared stage "
        "runner"
    );
    require_true(
        find_tracked_surface_file(
            generated_files,
            ".github/actions/publish-manifesto-report/action.yml"
        ) != nullptr,
        "remote-action tracked surface generation must keep the shared "
        "report publisher"
    );

    require_true(
        find_tracked_surface_file(
            generated_files, ".github/actions/setup-manifesto/action.yml"
        ) == nullptr,
        "remote-action tracked surface generation must not materialize the "
        "local setup action"
    );
}

void test_required_template_failures_preserve_generated_state() {
    for (const std::string path :
         { "cmake/apply_defaults.tpl", "cmake/visitor_facade.tpl",
           "tracked/.gitignore.tpl", ".github/workflows/tests.yml",
           "cmake/package_surface/add_interface_library.tpl" }) {
        temp_dir root;
        temp_dir templates;
        auto value = sample_manifest();
        value.components.front().stack
            = json::object({ { "llvm_clang", true } });
        write_text(root.path() / "CMakeLists.txt", "previous facade\n");
        write_text(root.path() / ".gitignore", "previous ignores\n");
        write_text(templates.path() / path, "{{required_missing_binding}}\n");
        scoped_env env("MANIFESTO_TEMPLATE_ROOT", templates.path().string());

        const auto report = ecosystem::sync_project(root.path(), value);
        require_true(
            !report.errors.empty() && report.written_files.empty(),
            "required template failure must fail before generated writes: "
                + path
        );
        require_contains(
            join_lines(report.errors), path,
            "render failure must identify its template"
        );
        require_true(
            read_text(root.path() / "CMakeLists.txt") == "previous facade\n"
                && read_text(root.path() / ".gitignore")
                    == "previous ignores\n",
            "failed generation must preserve previous generated contents"
        );
        require_true(
            !fs::exists(root.path() / ".github"),
            "failed rendering must not create partial workflows"
        );
    }
}

void test_required_developer_template_failure_is_reported() {
    temp_dir root;
    temp_dir templates;
    const auto value = sample_manifest();
    const fs::path cmake
        = ecosystem::local_developer_cmakelists_path(root.path());
    write_text(cmake, "previous developer surface\n");
    write_text(
        templates.path() / "cmake/developer_options.tpl", "{{missing}}\n"
    );
    scoped_env env("MANIFESTO_TEMPLATE_ROOT", templates.path().string());
    std::string error;
    const auto status
        = ecosystem::ensure_local_developer_surface(root.path(), value, &error);
    require_true(
        status == ecosystem::command_error::task_failed,
        "developer generation must translate render failure to operation "
        "failure"
    );
    require_contains(
        error, "cmake/developer_options.tpl",
        "developer error must name its template"
    );
    require_true(
        read_text(cmake) == "previous developer surface\n",
        "developer render failure must preserve its previous surface"
    );
}

void test_required_missing_and_unreadable_templates_fail() {
    for (const std::string path :
         { "missing-required-test.tpl", "unreadable-required-test.tpl" }) {
        temp_dir templates;
        fs::create_directories(
            templates.path() / "unreadable-required-test.tpl"
        );
        scoped_env env("MANIFESTO_TEMPLATE_ROOT", templates.path().string());
        bool failed = false;
        try {
            ecosystem::render_required_text_template(path, {});
        } catch (const ecosystem::template_render_error& error) {
            failed = true;
            require_contains(
                error.what(), path,
                "required error must identify missing or unreadable template"
            );
        }
        require_true(
            failed, "required renderer must throw on unavailable template"
        );
    }
}

void test_sync_project_removes_obsolete_tracked_surface_files() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    const ecosystem::manifest_report initial_report
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        initial_report.errors.empty(),
        "sample manifest must load before initial sync"
    );
    require_true(
        initial_report.value.has_value(),
        "sample manifest must parse before initial sync"
    );

    const ecosystem::sync_report initial_sync
        = ecosystem::sync_project(root.path(), *initial_report.value);
    require_true(
        initial_sync.errors.empty(),
        "initial sync must succeed before obsolete-surface cleanup"
    );
    require_true(
        fs::exists(root.path() / ".github/actions/setup-manifesto/action.yml"),
        "initial sync must materialize the local setup action"
    );

    write_text(
        root.path() / "manifesto.github.vars.json",
        "{\n"
        "  \"manifesto_setup_action\": "
        "\"example/ecosystem/.github/actions/setup-manifesto@release/"
        "preview\"\n"
        "}\n"
    );

    const ecosystem::manifest_report updated_report
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        updated_report.errors.empty(),
        "sample manifest must reload before obsolete-surface cleanup"
    );
    require_true(
        updated_report.value.has_value(),
        "sample manifest must reparse before obsolete-surface cleanup"
    );

    const ecosystem::string_list drift
        = ecosystem::tracked_surface_drift(root.path(), *updated_report.value);
    require_contains(
        join_lines(drift),
        ".github/actions/setup-manifesto/action.yml is obsolete for current "
        "ecosystem defaults; run `marx sync`",
        "tracked surface drift must report obsolete sync-owned files"
    );

    const ecosystem::sync_report updated_sync
        = ecosystem::sync_project(root.path(), *updated_report.value);
    require_true(
        updated_sync.errors.empty(),
        "sync must remove obsolete sync-owned files"
    );

    bool removed_local_action = false;
    for (const fs::path& removed_file : updated_sync.removed_files) {
        if (removed_file
            == root.path() / ".github/actions/setup-manifesto/action.yml") {
            removed_local_action = true;
            break;
        }
    }
    require_true(
        removed_local_action, "sync must report the removed local setup action"
    );
    require_true(
        !fs::exists(root.path() / ".github/actions/setup-manifesto/action.yml"),
        "sync must remove obsolete local setup action files"
    );
}

std::string github_shell_program(
    const std::string& yaml, const std::string& step_name,
    const std::size_t indentation
) {
    const auto step = yaml.find("- name: " + step_name + "\n");
    require_true(step != std::string::npos, "missing CI step: " + step_name);
    const std::string prefix(indentation, ' ');
    const std::string marker = prefix + "run: |\n";
    const auto start = yaml.find(marker, step);
    require_true(start != std::string::npos, "missing CI shell program");
    std::istringstream lines(yaml.substr(start + marker.size()));
    std::string program, line;
    while (std::getline(lines, line)) {
        if (!line.empty() && !line.starts_with(prefix + "  "))
            break;
        program += (line.empty() ? line : line.substr(indentation + 2)) + "\n";
    }
    return program;
}

std::string github_substitute(
    std::string text, const std::string& expression, const std::string& value
) {
    const auto token = "${{ " + expression + " }}";
    std::size_t offset = 0;
    while ((offset = text.find(token, offset)) != std::string::npos) {
        text.replace(offset, token.size(), value);
        offset += value.size();
    }
    return text;
}

void test_ci_stage_reports_intermediate_and_pipeline_failures() {
    const std::string action = ecosystem::render_required_text_template(
        "tracked/.github/actions/run-manifesto-stage/action.yml.tpl", {}
    );
    const std::string marker = "      run: |\n";
    const auto start = action.find(marker);
    require_true(
        start != std::string::npos,
        "stage action must contain its shell program"
    );
    std::istringstream lines(action.substr(start + marker.size()));
    std::string program, line;
    while (std::getline(lines, line)) {
        program
            += (line.starts_with("        ") ? line.substr(8) : line) + "\n";
    }
    temp_dir project;
    const auto generated = ecosystem::generate_tracked_surface_files(
        sample_manifest(), project.path()
    );
    for (const auto& filename : { "tests.yml", "codeql.yml", "html.yml" }) {
        const auto* workflow = find_tracked_surface_file(
            generated, std::string(".github/workflows/") + filename
        );
        require_true(workflow != nullptr, "workflow must be generated");
        if (std::string(filename) == "html.yml") {
            require_contains(
                workflow->contents, "on:\n  workflow_dispatch:\n",
                "presentation workflow must retain manual dispatch"
            );
            require_not_contains(
                workflow->contents,
                "  push:", "later-stage presentation must not run on pushes"
            );
            require_not_contains(
                workflow->contents, "  pull_request:",
                "later-stage presentation must not run on pull requests"
            );
            require_contains(
                workflow->contents, "steps.coverage.outputs.status == 'passed'",
                "coverage uploads must use the status output provided by the "
                "stage action"
            );
            require_not_contains(
                workflow->contents, "steps.coverage.outputs.enabled",
                "coverage uploads must not depend on a nonexistent output"
            );
        } else if (std::string(filename) == "codeql.yml") {
            const auto trigger
                = workflow->contents.find("      - 'templates/**'");
            require_true(
                trigger != std::string::npos
                    && workflow->contents.find(
                           "      - 'templates/**'", trigger + 1
                       ) != std::string::npos,
                "template changes must trigger both push and pull-request "
                "checks"
            );
            require_not_contains(
                workflow->contents, "check sphinx",
                "ordinary CI must not depend on later-stage presentation"
            );
            require_not_contains(
                workflow->contents, "install-docs: 'true'",
                "ordinary CI must not install presentation dependencies"
            );
        }
    }

    struct stage_case {
        std::string command;
        std::string status;
        int exit_code;
    };

    for (const auto& item : std::vector<stage_case> {
             { "false\nprintf should-not-run", "failed", 1 },
             { "false | cat", "failed", 1 },
             { "printf success", "passed", 0 },
             { "exit 3", "skipped", 3 },
         }) {
        temp_dir root;
        std::string script = program;
        for (const auto& [name, value] :
             std::vector<std::pair<std::string, std::string>> {
                 { "stage-id", "audit" },
                 { "stage-label", "Audit" },
                 { "shell-command", item.command },
                 { "skipped-exit-codes", "3" },
             }) {
            const std::string token = "${{ inputs." + name + " }}";
            std::size_t offset = 0;
            while ((offset = script.find(token, offset)) != std::string::npos) {
                script.replace(offset, token.size(), value);
                offset += value.size();
            }
        }
        const fs::path command = root.path() / "stage.sh";
        const fs::path outputs = root.path() / "outputs";
        write_text(command, script);
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", command.string() }, root.path(),
            { { "GITHUB_OUTPUT", outputs.string() },
              { "GITHUB_STEP_SUMMARY", (root.path() / "summary").string() } }
        );
        require_true(
            result.exit_code == 0,
            "stage must keep the workflow alive to report: " + result.output
        );
        require_contains(
            read_text(outputs), "status=" + item.status + "\n",
            "stage status must reflect every failure"
        );
        require_contains(
            read_text(outputs),
            "exit-code=" + std::to_string(item.exit_code) + "\n",
            "stage must retain the child exit status"
        );
        require_not_contains(
            read_text(
                root.path() / ".ecosystem/github/reports/audit/output.log"
            ),
            "should-not-run", "stage must stop after an intermediate failure"
        );
    }
}

void test_ci_required_result_enforces_failures_and_missing_evidence() {
    temp_dir root;
    const auto files = ecosystem::generate_tracked_surface_files(
        sample_manifest(), root.path()
    );
    const auto* workflow
        = find_tracked_surface_file(files, ".github/workflows/tests.yml");
    require_true(workflow != nullptr, "required workflow must exist");
    require_not_contains(
        workflow->contents, "paths:",
        "required checks must cover arbitrary manifest-owned source directories"
    );
    for (const auto* name : { "Upload check reports", "Publish check report",
                              "Enforce required result" }) {
        require_contains(
            workflow->contents,
            std::string("- name: ") + name + "\n        if: ${{ always() }}",
            "reporting and enforcement must also run after setup or stage "
            "failures"
        );
    }
    const auto gate = root.path() / "gate.sh";
    write_text(
        gate,
        github_shell_program(workflow->contents, "Enforce required result", 8)
    );
    const auto* action = find_tracked_surface_file(
        files, ".github/actions/run-manifesto-stage/action.yml"
    );
    require_true(action != nullptr, "stage runner must exist");
    const auto program
        = github_shell_program(action->contents, "Run stage command", 6);
    const auto report
        = root.path() / ".ecosystem/github/reports/01-required-checks";
    const auto outputs = root.path() / "outputs";
    const auto stage = root.path() / "stage.sh";
    const auto run_gate = [&](const std::string& outcome,
                              const std::string& status,
                              const std::string& code) {
        return ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", gate.string() }, root.path(),
            { { "CHECK_OUTCOME", outcome },
              { "CHECK_STATUS", status },
              { "CHECK_EXIT_CODE", code } }
        );
    };
    require_true(
        run_gate("skipped", "", "").exit_code != 0,
        "failed bootstrap cannot yield a successful required result"
    );
    for (const auto& [command, code] :
         std::vector<std::pair<std::string, int>> {
             { "printf 'beauty finding: advisory\\n'", 0 },
             { "false\nprintf unreachable", 1 },
             { "false | cat", 1 },
             { "exit 3", 3 },
             { "exit 5", 5 },
             { "exit 127", 127 } }) {
        auto script = program;
        for (const auto& [name, value] :
             std::vector<std::pair<std::string, std::string>> {
                 { "stage-id", "01-required-checks" },
                 { "stage-label", "Required local checks" },
                 { "shell-command", command },
                 { "skipped-exit-codes", "" } }) {
            script = github_substitute(script, "inputs." + name, value);
        }
        write_text(stage, script);
        write_text(outputs, "");
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", stage.string() }, root.path(),
            { { "GITHUB_OUTPUT", outputs.string() },
              { "GITHUB_STEP_SUMMARY", (root.path() / "summary").string() } }
        );
        require_true(
            result.exit_code == 0 && fs::exists(report / "summary.md"),
            "every completed stage must preserve its report before enforcement"
        );
        const auto status = code == 0 ? "passed" : "failed";
        require_contains(
            read_text(outputs), "exit-code=" + std::to_string(code) + "\n",
            "stage must expose its actual process exit code"
        );
        require_true(
            (run_gate("success", status, std::to_string(code)).exit_code == 0)
                == (code == 0),
            "required CI outcome must follow the local exit code, including "
            "unsupported and missing tools"
        );
    }
    require_true(
        run_gate("failure", "passed", "0").exit_code != 0,
        "partial outputs from an incomplete reporting step cannot pass"
    );
    require_true(
        run_gate("success", "skipped", "3").exit_code != 0,
        "a required result cannot be skipped"
    );
    fs::remove(report / "output.log");
    require_true(
        run_gate("success", "passed", "0").exit_code != 0,
        "a missing required log must fail closed"
    );
    write_text(report / "output.log", "");
    write_text(report / "summary.md", "");
    require_true(
        run_gate("success", "passed", "0").exit_code != 0,
        "an empty required summary must fail closed"
    );
    fs::remove(report / "summary.md");
    require_true(
        run_gate("success", "passed", "0").exit_code != 0,
        "a missing required summary must fail closed"
    );

    const auto* codeql
        = find_tracked_surface_file(files, ".github/workflows/codeql.yml");
    require_true(codeql != nullptr, "CodeQL adapter must exist");
    write_text(
        gate, github_shell_program(codeql->contents, "Enforce CodeQL result", 8)
    );
    const std::vector<std::pair<std::string, std::string>> clean {
        { "REPOSITORY_STATUS", "passed" },
        { "CODEQL_INIT_STATUS", "success" },
        { "CODEQL_BUILD_STATUS", "passed" },
        { "CODEQL_ANALYZE_STATUS", "success" }
    };
    require_true(
        ecosystem::capture_command_result(
            { "bash", gate.string() }, root.path(), clean
        )
                .exit_code
            == 0,
        "completed CodeQL operations must pass"
    );
    for (std::size_t i = 0; i < clean.size(); ++i) {
        for (const auto* state :
             { "", "failed", "failure", "skipped", "cancelled" }) {
            auto env = clean;
            env[i].second = state;
            require_true(
                ecosystem::capture_command_result(
                    { "bash", gate.string() }, root.path(), env
                )
                        .exit_code
                    != 0,
                "every incomplete CodeQL operation must fail its workflow"
            );
        }
    }
}

void test_github_bootstrap_vars_validate_explicit_selection() {
    temp_dir root;
    const auto generate = [&](const json& config,
                              ecosystem::string_list* errors) {
        write_text(root.path() / "manifesto.github.vars.json", config.dump());
        return ecosystem::generate_tracked_surface_files(
            sample_manifest(), root.path(), errors
        );
    };
    for (const auto& config : std::vector<json> {
             { { "manifesto_bootstrap", "unknown" } },
             { { "manifesto_repository", "example/tools" } },
             { { "manifesto_ref", "reviewed-ref" } },
             { { "manifesto_bootstrap", "checkout" },
               { "manifesto_repository", "example/tools" },
               { "manifesto_ref", "old" } },
             { { "manifesto_source_path", "../tools" } },
             { { "manifesto_source_path", "/tools" } },
             { { "manifesto_source_path", "" } },
             { { "manifesto_source_path", "tools\nnext" } },
             { { "manifesto_source_path", "${{ github.ref }}" } },
             { { "manifesto_build_parallelism", "0" } },
             { { "manifesto_build_parallelism", "2\nnext" } } }) {
        ecosystem::string_list errors;
        generate(config, &errors);
        require_true(
            !errors.empty(),
            "invalid bootstrap inputs must fail generation: " + config.dump()
        );
    }
    for (const auto& config : std::vector<json> {
             json::object(),
             { { "manifesto_bootstrap", "checkout" } },
             { { "manifesto_repository", "example/tools" },
               { "manifesto_ref", "release/preview" },
               { "manifesto_source_path", "tools/ecosystem" } },
             { { "manifesto_repository", "example/tools" },
               { "manifesto_ref", "0123456789abcdef" },
               { "manifesto_source_path", "tool source/#quoted" } } }) {
        ecosystem::string_list errors;
        const auto files = generate(config, &errors);
        require_true(
            errors.empty(), "explicit compatible bootstrap inputs must render"
        );
        for (const auto* name : { "tests.yml", "codeql.yml", "html.yml" }) {
            const auto* workflow = find_tracked_surface_file(
                files, std::string(".github/workflows/") + name
            );
            require_true(
                workflow != nullptr, "all workflow consumers must exist"
            );
            require_not_contains(
                workflow->contents, "ninjaro/cppr",
                "no obsolete repository fallback"
            );
            require_not_contains(
                workflow->contents, "manifesto-ref: master",
                "no mutable implicit ref"
            );
            require_contains(
                workflow->contents,
                "bootstrap: "
                    + config.value("manifesto_bootstrap", json("repository"))
                          .dump(),
                "workflow must forward explicit bootstrap mode as a YAML scalar"
            );
            require_contains(
                workflow->contents,
                "source-path: "
                    + config.value("manifesto_source_path", json(".")).dump(),
                "workflow must preserve the selected source layout"
            );
        }
    }
}

void test_ci_bootstrap_builds_reviewed_checkout_and_repository_layouts() {
    const auto action = ecosystem::render_required_text_template(
        "tracked/.github/actions/setup-manifesto/action.yml.tpl",
        { { "checkout_action", "actions/checkout@v4" },
          { "install_qt_action", "vendor/qt@pin" } }
    );
    require_contains(
        action, "if: ${{ inputs.bootstrap == 'repository' }}",
        "self-bootstrap must not fetch a different tooling revision"
    );
    require_not_contains(
        action, "sparse-checkout",
        "consumer layout cannot assume a parent monorepo"
    );
    require_contains(
        action, "repository: nlohmann/json\n        ref: v3.12.0",
        "clean Ubuntu runners must obtain the declared JSON version"
    );
    require_contains(
        action, "CMAKE_PREFIX_PATH=$prefix",
        "bootstrapped prerequisites must reach local child builds"
    );
    const auto prepare = github_shell_program(action, "Set ecosystem paths", 6);
    const auto build = github_shell_program(action, "Build Marx and Engels", 6);
    temp_dir fixture;
    const auto repository = fixture.path() / "provider";
    const auto source = repository / "nested/tool source";
    const auto git = [&](const ecosystem::string_list& args) {
        auto command = ecosystem::string_list { "git" };
        command.insert(command.end(), args.begin(), args.end());
        const auto result
            = ecosystem::capture_command_result(command, repository);
        require_true(
            result.exit_code == 0,
            "isolated revision fixture failed: " + result.output
        );
    };
    write_text(
        source / "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.20)\nproject(tooling_fixture "
        "LANGUAGES C)\n"
        "add_executable(marx actor.c)\nadd_executable(engels actor.c)\n"
    );
    write_text(source / "manifest.json", "{}\n");
    write_text(source / "templates/version.txt", "runtime data\n");
    write_text(
        source / "actor.c",
        "#include <stdio.h>\nint main(void) { puts(\"old-revision\"); return "
        "0; }\n"
    );
    git({ "init", "-q" });
    git({ "add", "." });
    git({ "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
          "commit", "-qm", "old" });
    git({ "tag", "old" });
    write_text(
        source / "actor.c",
        "#include <stdio.h>\nint main(void) { puts(\"reviewed-revision\"); "
        "return 0; }\n"
    );
    git({ "add", "." });
    git({ "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
          "commit", "-qm", "reviewed" });
    git({ "tag", "reviewed" });

    for (const auto* mode : { "checkout", "repository" }) {
        temp_dir workspace;
        const auto clone_root = std::string(mode) == "checkout"
            ? workspace.path() / "review"
            : workspace.path() / ".ecosystem/tooling/source";
        const auto clone = ecosystem::capture_command_result(
            { "git", "clone", "-q", repository.string(), clone_root.string() }
        );
        require_true(
            clone.exit_code == 0, "fixture must clone without remote access"
        );
        const auto selected_ref
            = std::string(mode) == "checkout" ? "reviewed" : "old";
        require_true(
            ecosystem::capture_command_result(
                { "git", "checkout", "-q", selected_ref }, clone_root
            )
                    .exit_code
                == 0,
            "fixture checkout must select the requested revision"
        );
        const auto project
            = std::string(mode) == "checkout" ? clone_root : workspace.path();
        const auto outputs = project / "outputs";
        const auto paths_script = project / "paths.sh";
        write_text(paths_script, prepare);
        auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", paths_script.string() }, project,
            { { "GITHUB_WORKSPACE", project.string() },
              { "GITHUB_OUTPUT", outputs.string() },
              { "BOOTSTRAP", mode },
              { "SOURCE_PATH", "nested/tool source" },
              { "TOOL_REPOSITORY",
                std::string(mode) == "checkout" ? "" : "fixture/tooling" },
              { "TOOL_REF",
                std::string(mode) == "checkout" ? "" : selected_ref },
              { "BUILD_PARALLELISM", "2" } }
        );
        require_true(
            result.exit_code == 0,
            "explicit bootstrap must prepare paths: " + result.output
        );
        std::map<std::string, std::string> values;
        std::istringstream lines(read_text(outputs));
        std::string line;
        while (std::getline(lines, line)) {
            const auto split = line.find('=');
            values[line.substr(0, split)] = line.substr(split + 1);
        }
        const auto build_script = project / "build.sh";
        write_text(build_script, build);
        const auto build_env
            = std::vector<std::pair<std::string, std::string>> {
                  { "CHECKOUT_ROOT", values.at("manifesto-checkout-root") },
                  { "SOURCE_ROOT", values.at("manifesto-source-root") },
                  { "BUILD_ROOT", values.at("manifesto-build-root") },
                  { "BUILD_PARALLELISM", "2" }
              };
        result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", build_script.string() }, project,
            build_env
        );
        require_true(
            result.exit_code == 0,
            "rendered bootstrap must build both actors: " + result.output
        );
        for (const auto* actor : { "marx-binary", "engels-binary" }) {
            result = ecosystem::capture_command_result(
                { values.at(actor) }, project
            );
            require_true(
                result.exit_code == 0, "advertised actor must execute"
            );
            require_contains(
                result.output,
                std::string(mode) == "checkout" ? "reviewed-revision"
                                                : "old-revision",
                "bootstrap must build exactly the reviewed or explicitly "
                "selected revision"
            );
        }
        fs::remove_all(clone_root / "nested/tool source/templates");
        result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", build_script.string() }, project,
            build_env
        );
        require_true(
            result.exit_code != 0,
            "incompatible source revisions without runtime templates must fail"
        );
        write_text(
            clone_root / "nested/tool source/templates/version.txt",
            "runtime data\n"
        );
        write_text(
            clone_root / "nested/tool source/CMakeLists.txt",
            "cmake_minimum_required(VERSION 3.20)\nproject(incompatible "
            "LANGUAGES NONE)\n"
        );
        result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", build_script.string() }, project,
            build_env
        );
        require_true(
            result.exit_code != 0,
            "stale actors from a previous build cannot satisfy an incompatible "
            "revision"
        );
        require_contains(
            result.output, "must build both marx and engels",
            "incompatible revisions must identify the required binary layout"
        );
    }
}

void test_ci_bootstrap_rejects_missing_inputs_and_escaping_layouts() {
    temp_dir root;
    const auto action = ecosystem::render_required_text_template(
        "tracked/.github/actions/setup-manifesto/action.yml.tpl",
        { { "checkout_action", "actions/checkout@v4" },
          { "install_qt_action", "vendor/qt@pin" } }
    );
    const auto paths = root.path() / "paths.sh";
    write_text(paths, github_shell_program(action, "Set ecosystem paths", 6));
    const auto clean = std::vector<std::pair<std::string, std::string>> {
        { "GITHUB_WORKSPACE", root.path().string() },
        { "GITHUB_OUTPUT", (root.path() / "outputs").string() },
        { "BOOTSTRAP", "repository" },
        { "SOURCE_PATH", "." },
        { "TOOL_REPOSITORY", "fixture/tooling" },
        { "TOOL_REF", "selected-ref" },
        { "BUILD_PARALLELISM", "2" }
    };
    for (const auto& [name, value] :
         std::vector<std::pair<std::string, std::string>> {
             { "BOOTSTRAP", "unknown" },
             { "BOOTSTRAP", "checkout" },
             { "SOURCE_PATH", "../outside" },
             { "SOURCE_PATH", "/absolute" },
             { "SOURCE_PATH", "" },
             { "SOURCE_PATH", "line\nbreak" },
             { "TOOL_REPOSITORY", "" },
             { "TOOL_REF", "" },
             { "BUILD_PARALLELISM", "0" },
             { "BUILD_PARALLELISM", "2; false" } }) {
        auto env = clean;
        for (auto& item : env)
            if (item.first == name)
                item.second = value;
        const auto result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", paths.string() }, root.path(), env
        );
        require_true(
            result.exit_code != 0,
            "invalid or incomplete bootstrap must fail before checkout/build: "
                + name
        );
    }
    const auto checkout = root.path() / "checkout";
    const auto outside = root.path() / "outside";
    fs::create_directories(checkout);
    fs::create_directories(outside);
    fs::create_directory_symlink(outside, checkout / "tooling");
    const auto build = root.path() / "build.sh";
    write_text(build, github_shell_program(action, "Build Marx and Engels", 6));
    const auto result = ecosystem::capture_command_result(
        { "bash", "-e", "-o", "pipefail", build.string() }, root.path(),
        { { "CHECKOUT_ROOT", checkout.string() },
          { "SOURCE_ROOT", (checkout / "tooling").string() },
          { "BUILD_ROOT", (root.path() / "build").string() },
          { "BUILD_PARALLELISM", "2" } }
    );
    require_true(
        result.exit_code != 0 && !fs::exists(root.path() / "build"),
        "symlinked source layouts cannot escape the selected revision"
    );
}

void test_mutate_add_module_updates_manifest_and_files() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    const ecosystem::mutation_report report = ecosystem::add_module(
        root.path(), &manifest_value, "sample", "sample/core"
    );
    require_true(report.errors.empty(), "add_module must succeed");
    require_true(
        report.changed_manifest, "add_module must update the manifest"
    );
    require_true(
        fs::exists(root.path() / "include/sample/core.hpp"),
        "header must be scaffolded"
    );
    require_true(
        fs::exists(root.path() / "src/sample/core.cpp"),
        "source must be scaffolded"
    );
}

void test_installed_actors_use_their_template_bundle() {
    temp_dir root;
    const auto install = root.path() / "install";
    require_true(
        ecosystem::run_command(
            { "cmake", "--install", test_cli_build_dir().string(), "--prefix",
              install.string() },
            root.path()
        ) == 0,
        "tooling install must include both actors and runtime templates"
    );
    const auto prefix = root.path() / "relocated";
    fs::rename(install, prefix);
    const auto bundle = prefix / "share/manifesto/templates";
    require_true(
        fs::is_regular_file(bundle / "tracked/.github/workflows/tests.yml.tpl"),
        "install must include hidden tracked template directories"
    );
    const auto surface = bundle / "cmake/surface_prefix.tpl";
    write_text(surface, "# installed bundle\n" + read_text(surface));
    write_text(
        bundle / "mutation/header.hpp.tpl",
        "// installed header\n#pragma once\n"
    );
    const auto project = root.path() / "project";
    write_sample_build_project(project, "sample");
    write_text(
        root.path() / "templates/cmake/surface_prefix.tpl",
        "{{wrong_workspace_version}}\n"
    );
    fs::create_directories(root.path() / "launchers");
    const auto marx = root.path() / "launchers/marx";
    fs::create_symlink(prefix / "bin/marx", marx);
    scoped_env no_override("MANIFESTO_TEMPLATE_ROOT", "");
    scoped_env no_legacy_override("ECOSYSTEM_TEMPLATE_ROOT", "");
    auto run = [&](const fs::path& binary, const std::string& args) {
        const auto result = run_cli_with_binary(binary, project, args);
        require_true(
            result.exit_code == 0,
            "installed command must succeed:\n" + result.output
        );
    };
    run(marx, "sync");
    require_contains(
        read_text(project / "CMakeLists.txt"), "# installed bundle",
        "installed actors must select their relocated bundle through a symlink "
        "launcher"
    );
    run(prefix / "bin/engels", "check repo");
    run(marx, "mutate add module sample:app helper");
    require_contains(
        read_text(project / "include/helper.hpp"), "installed header",
        "installed mutation must use the same bundle"
    );
    const auto overrides = root.path() / "overrides";
    write_text(
        overrides / "cmake/surface_prefix.tpl",
        "# explicit override\n" + read_text(surface)
    );
    {
        scoped_env override("MANIFESTO_TEMPLATE_ROOT", overrides.string());
        run(marx, "sync");
        require_contains(
            read_text(project / "CMakeLists.txt"), "# explicit override",
            "deliberate partial overrides must precede installed data"
        );
    }
    const auto previous = read_text(project / "CMakeLists.txt");
    fs::rename(surface, bundle / "cmake/surface_prefix.saved");
    auto failed = run_cli_with_binary(marx, project, "sync");
    require_true(
        failed.exit_code != 0,
        "missing bundle members must fail instead of borrowing source templates"
    );
    require_contains(
        failed.output, "cmake/surface_prefix.tpl",
        "missing template diagnostics must name the member"
    );
    require_contains(
        failed.output, bundle.string(),
        "missing template diagnostics must identify the selected bundle"
    );
    require_true(
        read_text(project / "CMakeLists.txt") == previous,
        "failed rendering must preserve tracked state"
    );
    fs::rename(bundle, prefix / "missing-bundle");
    failed = run_cli_with_binary(marx, project, "sync");
    require_true(
        failed.exit_code != 0,
        "a missing installation must not use an incidental checkout"
    );
    {
        scoped_env override(
            "MANIFESTO_TEMPLATE_ROOT",
            (fs::path(ECOS_TEST_SOURCE_DIR) / "templates").string()
        );
        run(marx, "sync");
    }
}

void write_fake_doxygen_tool(const fs::path& path) {
    write_executable_script(
        path,
        "#!/bin/sh\n"
        "if [ \"$1\" = \"--version\" ]; then echo 1.0; exit 0; fi\n"
        "printf '%s\\n' \"$1\" >> \"$DOXYGEN_TEST_LOG\"\n"
        "directory=${1%/*}\n"
        "case \"$DOXYGEN_TEST_MODE\" in\n"
        "  fail) echo 'fixture: unable to execute graph renderer' >&2; exit "
        "7;;\n"
        "  empty) exit 0;;\n"
        "esac\n"
        "mkdir -p \"$directory/html\"\n"
        "printf '<html>fresh documentation</html>\\n' > "
        "\"$directory/html/index.html\"\n"
        "echo 'input.hpp:3: warning: fixture documentation warning' > "
        "\"$directory/warnings.log\"\n"
        "echo 'fixture documentation generated'\n"
    );
}

void write_doxygen_project(const fs::path& root, const std::string& id) {
    write_text(
        root / "manifest.json",
        json(
            { { "id", id },
              { "description", "Doxygen ownership fixture" },
              { "version", "2.3.4" },
              { "facade", "lib:first" },
              { "artifacts",
                json::array(
                    { { { "id", "lib:first" },
                        { "kind", "static_lib" },
                        { "root", "first" },
                        { "owns",
                          json::array({ "model", "include/extra.hpp" }) } },
                      { { "id", "lib:second" },
                        { "kind", "static_lib" },
                        { "root", "second" },
                        { "owns", json::array({ "api" }) } } }
                ) } }
        ).dump(2)
    );
    write_text(
        root / "first/include/model.hpp",
        "#pragma once\n/// Selected public type.\nstruct selected_public_type "
        "{};\n"
    );
    write_text(
        root / "first/src/model.cpp",
        "#include \"model.hpp\"\nint selected_implementation() { return 1; }\n"
    );
    write_text(
        root / "first/include/model.tpp",
        "template <class T> T selected_template(T value) { return value; }\n"
    );
    write_text(
        root / "first/tests/model_tests.cpp",
        "int selected_test() { return 1; }\n"
    );
    write_text(
        root / "first/benchmarks/model_benchmarks.cpp",
        "int selected_benchmark() { return 1; }\n"
    );
    write_text(
        root / "first/include/extra.hpp", "struct quoted_input_type {};\n"
    );
    write_text(
        root / "second/include/api.hpp",
        "#pragma once\nstruct unselected_public_type {};\n"
    );
    write_text(root / "include/stray.hpp", "struct stray_public_type {};\n");
    write_text(root / "docs/stray.hpp", "struct stray_docs_type {};\n");
    write_text(root / "README.md", "# Project overview\n");
}

void test_doxygen_configuration_uses_exact_ownership_and_service_state() {
    temp_dir root;
    const auto project = root.path() / "project with # spaces";
    write_doxygen_project(project, "docs_sample");
    const auto loaded = ecosystem::load_manifest(project / "manifest.json");
    require_true(
        loaded.value.has_value() && loaded.errors.empty(),
        "Doxygen fixture must load: " + join_lines(loaded.errors)
    );
    const ecosystem::artifact_ref selected { "lib", "first" };
    const auto directory = ecosystem::local_doxygen_dir(project, selected);
    const auto config = directory / "Doxyfile";
    std::string error;
    require_true(
        ecosystem::write_local_doxygen_config(
            project, *loaded.value, selected, &error
        ),
        "selected configuration must render: " + error
    );
    const auto contents = read_text(config);
    require_contains(
        contents, "PROJECT_NAME           = \"docs_sample\"",
        "identity must come from the manifest"
    );
    require_contains(
        contents, "PROJECT_NUMBER         = \"2.3.4\"",
        "version must come from the manifest"
    );
    require_contains(
        contents,
        "OUTPUT_DIRECTORY       = \".ecosystem/doxygen/artifacts/lib/first\"",
        "artifact output must be isolated"
    );
    for (const std::string input :
         { "first/include/model.hpp", "first/src/model.cpp",
           "first/include/model.tpp", "first/tests/model_tests.cpp",
           "first/benchmarks/model_benchmarks.cpp", "first/include/extra.hpp" })
        require_contains(
            contents, "\"" + input + "\"",
            "owned input must be listed explicitly: " + input
        );
    for (const std::string excluded :
         { "second/", "stray.hpp", "README.md", "docs/" })
        require_not_contains(
            contents, excluded,
            "artifact configuration must exclude unrelated inputs"
        );
    require_contains(
        contents, "RECURSIVE              = NO",
        "Doxygen must not broaden explicit ownership by walking directories"
    );
    require_contains(
        contents, "CLANG_DATABASE_PATH    = \"\"",
        "missing compile state must not invent an obsolete database path"
    );
    require_true(
        !fs::exists(project / "Doxyfile"),
        "configuration must stay inside service state"
    );

    write_text(
        ecosystem::local_build_dir(project, "debug") / "compile_commands.json",
        "[]\n"
    );
    require_true(
        ecosystem::write_local_doxygen_config(
            project, *loaded.value, std::nullopt, &error
        ),
        "project configuration must render"
    );
    const auto full
        = read_text(ecosystem::local_doxygen_dir(project) / "Doxyfile");
    require_contains(
        full, "\"second/include/api.hpp\"",
        "project documentation must include all owned artifacts"
    );
    require_contains(
        full, "\"README.md\"",
        "project-wide documentation may include its root overview"
    );
    require_contains(
        full,
        "CLANG_DATABASE_PATH    = "
        "\".ecosystem/build/project/desktop/debug/default\"",
        "optional Clang configuration must use the current build layout"
    );
    require_true(
        read_text(config) == contents,
        "project generation must not replace artifact configuration"
    );
    fs::remove(project / "first/include/model.hpp");
    require_true(
        !ecosystem::write_local_doxygen_config(
            project, *loaded.value, selected, &error
        ) && read_text(config) == contents,
        "missing owned inputs must fail before replacing configuration"
    );
    require_contains(
        error, "first/include/model.hpp",
        "input failure must identify the selected path"
    );
}

void test_cli_doxygen_generates_native_scoped_documentation() {
    temp_dir root;
    const auto project = root.path() / "project with # spaces";
    write_doxygen_project(project, "docs_native");
    write_text(project / "Doxyfile", "# unrelated root configuration\n");
    auto result = run_engels_cli(project, "check doxy lib:first");
    require_true(
        result.exit_code == 0,
        "native Doxygen must document selected ownership:\n" + result.output
    );
    const auto directory = ecosystem::local_doxygen_dir(
        project, ecosystem::artifact_ref { "lib", "first" }
    );
    require_true(
        fs::is_regular_file(directory / "html/index.html"),
        "native Doxygen must produce its advertised HTML entry"
    );
    std::string html;
    for (const auto& entry :
         fs::recursive_directory_iterator(directory / "html"))
        if (entry.is_regular_file() && entry.path().extension() == ".html")
            html += read_text(entry.path());
    require_contains(
        html, "selected_public_type",
        "owned public declarations must reach the native result"
    );
    require_contains(
        html, "quoted_input_type",
        "owned headers must be documented when the project path contains "
        "spaces and hashes"
    );
    require_not_contains(
        html, "unselected_public_type",
        "a shared component namespace must not widen documentation"
    );
    require_not_contains(
        html, "stray_public_type",
        "undeclared root source directories must not be scanned"
    );
    require_true(
        read_text(project / "Doxyfile") == "# unrelated root configuration\n",
        "the root Doxyfile must remain untouched"
    );
    const auto before = read_text(directory / "Doxyfile");
    result = run_engels_cli(project, "check doxy lib:absent");
    require_true(
        result.exit_code == 2 && read_text(directory / "Doxyfile") == before,
        "unknown artifacts must fail before changing service state"
    );
}

void test_cli_doxy_propagates_native_graphviz_errors_with_zero_tool_exit() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_doxygen_project(project, "docs_graph_failure");
    write_text(
        project / "first/include/model.hpp",
        "struct base {};\nstruct derived : base {};\n"
    );
    const auto bin = root.path() / "bin";
    write_executable_script(
        bin / "dot",
        "#!/bin/sh\n"
        "if [ \"$1\" = \"-V\" ]; then echo 'dot fixture'; exit 0; fi\n"
        "echo 'native_graph_fixture_failed' >&2\nexit 7\n"
    );
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    const auto result = run_engels_cli(project, "check doxy lib:first");
    const auto directory = ecosystem::local_doxygen_dir(
        project, ecosystem::artifact_ref { "lib", "first" }
    );
    require_true(
        result.exit_code == 5
            && fs::is_regular_file(directory / "html/index.html"),
        "native graph failures must fail even when Doxygen has produced an "
        "HTML index"
    );
    require_contains(
        result.output, "doxygen reported errors",
        "zero-exit native errors must remain operation failures"
    );
    require_contains(
        result.output, "Problems running dot",
        "terminal must display the error from Doxygen's warning log"
    );
    require_contains(
        read_text(directory / "warnings.log"), "exit code=7",
        "native graph process evidence must be retained"
    );
    require_contains(
        read_text(directory / "doxygen.log"), "native_graph_fixture_failed",
        "graph stderr must be retained with tool output"
    );
}

void test_cli_doxy_retains_tool_failures_and_requires_fresh_output() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_sample_build_project(project, "docs_failures");
    const auto bin = root.path() / "bin";
    fs::create_directories(bin);
    const auto calls = root.path() / "calls.log";
    scoped_env log("DOXYGEN_TEST_LOG", calls.string());
    const auto directory = ecosystem::local_doxygen_dir(project);
    {
        scoped_env path("PATH", bin.string());
        auto result = run_engels_cli(project, "check doxy");
        require_true(
            result.exit_code == 4 && !fs::exists(directory),
            "missing Doxygen must fail before writing config"
        );
        require_contains(
            result.output, "doxygen is not available",
            "missing tooling must name Doxygen"
        );
        write_fake_doxygen_tool(bin / "doxygen");
        result = run_engels_cli(project, "check doxy");
        require_true(
            result.exit_code == 4 && !fs::exists(directory)
                && !fs::exists(calls),
            "missing Graphviz must not start generation"
        );
        require_contains(
            result.output, "Graphviz dot",
            "shared graph policy must name its missing dependency"
        );
        result = run_engels_cli(project, "doctor doxy");
        require_true(
            result.exit_code == 4,
            "doctor must use the same required tool inventory"
        );
        require_contains(
            result.output, "Graphviz dot: missing",
            "doctor must expose the missing graph renderer"
        );
    }
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    {
        scoped_env mode("DOXYGEN_TEST_MODE", "fail");
        const auto result = run_engels_cli(project, "check doxy");
        require_true(
            result.exit_code == 5,
            "nonzero Doxygen status must fail the operation"
        );
        require_contains(
            result.output, "exit 7", "failure must retain the tool exit status"
        );
        require_contains(
            result.output, ".ecosystem/doxygen/Doxyfile",
            "failure must identify the exact config"
        );
        require_contains(
            result.output, "unable to execute graph renderer",
            "failure must display the native diagnostic"
        );
        require_contains(
            read_text(directory / "doxygen.log"),
            "unable to execute graph renderer",
            "tool output must persist on failure"
        );
    }
    write_text(directory / "html/index.html", "stale success\n");
    write_text(directory / "html/removed_owner.html", "stale owner\n");
    {
        scoped_env mode("DOXYGEN_TEST_MODE", "empty");
        const auto result = run_engels_cli(project, "check doxy");
        require_true(
            result.exit_code == 5 && !fs::exists(directory / "html/index.html"),
            "zero exit with no fresh HTML must not accept an old result"
        );
        require_contains(
            result.output, "no HTML index", "incomplete output must be explicit"
        );
    }
    const auto result = run_engels_cli(project, "check doxy");
    require_true(
        result.exit_code == 0
            && fs::is_regular_file(directory / "html/index.html")
            && !fs::exists(directory / "html/removed_owner.html"),
        "successful generation must replace stale output within this scope"
    );
    require_contains(
        result.output,
        "doxygen warnings:", "successful runs must locate retained warnings"
    );
    require_contains(
        read_text(directory / "warnings.log"), "fixture documentation warning",
        "warnings must remain inspectable without changing their current "
        "advisory policy"
    );
    require_contains(
        read_text(directory / "doxygen.log"), "fixture documentation generated",
        "successful tool output must persist too"
    );
}

void test_cli_doxy_rejects_service_output_aliases_before_writing() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_sample_build_project(project, "docs_aliases");
    const auto bin = root.path() / "bin";
    write_fake_doxygen_tool(bin / "doxygen");
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    const auto calls = root.path() / "calls.log";
    scoped_env log("DOXYGEN_TEST_LOG", calls.string());
    const auto directory = ecosystem::local_doxygen_dir(project);
    const auto config = directory / "Doxyfile";
    write_text(config, "stale config\n");
    write_text(directory / "html/index.html", "previous output\n");
    const auto authored = project / "authored.txt";
    const auto outside = root.path() / "outside.txt";
    write_text(authored, "authored content\n");
    write_text(outside, "outside content\n");
    for (const auto& name :
         { "doxygen.log", "warnings.log", "html/alias.html" }) {
        fs::create_symlink(
            name == std::string("warnings.log") ? outside : authored,
            directory / name
        );
        const auto result = run_engels_cli(project, "check doxy");
        require_true(
            result.exit_code == 5 && !fs::exists(calls)
                && read_text(config) == "stale config\n"
                && read_text(directory / "html/index.html")
                    == "previous output\n",
            "output aliases must fail before config replacement, cleanup or "
            "execution"
        );
        require_true(
            read_text(authored) == "authored content\n"
                && read_text(outside) == "outside content\n",
            "service writes must not reach aliased authored or external files"
        );
        fs::remove(directory / name);
    }
    fs::rename(config, directory / "saved-config");
    fs::create_symlink(authored, config);
    auto result = run_engels_cli(project, "check doxy");
    require_true(
        result.exit_code == 5 && read_text(authored) == "authored content\n",
        "a config symlink inside the project must also be rejected"
    );
    fs::remove(config);
    fs::rename(directory / "saved-config", config);
    fs::create_directory(directory / "doxygen.log");
    result = run_engels_cli(project, "check doxy");
    require_true(
        result.exit_code == 5 && !fs::exists(calls)
            && read_text(config) == "stale config\n",
        "an unwritable log shape must fail before generation"
    );
    require_contains(
        result.output, "doxygen.log",
        "output failures must identify the failing path"
    );
    fs::remove(directory / "doxygen.log");
    const auto saved = project / ".ecosystem/doxygen-saved";
    fs::rename(directory, saved);
    fs::create_directory_symlink(saved, directory);
    result = run_engels_cli(project, "check doxy");
    require_true(
        result.exit_code == 5 && !fs::exists(calls)
            && read_text(saved / "Doxyfile") == "stale config\n",
        "service directory aliases must not redirect configuration writes"
    );
}

void test_cli_workspace_doxy_keeps_selected_artifact_outputs_separate() {
    temp_dir root;
    write_doxygen_project(root.path() / "alpha", "alpha");
    write_doxygen_project(root.path() / "beta", "beta");
    write_text(root.path() / "broken/manifest.json", "invalid JSON\n");
    const auto bin = root.path() / "bin";
    write_fake_doxygen_tool(bin / "doxygen");
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    const auto calls = root.path() / "calls.log";
    scoped_env log("DOXYGEN_TEST_LOG", calls.string());
    auto result = run_engels_cli(root.path(), "check doxy");
    require_true(
        result.exit_code == 2 && !fs::exists(calls),
        "workspace validity must be checked before any documentation run"
    );
    result = run_engels_cli(
        root.path(),
        "check doxy alpha/lib:first alpha/lib:second beta/lib:first"
    );
    require_true(
        result.exit_code == 0,
        "multiple qualified Doxygen requests must succeed:\n" + result.output
    );
    for (const std::string project : { "alpha", "beta" }) {
        const auto first = ecosystem::local_doxygen_dir(
            root.path() / project, ecosystem::artifact_ref { "lib", "first" }
        );
        require_true(
            fs::is_regular_file(first / "html/index.html"),
            "every selected project must retain its documentation"
        );
        require_not_contains(
            read_text(first / "Doxyfile"), "second/include",
            "artifact config must not widen to its namespace"
        );
    }
    const auto second = ecosystem::local_doxygen_dir(
        root.path() / "alpha", ecosystem::artifact_ref { "lib", "second" }
    );
    require_true(
        fs::is_regular_file(second / "html/index.html"),
        "a second owner in the same project must retain its own HTML"
    );
    require_not_contains(
        read_text(second / "Doxyfile"), "first/include",
        "peer configuration must not be overwritten"
    );
    write_text(
        root.path() / "manifesto.workspace.json",
        "{\"groups\":{\"docs\":[\"alpha\",\"beta\"]}}\n"
    );
    fs::create_directory(
        ecosystem::local_doxygen_dir(root.path() / "beta") / "doxygen.log"
    );
    result = run_engels_cli(root.path(), "check doxy --group docs");
    require_true(
        result.exit_code == 5
            && fs::is_regular_file(
                ecosystem::local_doxygen_dir(root.path() / "alpha")
                / "html/index.html"
            ),
        "workspace failure must retain successful peer output and fail the "
        "aggregate"
    );
    require_contains(
        result.output,
        "== beta (beta) ==", "workspace errors must retain project attribution"
    );
    require_true(
        fs::is_regular_file(second / "html/index.html"),
        "project-wide generation must not erase existing artifact results"
    );
}

void test_cli_check_doxy_rejects_failed_configuration() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_sample_build_project(project, "sample");
    const auto bin = root.path() / "bin";
    const auto log = root.path() / "doxygen.log";
    write_fake_doxygen_tool(bin / "doxygen");
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    scoped_env tool_log("DOXYGEN_TEST_LOG", log.string());
    const auto overrides = root.path() / "templates";
    scoped_env templates("MANIFESTO_TEMPLATE_ROOT", overrides.string());
    const auto config = ecosystem::local_doxygen_dir(project) / "Doxyfile";
    write_text(project / "Doxyfile", "# root config must remain untouched\n");
    write_text(config, "# stale configuration\n");
    write_text(overrides / "tooling/Doxyfile.tpl", "{{missing_binding}}\n");
    auto require_failed = [&]() {
        const auto result = run_engels_cli(project, "check doxy");
        require_true(result.exit_code != 0, "invalid Doxygen config must fail");
        require_contains(
            result.output, "error[task_failed]", "failure must reach the CLI"
        );
        require_contains(
            result.output, "Doxyfile", "failure must name its config"
        );
        require_true(
            !fs::exists(log), "Doxygen must not run with invalid config"
        );
        return result;
    };
    const auto malformed = require_failed();
    require_contains(
        malformed.output, "tooling/Doxyfile.tpl",
        "render failure must name its template"
    );
    require_true(
        read_text(config) == "# stale configuration\n",
        "render failure must preserve existing config"
    );

    write_text(
        overrides / "tooling/Doxyfile.tpl", "PROJECT_NAME = {{project_name}}\n"
    );
    fs::rename(config, project / "Doxyfile.saved");
    fs::create_directory(config);
    require_failed();
    fs::rename(config, project / "Doxyfile.directory");
    const auto outside = root.path() / "outside-config";
    write_text(outside, "# outside project\n");
    fs::create_symlink(outside, config);
    require_failed();
    require_true(
        read_text(outside) == "# outside project\n",
        "config generation must not follow escaping symlinks"
    );

    fs::rename(config, project / "Doxyfile.symlink");
    const auto valid = run_engels_cli(project, "check doxy");
    require_true(
        valid.exit_code == 0,
        "valid Doxygen config must still run:\n" + valid.output
    );
    require_contains(
        read_text(config), "PROJECT_NAME = \"sample\"",
        "Doxygen must receive the rendered config"
    );
    require_true(
        read_text(project / "Doxyfile")
            == "# root config must remain untouched\n",
        "service materialization must not alter a root Doxyfile"
    );
    require_contains(
        read_text(log), ".ecosystem/doxygen/Doxyfile",
        "successful generation must invoke Doxygen"
    );
}

void test_template_loader_renders_repo_owned_templates() {
    const fs::path mutation_template
        = ecosystem::locate_template_path({ "mutation/source.cpp.tpl" });
    require_true(
        !mutation_template.empty(),
        "template loader must resolve mutation templates from the repo"
    );

    std::string error_message;
    const std::string source_contents = ecosystem::render_text_template(
        "mutation/source.cpp.tpl", { { "module_path", "sample/core" } },
        &error_message
    );
    require_true(
        error_message.empty(), "template loader must render mutation templates"
    );
    require_contains(
        source_contents, "#include \"sample/core.hpp\"",
        "mutation source template must substitute the module path"
    );

    error_message.clear();
    const std::string sphinx_contents = ecosystem::render_text_template(
        "tooling/sphinx_conf.py.tpl",
        {
            { "project_name", "sample" },
            { "extensions", "    'myst_parser',\n" },
            { "source_suffix_markdown", "    '.md': 'markdown',\n" },
        },
        &error_message
    );
    require_true(
        error_message.empty(), "template loader must render tooling templates"
    );
    require_contains(
        sphinx_contents, "project = \"sample\"",
        "tooling templates must substitute the project name"
    );
    require_contains(
        sphinx_contents, "'myst_parser'",
        "tooling templates must preserve injected extension blocks"
    );
    require_contains(
        sphinx_contents, "sphinx_rtd_theme",
        "tooling templates must keep the default Read the Docs theme"
    );

    const fs::path shared_format_template = ecosystem::locate_template_path(
        { ".clang-format", "tooling/clang-format.tpl" }
    );
    require_true(
        !shared_format_template.empty(),
        "template loader must resolve the shared format template"
    );
    require_contains(
        read_text(shared_format_template), "ColumnLimit:     80",
        "template loader must retain the shipped format defaults "
        "when it is available"
    );

    const fs::path shared_workflow_template = ecosystem::locate_template_path(
        { ".github/workflows/tests.yml",
          "tracked/.github/workflows/tests.yml.tpl" }
    );
    require_true(
        !shared_workflow_template.empty(),
        "template loader must resolve the shared workflow template"
    );
    require_contains(
        shared_workflow_template.generic_string(),
        "/templates/tracked/.github/workflows/tests.yml.tpl",
        "template loader must prefer the executable-owned workflow "
        "template when it is available"
    );

    const fs::path shared_action_template = ecosystem::locate_template_path(
        { ".github/actions/setup-manifesto/action.yml",
          "tracked/.github/actions/setup-manifesto/action.yml.tpl" }
    );
    require_true(
        !shared_action_template.empty(),
        "template loader must resolve the shared GitHub action template"
    );
    require_contains(
        shared_action_template.generic_string(),
        "/templates/tracked/.github/actions/setup-manifesto/action.yml.tpl",
        "template loader must prefer the executable-owned GitHub action "
        "template when it is available"
    );

    const fs::path shared_doxygen_template = ecosystem::locate_template_path(
        { "Doxyfile", "tooling/Doxyfile.tpl" }
    );
    require_true(
        !shared_doxygen_template.empty(),
        "template loader must resolve the shared Doxygen template"
    );
    require_contains(
        shared_doxygen_template.generic_string(),
        "/templates/tooling/Doxyfile.tpl",
        "template loader must prefer the executable-owned Doxygen template"
    );

    error_message.clear();
    const std::string doxygen_contents
        = ecosystem::render_text_template_candidates(
            { "Doxyfile", "tooling/Doxyfile.tpl" },
            { { "project_name", "\"sample\"" },
              { "project_version", "\"2.3.4\"" },
              { "input_files", "\"src/main.cpp\"" },
              { "output_dir", "\".ecosystem/doxygen\"" },
              { "warning_log", "\".ecosystem/doxygen/warnings.log\"" },
              { "clang_database", "\"\"" } },
            &error_message
        );
    require_true(
        error_message.empty(),
        "template loader must render the shared Doxygen template"
    );
    require_contains(
        doxygen_contents, "PROJECT_NAME           = \"sample\"",
        "shared Doxygen template must substitute the project name"
    );
    require_contains(
        doxygen_contents, "OUTPUT_DIRECTORY       = \".ecosystem/doxygen\"",
        "shared Doxygen template must keep the ecosystem-local "
        "output directory"
    );

    const fs::path shared_cmake_template
        = ecosystem::locate_template_path({ "cmake/surface_prefix.tpl" });
    require_true(
        !shared_cmake_template.empty(),
        "template loader must resolve shared CMake surface templates"
    );
    require_true(
        shared_cmake_template.lexically_normal()
            == (fs::path(ECOS_TEST_SOURCE_DIR) / "templates/cmake"
                / "surface_prefix.tpl")
                   .lexically_normal(),
        "template loader must prefer the executable-owned CMake template "
        "when it is available"
    );

    error_message.clear();
    const std::string doctor_probe_contents = ecosystem::render_text_template(
        "cmake/doctor_probe.tpl",
        {
            { "cpp_standard", "20" },
            { "package_surface",
              "find_package(nlohmann_json CONFIG REQUIRED)\n" },
        },
        &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render the shared doctor probe template"
    );
    require_contains(
        doctor_probe_contents, "set(CMAKE_CXX_STANDARD 20)",
        "doctor probe template must substitute the project C++ standard"
    );
    require_contains(
        doctor_probe_contents, "find_package(nlohmann_json CONFIG REQUIRED)",
        "doctor probe template must preserve injected package surface text"
    );

    const fs::path shared_release_template
        = ecosystem::locate_template_path({ "release/debian_control.tpl" });
    require_true(
        !shared_release_template.empty(),
        "template loader must resolve shared release templates"
    );
    require_true(
        shared_release_template.lexically_normal()
            == (fs::path(ECOS_TEST_SOURCE_DIR) / "templates/release"
                / "debian_control.tpl")
                   .lexically_normal(),
        "template loader must prefer the executable-owned release "
        "template when it is available"
    );

    error_message.clear();
    const std::string debian_control = ecosystem::render_text_template(
        "release/debian_control.tpl",
        {
            { "package_name", "sample" },
            { "debian_version", "1.2.3~pre.1" },
            { "architecture", "amd64" },
            { "installed_size_kib", "42" },
            { "packager", "ecosystem prerelease <noreply@local>" },
            { "description", "Sample package" },
        },
        &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render the shared release template"
    );
    require_contains(
        debian_control, "Package: sample",
        "release templates must substitute the package name"
    );
    require_contains(
        debian_control, "Installed-Size: 42",
        "release templates must substitute computed size fields"
    );

    const fs::path shared_benchmark_template
        = ecosystem::locate_template_path({ "benchmark/plot.svg.tpl" });
    require_true(
        !shared_benchmark_template.empty(),
        "template loader must resolve shared benchmark templates"
    );
    require_true(
        shared_benchmark_template.lexically_normal()
            == (fs::path(ECOS_TEST_SOURCE_DIR) / "templates/benchmark"
                / "plot.svg.tpl")
                   .lexically_normal(),
        "template loader must prefer the executable-owned benchmark "
        "template when it is available"
    );

    error_message.clear();
    const std::string benchmark_svg = ecosystem::render_text_template(
        "benchmark/plot.svg.tpl",
        {
            { "svg_width", "960" },
            { "svg_height", "540" },
            { "margin_left", "80.00" },
            { "margin_top", "50.00" },
            { "plot_width", "660.00" },
            { "plot_height", "420.00" },
            { "title", "sample plot" },
            { "x_axis_label_y", "522.00" },
            { "y_axis_label_y", "260.00" },
            { "y_ticks", "  <line />\n" },
            { "x_ticks", "  <text>64</text>\n" },
            { "series_layers", "  <polyline />\n" },
        },
        &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render the shared benchmark template"
    );
    require_contains(
        benchmark_svg, "sample plot",
        "benchmark templates must substitute the plot title"
    );
    require_contains(
        benchmark_svg, "GFLOPs/s",
        "benchmark templates must preserve the throughput axis label"
    );

    const fs::path shared_package_surface_template
        = ecosystem::locate_template_path(
            { "cmake/package_surface/add_interface_library.tpl" }
        );
    require_true(
        !shared_package_surface_template.empty(),
        "template loader must resolve shared package-surface templates"
    );
    require_true(
        shared_package_surface_template.lexically_normal()
            == (fs::path(ECOS_TEST_SOURCE_DIR)
                / "templates/cmake/package_surface/add_interface_library.tpl")
                   .lexically_normal(),
        "template loader must prefer the executable-owned package-surface "
        "template when it is available"
    );

    error_message.clear();
    const std::string package_surface_line = ecosystem::render_text_template(
        "cmake/package_surface/target_link_libraries_single.tpl",
        {
            { "indent", {} },
            { "target_name", "sample_support" },
            { "library", "sample::core" },
        },
        &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render the shared package-surface template"
    );
    require_contains(
        package_surface_line,
        "target_link_libraries(sample_support INTERFACE sample::core)",
        "package-surface templates must substitute target link bindings"
    );

    const fs::path shared_sync_surface_template
        = ecosystem::locate_template_path(
            { "cmake/generated_tests_block.tpl" }
        );
    require_true(
        !shared_sync_surface_template.empty(),
        "template loader must resolve shared sync CMake templates"
    );
    require_true(
        shared_sync_surface_template.lexically_normal()
            == (fs::path(ECOS_TEST_SOURCE_DIR)
                / "templates/cmake/generated_tests_block.tpl")
                   .lexically_normal(),
        "template loader must prefer the executable-owned sync CMake "
        "template when it is available"
    );

    error_message.clear();
    const std::string path_set_block = ecosystem::render_text_template(
        "cmake/path_set_block.tpl",
        {
            { "variable_name", "SAMPLE_HEADERS" },
            { "paths_block",
              "        ${CMAKE_CURRENT_SOURCE_DIR}/include/sample/core.hpp\n" },
        },
        &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render shared path-set templates"
    );
    require_contains(
        path_set_block, "set(SAMPLE_HEADERS",
        "path-set templates must substitute the variable name"
    );
    require_contains(
        path_set_block, "include/sample/core.hpp",
        "path-set templates must preserve injected path entries"
    );

    error_message.clear();
    const std::string install_target_block = ecosystem::render_text_template(
        "cmake/install_target.tpl", { { "target_name", "sample__app" } },
        &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render shared install templates"
    );
    require_contains(
        install_target_block, "install(TARGETS sample__app",
        "install templates must substitute the target name"
    );

    const fs::path shared_artifact_template
        = ecosystem::locate_template_path({ "cmake/artifact/library.tpl" });
    require_true(
        !shared_artifact_template.empty(),
        "template loader must resolve shared artifact CMake templates"
    );
    require_true(
        shared_artifact_template.lexically_normal()
            == (fs::path(ECOS_TEST_SOURCE_DIR)
                / "templates/cmake/artifact/library.tpl")
                   .lexically_normal(),
        "template loader must prefer the executable-owned artifact CMake "
        "template when it is available"
    );

    error_message.clear();
    const std::string artifact_link_block = ecosystem::render_text_template(
        "cmake/artifact/link_libraries.tpl",
        {
            { "target_name", "sample__app" },
            { "link_scope", "PRIVATE" },
            { "link_targets_block", "    sample__core\n    sample::dep\n" },
        },
        &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render shared artifact templates"
    );
    require_contains(
        artifact_link_block, "target_link_libraries(sample__app PRIVATE",
        "artifact templates must substitute the target and link scope"
    );
    require_contains(
        artifact_link_block, "sample::dep",
        "artifact templates must preserve injected link targets"
    );

    const fs::path shared_component_template = ecosystem::locate_template_path(
        { "cmake/component/tests_guard_open.tpl" }
    );
    require_true(
        !shared_component_template.empty(),
        "template loader must resolve shared component CMake templates"
    );
    require_true(
        shared_component_template.lexically_normal()
            == (fs::path(ECOS_TEST_SOURCE_DIR)
                / "templates/cmake/component/tests_guard_open.tpl")
                   .lexically_normal(),
        "template loader must prefer the executable-owned component CMake "
        "template when it is available"
    );

    error_message.clear();
    const std::string gtest_warning_block = ecosystem::render_text_template(
        "cmake/component/gtest_guard_else.tpl",
        { { "component_id", "sample_tests" } }, &error_message
    );
    require_true(
        error_message.empty(),
        "template loader must render shared component templates"
    );
    require_contains(
        gtest_warning_block,
        "GTest not found; skipping test component sample_tests",
        "component templates must substitute the component id in warnings"
    );
}

void test_ensure_local_artifacts_keeps_tracked_style_surfaces_sync_owned() {
    temp_dir root;
    ecosystem::ensure_local_artifacts(root.path(), true, true);

    require_true(
        !fs::exists(root.path() / ".clang-format"),
        "local artifact materialization must not rewrite the tracked "
        "format surface"
    );
    require_true(
        !fs::exists(root.path() / ".clang-tidy"),
        "local artifact materialization must not rewrite the tracked "
        "tidy surface"
    );
    require_true(
        fs::exists(root.path() / ".ecosystem" / "source"),
        "local artifact materialization must still prepare the "
        "ecosystem state root"
    );
}

void test_mutate_add_component_scaffolds_templates() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    ecosystem::mutation_report report
        = ecosystem::add_component(root.path(), &manifest_value, "core");
    require_true(
        report.errors.empty(), "default component template must succeed"
    );
    const ecosystem::component* core
        = find_component_value(manifest_value, "core");
    require_true(
        core != nullptr, "default component template must add a component"
    );
    require_true(
        core->artifacts.size() == 1U,
        "default component template must add one artifact"
    );
    require_true(
        core->artifacts.front().kind == "static_lib",
        "default component template must produce a static library artifact"
    );
    require_true(
        core->artifacts.front().id == "core",
        "default component template must keep the component id as the "
        "library artifact id"
    );
    require_true(
        core->modules == std::vector<std::string> { "core" },
        "default component template must scaffold a default module"
    );
    require_true(
        fs::exists(root.path() / "core/include/core.hpp"),
        "default component header must exist"
    );
    require_true(
        fs::exists(root.path() / "core/src/core.cpp"),
        "default component source must exist"
    );
    require_contains(
        read_text(root.path() / "core/src/core.cpp"), "#include \"core.hpp\"",
        "default component source must include the scaffolded header"
    );

    report = ecosystem::add_component(
        root.path(), &manifest_value, "headers", "interface_lib"
    );
    require_true(
        report.errors.empty(), "interface component template must succeed"
    );
    const ecosystem::component* headers
        = find_component_value(manifest_value, "headers");
    require_true(
        headers != nullptr, "interface component template must add a component"
    );
    require_true(
        headers->artifacts.front().kind == "interface_lib",
        "interface component template must produce an interface artifact"
    );
    require_true(
        headers->artifacts.front().id == "headers",
        "interface component template must keep the component id as the "
        "interface artifact id"
    );
    require_true(
        headers->file_units.size() == 1U
            && headers->file_units.front().id == "headers"
            && headers->file_units.front().kind == "header_only",
        "interface component template must declare a header-only file unit"
    );
    require_true(
        fs::exists(root.path() / "headers/include/headers.hpp"),
        "interface component header must exist"
    );

    report = ecosystem::add_component(
        root.path(), &manifest_value, "widget_shell", "qt_app"
    );
    require_true(
        report.errors.empty(), "Qt app component template must succeed"
    );
    const ecosystem::component* widget_shell
        = find_component_value(manifest_value, "widget_shell");
    require_true(
        widget_shell != nullptr,
        "Qt app component template must add a component"
    );
    require_true(
        widget_shell->artifacts.front().kind == "qt_app",
        "Qt app component template must produce a Qt app artifact"
    );
    require_true(
        widget_shell->artifacts.front().id == "app",
        "Qt app component template must default to an app artifact id"
    );
    require_true(
        widget_shell->stack.at("qt")
            == ecosystem::json::array({ "Core", "Gui", "Widgets" }),
        "Qt app component template must declare the default Qt stack"
    );
    require_true(
        widget_shell->file_units.size() == 1U
            && widget_shell->file_units.front().id == "main"
            && widget_shell->file_units.front().kind == "source_only",
        "Qt app component template must declare a main source file"
    );
    require_contains(
        read_text(root.path() / "widget_shell/src/main.cpp"),
        "#include <QApplication>",
        "Qt app component template must scaffold a QApplication entrypoint"
    );

    report = ecosystem::add_component(
        root.path(), &manifest_value, "suite", "tests"
    );
    require_true(report.errors.empty(), "test component template must succeed");
    const ecosystem::component* suite
        = find_component_value(manifest_value, "suite");
    require_true(
        suite != nullptr, "test component template must add a component"
    );
    require_true(
        suite->tests.at("gtest").get<bool>(),
        "test component template must enable gtest support"
    );
    require_true(
        suite->artifacts.front().id == "tests",
        "test component template must default to a tests artifact id"
    );
    require_true(
        suite->modules.empty() && suite->file_units.size() == 1U
            && suite->file_units.front().id == "tests/test_main",
        "test component template must stay file-unit based"
    );
    require_contains(
        read_text(root.path() / "suite/tests/test_main.cpp"), "RUN_ALL_TESTS()",
        "test component template must scaffold a gtest entrypoint"
    );

    report = ecosystem::add_component(
        root.path(), &manifest_value, "perf", "benchmarks"
    );
    require_true(
        report.errors.empty(), "benchmark component template must succeed"
    );
    const ecosystem::component* perf
        = find_component_value(manifest_value, "perf");
    require_true(
        perf != nullptr, "benchmark component template must add a component"
    );
    require_true(
        perf->benchmarks.at("google_benchmark").get<bool>(),
        "benchmark component template must enable benchmark support"
    );
    require_true(
        perf->artifacts.front().id == "bench",
        "benchmark component template must default to a bench artifact id"
    );
    require_true(
        perf->modules.empty() && perf->file_units.size() == 1U
            && perf->file_units.front().id == "benchmarks/bench_main",
        "benchmark component template must stay file-unit based"
    );
    require_contains(
        read_text(root.path() / "perf/benchmarks/bench_main.cpp"),
        "BENCHMARK_MAIN()",
        "benchmark component template must scaffold a benchmark entrypoint"
    );
}

void test_mutate_add_component_supports_custom_artifact_ids_and_links() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_library_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    ecosystem::add_component_options tool_options;
    tool_options.template_kind = "exe";
    tool_options.artifact_id = "cli";
    tool_options.artifact_links = { ecosystem::artifact_ref { "core", "lib" } };

    ecosystem::mutation_report report = ecosystem::add_component(
        root.path(), &manifest_value, "tool", tool_options
    );
    require_true(
        report.errors.empty(),
        "custom artifact-id component mutation must succeed"
    );
    const ecosystem::component* tool
        = find_component_value(manifest_value, "tool");
    require_true(
        tool != nullptr,
        "custom artifact-id component mutation must add a component"
    );
    require_true(
        tool->artifacts.front().id == "cli",
        "custom artifact ids must be preserved in scaffolded components"
    );
    require_true(
        tool->artifacts.front().link == ecosystem::string_list { "core:lib" },
        "explicit artifact links must be preserved in scaffolded components"
    );
    require_true(
        fs::exists(root.path() / "tool/src/main.cpp"),
        "custom artifact-id component mutation must still scaffold files"
    );

    ecosystem::add_component_options tests_options;
    tests_options.template_kind = "tests";

    report = ecosystem::add_component(
        root.path(), &manifest_value, "suite", tests_options
    );
    require_true(
        report.errors.empty(),
        "test component mutation must infer a default link when unambiguous"
    );
    const ecosystem::component* suite
        = find_component_value(manifest_value, "suite");
    require_true(
        suite != nullptr, "test component mutation must add a test component"
    );
    require_true(
        suite->artifacts.front().id == "tests",
        "test component mutation must keep the default tests artifact id"
    );
    require_true(
        suite->artifacts.front().link == ecosystem::string_list { "core:lib" },
        "test component mutation must default-link to the single library facade"
    );

    ecosystem::add_component_options benchmark_options;
    benchmark_options.template_kind = "benchmarks";

    report = ecosystem::add_component(
        root.path(), &manifest_value, "perf", benchmark_options
    );
    require_true(
        report.errors.empty(),
        "benchmark component mutation must infer "
        "a default link when unambiguous"
    );
    const ecosystem::component* perf
        = find_component_value(manifest_value, "perf");
    require_true(
        perf != nullptr,
        "benchmark component mutation must add a benchmark component"
    );
    require_true(
        perf->artifacts.front().id == "bench",
        "benchmark component mutation must keep the default bench artifact id"
    );
    require_true(
        perf->artifacts.front().link == ecosystem::string_list { "core:lib" },
        "benchmark component mutation must default-link to the single "
        "library facade"
    );
}

void test_mutate_add_component_infers_facade_library_closure() {
    temp_dir root;
    ecosystem::manifest manifest_value
        = sample_facade_library_closure_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    ecosystem::add_component_options options;
    options.template_kind = "tests";
    const ecosystem::mutation_report report = ecosystem::add_component(
        root.path(), &manifest_value, "suite", options
    );
    require_true(
        report.errors.empty(),
        "test component mutation must infer the facade-library closure"
    );

    const ecosystem::component* suite
        = find_component_value(manifest_value, "suite");
    require_true(
        suite != nullptr,
        "facade-library closure inference must add the new component"
    );
    require_true(
        suite->artifacts.front().link
            == ecosystem::string_list { "api:api", "core:core" },
        "default links must include the facade library and its transitive "
        "library dependencies without unrelated spare libraries"
    );
}

void test_mutate_add_component_infers_facade_component_library_closure() {
    temp_dir root;
    ecosystem::manifest manifest_value
        = sample_facade_component_library_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    ecosystem::add_component_options options;
    options.template_kind = "exe";
    const ecosystem::mutation_report report = ecosystem::add_component(
        root.path(), &manifest_value, "tool", options
    );
    require_true(
        report.errors.empty(),
        "runnable component mutation must infer the facade-component "
        "library closure"
    );

    const ecosystem::component* tool
        = find_component_value(manifest_value, "tool");
    require_true(
        tool != nullptr,
        "facade-component closure inference must add the new component"
    );
    require_true(
        tool->artifacts.front().link
            == ecosystem::string_list { "frontend:ui", "backend_core:core" },
        "default links must include facade-component libraries and their "
        "transitive library dependencies"
    );
}

void test_mutate_add_component_infers_direct_runnable_library_links() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_shared_runtime_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    ecosystem::add_component_options options;
    options.template_kind = "benchmarks";
    const ecosystem::mutation_report report = ecosystem::add_component(
        root.path(), &manifest_value, "perf", options
    );
    require_true(
        report.errors.empty(),
        "benchmark component mutation must infer direct runnable-library links"
    );

    const ecosystem::component* perf
        = find_component_value(manifest_value, "perf");
    require_true(
        perf != nullptr,
        "direct runnable-library inference must add the new component"
    );
    require_true(
        perf->artifacts.front().link == ecosystem::string_list { "core:lib" },
        "default links must follow direct runnable-library links and ignore "
        "unrelated spare libraries"
    );
}

void test_mutate_add_file_unit_supports_h_variants() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    ecosystem::mutation_report report = ecosystem::add_file_unit(
        root.path(), &manifest_value, "sample", "sample/detail", "header_only_h"
    );
    require_true(report.errors.empty(), "header_only_h file unit must succeed");
    require_true(
        fs::exists(root.path() / "include/sample/detail.h"),
        "C header must be scaffolded"
    );

    report = ecosystem::add_file_unit(
        root.path(), &manifest_value, "sample", "sample/native_bridge",
        "source_pair_h"
    );
    require_true(report.errors.empty(), "source_pair_h file unit must succeed");
    require_true(
        fs::exists(root.path() / "include/sample/native_bridge.h"),
        "paired .h header must exist"
    );
    require_true(
        fs::exists(root.path() / "src/sample/native_bridge.cpp"),
        "paired .cpp source must exist"
    );
}

void test_cli_mutate_add_component_supports_artifact_id_and_link_options() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_multi_library_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );

    const cli_result result = run_cli(
        root.path(),
        "mutate add component widget_shell --kind qt_app "
        "--artifact-id desktop_app --link support:helper"
    );
    require_true(
        result.exit_code == 0,
        "mutate add component must accept artifact-id and link options"
    );
    require_contains(
        result.output, "updated manifest.json",
        "mutate add component must save the manifest"
    );
    require_contains(
        result.output, "wrote widget_shell/src/main.cpp",
        "mutate add component must scaffold files"
    );

    const ecosystem::manifest_report report
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(
        report.errors.empty(),
        "mutate add component must keep the manifest valid"
    );
    require_true(
        report.value.has_value(),
        "mutate add component must persist the manifest"
    );
    const ecosystem::component* widget_shell
        = find_component_value(*report.value, "widget_shell");
    require_true(
        widget_shell != nullptr,
        "mutate add component must persist the new component"
    );
    require_true(
        widget_shell->artifacts.front().kind == "qt_app",
        "mutate add component --kind qt_app must persist the selected "
        "artifact kind"
    );
    require_true(
        widget_shell->artifacts.front().id == "desktop_app",
        "mutate add component must persist a custom artifact id"
    );
    require_true(
        widget_shell->artifacts.front().link
            == ecosystem::string_list { "support:helper" },
        "mutate add component must persist explicit artifact links "
        "instead of inferred defaults"
    );
    require_true(
        widget_shell->stack.at("qt")
            == ecosystem::json::array({ "Core", "Gui", "Widgets" }),
        "mutate add component --kind qt_app must persist the default Qt stack"
    );
    require_contains(
        read_text(root.path() / "widget_shell/src/main.cpp"),
        "QApplication application",
        "mutate add component --kind qt_app must scaffold the Qt entrypoint"
    );
}

void test_cli_list_artifacts() {
    const cli_result result
        = run_cli(fs::path(ECOS_TEST_SOURCE_DIR), "list artifacts");
    require_true(
        result.exit_code == 0, "manifesto list artifacts must succeed"
    );
    require_not_contains(
        result.output, "manifesto:manifesto",
        "no dispatcher artifact may remain"
    );
    require_contains(
        result.output, "marx:marx : exe",
        "list artifacts must show the marx frontend artifact"
    );
    require_contains(
        result.output, "engels:engels : exe",
        "list artifacts must show the engels frontend artifact"
    );
}

void test_cli_actor_batches_preserve_ownership() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    const cli_result wrong_owner
        = run_marx_cli(root.path(), "sync --then list artifacts");
    require_true(wrong_owner.exit_code != 0, "cross-actor batch must fail");
    require_contains(
        wrong_owner.output, "available via engels",
        "batch must identify the owner"
    );
    require_true(
        !fs::exists(root.path() / "CMakeLists.txt"),
        "batch ownership must be checked before any mutation"
    );
    const cli_result reads = run_engels_cli(
        root.path(), "--quiet list artifacts --then report matrix"
    );
    require_true(
        reads.exit_code == 0 && reads.output.empty(),
        "quiet same-actor batch must work"
    );
    const cli_result writes = run_marx_cli(root.path(), "sync --then sync");
    require_true(
        writes.exit_code == 0 && fs::exists(root.path() / "CMakeLists.txt"),
        "Marx must retain same-actor chaining"
    );
    const cli_result failure
        = run_marx_cli(root.path(), "--quiet build nonsense --then sync");
    require_true(
        failure.exit_code != 0 && !failure.output.empty(),
        "quiet mode must retain failures"
    );
    const fs::path shell_input = root.path() / "shell-input";
    write_text(shell_input, "sync\nqueue\nexit\n");
    const cli_result shell = run_engels_cli(
        root.path(), "--interactive < '" + shell_input.string() + "'"
    );
    require_true(
        shell.exit_code == 0, "explicit queued shell must exit cleanly"
    );
    require_contains(
        shell.output, "available via marx", "shell must preserve ownership"
    );
    require_not_contains(
        shell.output, "queued #", "shell must never enqueue a foreign operation"
    );
    const cli_result bad
        = run_engels_cli(root.path(), "--interactive list artifacts");
    require_true(
        bad.exit_code != 0, "interactive mode must reject queued arguments"
    );
    require_contains(
        bad.output, "interactive mode does not accept queued requests",
        "actor mode error must remain explicit"
    );
}

void test_cli_actor_help_and_queue_validation() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    for (const fs::path& binary :
         { marx_binary_path(), engels_binary_path() }) {
        const auto short_help = run_cli_with_binary(binary, root.path(), "-h");
        const auto long_help
            = run_cli_with_binary(binary, root.path(), "--help");
        const auto welcome = run_cli_with_binary(binary, root.path(), "");
        require_true(
            welcome.exit_code == 0 && welcome.output == long_help.output,
            "a visitor launch without arguments must show help successfully"
        );
        const auto empty_quiet
            = run_cli_with_binary(binary, root.path(), "--quiet");
        require_true(
            empty_quiet.exit_code != 0,
            "request options without a command must still fail"
        );
        require_true(
            short_help.exit_code == 0 && short_help.output == long_help.output,
            "both public actors must retain the short help option"
        );
        const bool marx = binary.filename() == "marx";
        require_contains(
            long_help.output, marx ? "  format [" : "check profiles:",
            "help must expose the canonical daily operations"
        );
        require_true(
            run_cli_with_binary(binary, root.path(), "init").exit_code != 0,
            "init must remain outside the public daily interface"
        );
        const fs::path input = root.path() / "queue-input";
        write_text(
            input,
            std::string(
                marx ? "sync --then list artifacts"
                     : "list artifacts --then sync"
            )
                + "\nlog 1junk\nskip -1\nlog +1\nlog "
                  "184467440737095516160000\nlog 1\nexit\n"
        );
        const auto shell = run_cli_with_binary(
            binary, root.path(), "--interactive < '" + input.string() + "'"
        );
        require_true(
            shell.exit_code == 0, "shell must recover from invalid requests"
        );
        require_not_contains(
            shell.output, "queued #",
            "mixed-actor lines must be rejected before any enqueue"
        );
        require_true(
            !fs::exists(root.path() / "CMakeLists.txt"),
            "mixed-actor lines must not start writes"
        );
        for (const std::string id :
             { "1junk", "-1", "+1", "184467440737095516160000" }) {
            require_contains(
                shell.output, "invalid queue id: " + id,
                "queue identifiers must reject suffixes, signs and overflow"
            );
        }
        require_contains(
            shell.output, "unknown queue item: 1",
            "valid numeric IDs must still reach queue lookup"
        );
    }
}

void test_cli_all_operations_have_one_actor() {
    temp_dir root;
    for (const std::string command : { "list", "check", "doctor", "report" }) {
        const cli_result wrong = run_marx_cli(root.path(), command);
        require_true(
            wrong.exit_code != 0, "Marx must reject every Engels operation"
        );
        require_contains(
            wrong.output, "available via engels",
            "Engels ownership must be explicit"
        );
    }
    for (const std::string command : { "sync", "format", "mutate", "build",
                                       "benchmark", "run", "prerelease" }) {
        const cli_result wrong = run_engels_cli(root.path(), command);
        require_true(
            wrong.exit_code != 0, "Engels must reject every Marx operation"
        );
        require_contains(
            wrong.output, "available via marx",
            "Marx ownership must be explicit"
        );
    }
}

void test_cli_engels_rejects_marx_commands() {
    const cli_result result
        = run_engels_cli(fs::path(ECOS_TEST_SOURCE_DIR), "sync");
    require_true(
        result.exit_code != 0,
        "engels must reject write-capable commands owned by marx"
    );
    require_contains(
        result.output, "command 'sync' is available via marx; run `marx sync`",
        "engels must route sync requests to the marx frontend"
    );
}

void test_cli_marx_rejects_engels_commands() {
    const cli_result result
        = run_marx_cli(fs::path(ECOS_TEST_SOURCE_DIR), "report matrix");
    require_true(
        result.exit_code != 0,
        "marx must reject diagnostic commands owned by engels"
    );
    require_contains(
        result.output,
        "command 'report' is available via engels; run `engels report`",
        "marx must route report requests to the engels frontend"
    );
}

void test_cli_report_matrix() {
    const cli_result result
        = run_cli(fs::path(ECOS_TEST_SOURCE_DIR), "report matrix");
    require_true(result.exit_code == 0, "manifesto report matrix must succeed");
    const json report = json::parse(result.output);
    require_true(
        report.at("project").get<std::string>() == "manifesto",
        "matrix report project id must match"
    );
    require_true(
        report.at("artifacts")
            .at(0)
            .at("build_profiles")
            .at("debug")
            .get<bool>(),
        "debug profile must be supported by the self-hosted project"
    );
}

void test_cli_workspace_list() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    write_text(root.path() / "tooling/readme.md", "Unmanaged tools.\n");
    const cli_result result = run_cli(root.path(), "list");
    require_true(
        result.exit_code == 0, "workspace manifesto list must succeed"
    );
    require_contains(
        result.output,
        "workspace:", "workspace list must identify the workspace root"
    );
    require_contains(
        result.output, "alpha", "workspace list must include alpha"
    );
    require_contains(result.output, "beta", "workspace list must include beta");
    require_not_contains(
        result.output, "tooling",
        "workspace list must ignore non-managed legacy tooling"
    );
}

void test_invalid_workspace_projects_remain_visible() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_text(root.path() / "broken/manifest.json", "{ invalid JSON\n");
    write_text(
        root.path() / "structural/manifest.json", "{\"id\":\"damaged\"}\n"
    );
    write_text(root.path() / "unmanaged/note.txt", "not a project\n");

    const auto workspace = ecosystem::discover_workspace(root.path());
    require_true(
        workspace.has_value() && workspace->projects.size() == 3U,
        "discovery must retain valid and invalid manifests only"
    );
    const auto* broken
        = ecosystem::find_workspace_project(*workspace, "broken");
    const auto* damaged
        = ecosystem::find_workspace_project(*workspace, "damaged");
    require_true(
        broken != nullptr && !broken->valid() && !broken->errors.empty(),
        "malformed JSON must retain path identity and errors"
    );
    require_true(
        damaged != nullptr && !damaged->valid(),
        "structural failures must retain parsed identity"
    );

    for (const std::string target : { "", "projects", "components", "artifacts",
                                      "profiles", "platforms" }) {
        const cli_result result = run_cli(root.path(), "list " + target);
        require_true(
            result.exit_code == 0, "listing invalid state must succeed"
        );
        require_contains(
            result.output, "broken/manifest.json [invalid]",
            "list must expose invalid paths"
        );
        require_contains(
            result.output, "damaged",
            "list must expose parsed invalid identities"
        );
    }
    const cli_result matrix
        = run_cli(root.path(), "report matrix --project damaged");
    require_true(matrix.exit_code == 0, "reporting invalid state must succeed");
    const json project = json::parse(matrix.output).at("projects").at(0);
    require_true(
        !project.at("valid").get<bool>() && !project.at("errors").empty(),
        "canonical workspace matrix must carry validity and errors"
    );
    require_true(
        project.at("manifest_path") == "structural/manifest.json",
        "invalid report must identify its manifest"
    );

    for (const std::string request :
         { "check repo", "check repo --project broken",
           "check repo --project damaged", "sync", "format", "build debug",
           "benchmark", "prerelease", "run debug --project broken",
           "check tests broken/core:core" }) {
        const cli_result result = run_cli(root.path(), request);
        require_true(
            result.exit_code != 0,
            "invalid selected state must fail: " + request
        );
        require_contains(
            result.output, "invalid project:",
            "failure must distinguish invalid from unknown projects"
        );
    }
    require_true(
        !fs::exists(root.path() / "alpha/CMakeLists.txt")
            && !fs::exists(root.path() / "alpha/.ecosystem"),
        "workspace writes must validate all selected projects before side "
        "effects"
    );
    const cli_result unknown = run_cli(root.path(), "sync --project absent");
    require_contains(
        unknown.output, "unknown project: absent",
        "unknown selection must remain distinct"
    );
    const cli_result filtered = run_cli(root.path(), "sync --project alpha");
    require_true(
        filtered.exit_code == 0,
        "a valid explicit scope may exclude invalid siblings"
    );
}

void test_invalid_workspace_group_and_root_manifest() {
    temp_dir root;
    write_text(root.path() / "broken/manifest.json", "[]\n");
    write_text(
        root.path() / "manifesto.workspace.json",
        "{\"groups\":{\"broken_group\":[\"broken\"]}}\n"
    );
    const auto workspace = ecosystem::discover_workspace(root.path());
    require_true(
        workspace.has_value() && workspace->errors.empty(),
        "a group member with an invalid manifest is known"
    );
    const cli_result result = run_cli(root.path(), "sync --group broken_group");
    require_true(result.exit_code != 0, "invalid group must prevent sync");
    require_contains(
        result.output, "invalid project: broken",
        "group failure must attribute its invalid member"
    );
    write_text(root.path() / "manifest.json", "{ broken\n");
    const cli_result project = run_cli(root.path(), "list");
    require_true(
        project.exit_code != 0,
        "a malformed root manifest must not fall through to workspace discovery"
    );
    require_contains(
        project.output, "invalid JSON in manifest",
        "root manifest errors must be preserved"
    );
}

void test_cli_workspace_list_groups() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    write_sample_workspace_project(root.path(), "gamma");
    write_sample_workspace_config(root.path());

    const cli_result result = run_cli(root.path(), "list groups");
    require_true(
        result.exit_code == 0,
        "workspace ecos list groups must succeed when a workspace config exists"
    );
    require_contains(
        result.output, "core : alpha beta",
        "workspace list groups must emit the configured core group"
    );
    require_contains(
        result.output, "apps : gamma",
        "workspace list groups must emit the configured apps group"
    );
}

void test_cli_workspace_group_filter_selects_configured_projects() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    write_sample_workspace_project(root.path(), "gamma");
    write_sample_workspace_config(root.path());

    const cli_result result = run_cli(root.path(), "build debug --group core");
    require_true(
        result.exit_code == 0,
        "workspace ecos build must accept named workspace groups"
    );
    require_contains(
        result.output,
        "== alpha (alpha) ==", "workspace group selection must include alpha"
    );
    require_contains(
        result.output,
        "== beta (beta) ==", "workspace group selection must include beta"
    );
    require_not_contains(
        result.output, "gamma",
        "workspace group selection must exclude projects outside the group"
    );
}

void test_cli_workspace_report_matrix_records_group_selection() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    write_sample_workspace_project(root.path(), "gamma");
    write_sample_workspace_config(root.path());

    const cli_result result
        = run_cli(root.path(), "report matrix --group core");
    require_true(
        result.exit_code == 0,
        "workspace ecos report matrix must accept named workspace groups"
    );

    const json report = json::parse(result.output);
    require_true(
        report.contains("selection"),
        "workspace matrix report must describe a named group selection"
    );
    require_true(
        report.at("selection").at("group").get<std::string>() == "core",
        "workspace matrix selection must retain the requested group name"
    );
    require_true(
        report.at("projects").size() == 2U,
        "workspace matrix group selection must keep only the grouped projects"
    );
}

void test_cli_workspace_group_filter_rejects_unknown_groups() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");

    const cli_result result = run_cli(root.path(), "build debug --group core");
    require_true(
        result.exit_code != 0,
        "workspace group filters must reject unknown group ids"
    );
    require_contains(
        result.output, "unknown workspace group: core",
        "workspace group filter errors must name the missing group"
    );
}

void test_cli_workspace_config_rejects_unknown_group_project_selector() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_text(
        root.path() / "manifesto.workspace.json",
        "{\n"
        "  \"groups\": {\n"
        "    \"broken\": [\"missing\"]\n"
        "  }\n"
        "}\n"
    );

    const cli_result result = run_cli(root.path(), "list");
    require_true(
        result.exit_code != 0,
        "invalid workspace group selectors must reject workspace commands"
    );
    require_contains(
        result.output,
        "manifesto.workspace.json.groups.broken references unknown project "
        "selector: missing",
        "workspace config errors must explain the broken selector"
    );
}

void test_cli_workspace_report_matrix() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    const cli_result result = run_cli(root.path(), "report matrix");
    require_true(result.exit_code == 0, "workspace matrix must succeed");
    const json report = json::parse(result.output);
    require_true(
        report.at("projects").size() == 2U,
        "workspace matrix must include exactly the fixture projects"
    );
    std::set<std::string> identities;
    for (const auto& project : report.at("projects"))
        identities.insert(project.at("project").get<std::string>());
    require_true(
        identities == std::set<std::string> { "alpha", "beta" },
        "workspace matrix must preserve fixture identities"
    );
}

void test_cli_workspace_build_on_sample_workspace() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");

    const cli_result sync_result = run_cli(root.path(), "sync");
    require_true(
        sync_result.exit_code == 0,
        "workspace ecos sync must succeed on sample workspace"
    );

    const cli_result build_result
        = run_cli(root.path(), "build debug --project alpha");
    require_true(
        build_result.exit_code == 0,
        "workspace ecos build debug must succeed with a project filter"
    );
    require_contains(
        build_result.output,
        "== alpha (alpha) ==", "workspace build must identify the project"
    );
    require_contains(
        build_result.output, "built alpha:app",
        "workspace build must build the facade entry artifact"
    );
    require_not_contains(
        build_result.output, "beta",
        "workspace build filter must exclude other projects"
    );
    require_true(
        fs::exists(root.path() / "alpha/.ecosystem/source/CMakeLists.txt"),
        "workspace build must materialize the local developer surface"
    );
    require_contains(
        read_text(root.path() / "alpha/.ecosystem/source/CMakeLists.txt"),
        "get_filename_component(ECOSYSTEM_PROJECT_ROOT "
        "\"${CMAKE_CURRENT_SOURCE_DIR}/../..\" ABSOLUTE)",
        "workspace build must use the generated local developer surface"
    );
    require_not_contains(
        read_text(root.path() / "alpha/CMakeLists.txt"),
        "ECOSYSTEM_BUILD_TESTS",
        "workspace sync must keep the tracked facade slim"
    );
}

void test_cli_workspace_build_supports_multiple_artifact_filters() {
    temp_dir root;
    write_sample_dual_run_workspace_project(root.path(), "alpha");
    write_sample_dual_run_workspace_project(root.path(), "beta");

    const cli_result result
        = run_cli(root.path(), "build debug alpha/app:app beta/tool:cli");
    require_true(
        result.exit_code == 0,
        "workspace ecos build must support multiple qualified artifact filters"
    );
    require_contains(
        result.output, "== alpha (alpha) ==",
        "multi-artifact workspace build must identify alpha"
    );
    require_contains(
        result.output, "built app:app",
        "multi-artifact workspace build must build the selected alpha artifact"
    );
    require_contains(
        result.output,
        "== beta (beta) ==", "multi-artifact workspace build must identify beta"
    );
    require_contains(
        result.output, "built tool:cli",
        "multi-artifact workspace build must build the selected beta artifact"
    );
}

void test_cli_workspace_sync_supports_project_filter() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");

    const cli_result sync_result = run_cli(root.path(), "sync --project alpha");
    require_true(
        sync_result.exit_code == 0,
        "workspace ecos sync must support a project filter"
    );
    require_contains(
        sync_result.output, "alpha: wrote alpha/CMakeLists.txt",
        "workspace sync filter must write alpha"
    );
    require_not_contains(
        sync_result.output,
        "beta:", "workspace sync filter must exclude beta output"
    );
    require_true(
        fs::exists(root.path() / "alpha/CMakeLists.txt"),
        "workspace sync filter must generate alpha facade"
    );
    require_true(
        fs::exists(root.path() / "alpha/.gitignore"),
        "workspace sync filter must generate alpha gitignore"
    );
    require_true(
        !fs::exists(root.path() / "beta/CMakeLists.txt"),
        "workspace sync filter must not generate non-selected project facades"
    );
}

void test_cli_workspace_sync_supports_multi_project_filter() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    write_sample_workspace_project(root.path(), "gamma");

    const cli_result sync_result
        = run_cli(root.path(), "sync --project alpha --project beta");
    require_true(
        sync_result.exit_code == 0,
        "workspace ecos sync must support multiple project filters"
    );
    require_contains(
        sync_result.output, "alpha: wrote alpha/CMakeLists.txt",
        "workspace sync multi-project filter must write alpha"
    );
    require_contains(
        sync_result.output, "beta: wrote beta/CMakeLists.txt",
        "workspace sync multi-project filter must write beta"
    );
    require_not_contains(
        sync_result.output,
        "gamma:", "workspace sync multi-project filter must exclude gamma"
    );
    require_true(
        fs::exists(root.path() / "alpha/CMakeLists.txt"),
        "workspace sync must generate alpha facade"
    );
    require_true(
        fs::exists(root.path() / "beta/CMakeLists.txt"),
        "workspace sync must generate beta facade"
    );
    require_true(
        !fs::exists(root.path() / "gamma/CMakeLists.txt"),
        "workspace sync must not generate gamma facade"
    );
}

void test_cli_workspace_report_matrix_supports_filters() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");

    const cli_result result
        = run_cli(root.path(), "report matrix alpha/alpha:app");
    require_true(
        result.exit_code == 0,
        "workspace ecos report matrix must support qualified artifact filters"
    );

    const json report = json::parse(result.output);
    require_true(
        report.contains("selection"),
        "filtered workspace matrix report must describe its selection"
    );
    require_true(
        report.at("projects").size() == 1U,
        "filtered workspace matrix report must contain one project"
    );
    require_true(
        report.at("selection").at("project").get<std::string>() == "alpha",
        "workspace matrix selection must resolve the target project"
    );
    require_true(
        report.at("selection").at("artifact").get<std::string>() == "alpha:app",
        "workspace matrix selection must retain the requested artifact"
    );
    require_true(
        report.at("projects").at(0).at("project").get<std::string>() == "alpha",
        "workspace matrix filter must keep the selected project"
    );
    require_true(
        report.at("projects").at(0).at("artifacts").size() == 1U,
        "workspace matrix artifact filter must keep one artifact"
    );
    require_true(
        report.at("projects")
                .at(0)
                .at("artifacts")
                .at(0)
                .at("ref")
                .get<std::string>()
            == "alpha:app",
        "workspace matrix artifact filter must keep the requested artifact"
    );
}

void test_cli_workspace_report_matrix_supports_multiple_artifact_filters() {
    temp_dir root;
    write_sample_dual_run_workspace_project(root.path(), "alpha");
    write_sample_dual_run_workspace_project(root.path(), "beta");

    const cli_result result
        = run_cli(root.path(), "report matrix alpha/app:app beta/tool:cli");
    require_true(
        result.exit_code == 0,
        "workspace ecos report matrix must support multiple artifact filters"
    );

    const json report = json::parse(result.output);
    require_true(
        report.contains("selection"),
        "multi-artifact workspace matrix report must describe its selection"
    );
    require_true(
        report.at("selection").contains("artifacts"),
        "multi-artifact workspace matrix selection must list artifacts"
    );
    require_true(
        report.at("selection").at("artifacts").size() == 2U,
        "multi-artifact workspace matrix selection must keep two artifacts"
    );
    require_true(
        report.at("selection")
                .at("artifacts")
                .at(0)
                .at("project")
                .get<std::string>()
            == "alpha",
        "multi-artifact workspace matrix selection must keep the alpha project"
    );
    require_true(
        report.at("selection")
                .at("artifacts")
                .at(1)
                .at("artifact")
                .get<std::string>()
            == "tool:cli",
        "multi-artifact workspace matrix selection must keep the beta tool "
        "artifact"
    );
    require_true(
        report.at("projects").size() == 2U,
        "multi-artifact workspace matrix report must contain both projects"
    );
    require_true(
        report.at("projects").at(0).at("artifacts").size() == 1U,
        "multi-artifact workspace matrix report must filter alpha to one "
        "artifact"
    );
    require_true(
        report.at("projects").at(1).at("artifacts").size() == 1U,
        "multi-artifact workspace matrix report must filter beta to one "
        "artifact"
    );
}

void test_cli_workspace_report_matrix_supports_multi_project_filters() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");
    write_sample_workspace_project(root.path(), "gamma");

    const cli_result result
        = run_cli(root.path(), "report matrix --project alpha --project beta");
    require_true(
        result.exit_code == 0,
        "workspace ecos report matrix must support multiple project filters"
    );

    const json report = json::parse(result.output);
    require_true(
        report.contains("selection"),
        "multi-project workspace matrix report must describe its selection"
    );
    require_true(
        report.at("selection").contains("projects"),
        "multi-project selection must list projects"
    );
    require_true(
        report.at("selection").at("projects").size() == 2U,
        "multi-project selection must keep two projects"
    );
    require_true(
        report.at("projects").size() == 2U,
        "multi-project workspace matrix report must contain two projects"
    );

    std::set<std::string> projects;
    for (const json& project : report.at("projects")) {
        projects.insert(project.at("project").get<std::string>());
    }
    require_true(
        projects.contains("alpha"),
        "multi-project workspace matrix report must include alpha"
    );
    require_true(
        projects.contains("beta"),
        "multi-project workspace matrix report must include beta"
    );
    require_true(
        !projects.contains("gamma"),
        "multi-project workspace matrix report must exclude gamma"
    );
}

void test_cli_workspace_unqualified_artifact_filter_rejects_multi_project_selection() {
    temp_dir root;
    write_sample_workspace_project(root.path(), "alpha");
    write_sample_workspace_project(root.path(), "beta");

    const cli_result result = run_cli(
        root.path(), "report matrix --project alpha --project beta alpha:app"
    );
    require_true(
        result.exit_code != 0,
        "unqualified workspace artifact filters must reject multi-project "
        "selection"
    );
    require_contains(
        result.output,
        "unqualified workspace artifact filters require exactly one selected "
        "workspace project",
        "unqualified workspace artifact filter error must explain the "
        "ambiguity"
    );
}

void test_cli_run_executes_facade_entry() {
    temp_dir root;
    write_sample_build_project(
        root.path(), "sample",
        "#include <iostream>\nint main() { std::cout << "
        "\"sample-run\\n\"; return 0; }\n"
    );

    const cli_result result = run_cli(root.path(), "run debug");
    require_true(
        result.exit_code == 0, "ecos run must execute the facade entry artifact"
    );
    require_contains(
        result.output, "sample-run",
        "ecos run must execute the facade entry binary"
    );
    require_contains(
        result.output, "ran sample:app",
        "ecos run must report the executed artifact"
    );
}

void test_cli_run_executes_requested_artifact_with_passthrough_args() {
    temp_dir root;
    write_sample_dual_run_project(root.path());

    const cli_result result
        = run_cli(root.path(), "run debug tool:cli -- alpha --then beta");
    require_true(
        result.exit_code == 0,
        "ecos run must execute an explicitly selected runnable artifact"
    );
    require_contains(
        result.output, "tool-run alpha --then beta",
        "ecos run must forward passthrough arguments to the child artifact"
    );
    require_not_contains(
        result.output, "app-run",
        "ecos run must not execute the facade entry when "
        "another artifact was requested"
    );
}

void test_cli_workspace_run_executes_selected_project() {
    temp_dir root;
    write_sample_build_project(
        root.path() / "alpha", "alpha",
        "#include <iostream>\nint main() { std::cout << "
        "\"alpha-run\\n\"; return 0; }\n"
    );
    write_sample_build_project(
        root.path() / "beta", "beta",
        "#include <iostream>\nint main() { std::cout << "
        "\"beta-run\\n\"; return 0; }\n"
    );

    const cli_result result = run_cli(root.path(), "run debug --project alpha");
    require_true(
        result.exit_code == 0,
        "workspace ecos run must execute the selected project"
    );
    require_contains(
        result.output, "== alpha (alpha) ==",
        "workspace ecos run must identify the selected project"
    );
    require_contains(
        result.output, "alpha-run",
        "workspace ecos run must execute the selected project's facade entry"
    );
    require_not_contains(
        result.output, "beta-run",
        "workspace ecos run must not execute unselected projects"
    );
}

void test_cli_workspace_run_rejects_multiple_artifact_filters() {
    temp_dir root;
    write_sample_dual_run_workspace_project(root.path(), "alpha");

    const cli_result result
        = run_cli(root.path(), "run debug --project alpha app:app tool:cli");
    require_true(
        result.exit_code != 0,
        "workspace ecos run must reject multiple artifact filters"
    );
    require_contains(
        result.output, "run supports at most one workspace artifact filter",
        "workspace ecos run must explain why multiple artifact filters "
        "are invalid"
    );
}

void test_cli_workspace_check_ci_rejects_multiple_artifact_filters() {
    temp_dir root;
    write_sample_dual_run_workspace_project(root.path(), "alpha");

    const cli_result result
        = run_cli(root.path(), "check ci --project alpha app:app tool:cli");
    require_true(
        result.exit_code != 0,
        "workspace ecos check ci must reject multiple artifact filters"
    );
    require_contains(
        result.output,
        "check ci supports at most one workspace artifact filter",
        "workspace ecos check ci must explain that multi-artifact scope is "
        "unsupported"
    );
}

void test_cli_run_android_deploys_selected_artifact() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_build_manifest("sample");
    manifest_value.components.front().stack = json::object(
        {
            { "qt", json::array({ "Core" }) },
            { "android", json::array({ "qt_android" }) },
        }
    );
    manifest_value.components.front().artifacts.front().kind = "qt_app";

    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary android manifest"
    );
    write_text(root.path() / "src/main.cpp", "int main() { return 0; }\n");

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path sdk_root = fake_root / "sdk";
    const fs::path ndk_root = sdk_root / "ndk" / "27.2.12479018";
    const fs::path adb_log = fake_root / "adb.log";
    fs::create_directories(fake_bin);
    fs::create_directories(ndk_root);

    write_executable_script(
        fake_bin / "qt-cmake",
        "#!/usr/bin/env bash\n"
        "while [ $# -gt 0 ]; do\n"
        "  if [ \"$1\" = \"-B\" ]; then\n"
        "    build_dir=\"$2\"\n"
        "    shift 2\n"
        "    continue\n"
        "  fi\n"
        "  shift\n"
        "done\n"
        "mkdir -p \"$build_dir/outputs/apk/debug\"\n"
        ": > \"$build_dir/outputs/apk/debug/stale-debug.apk\"\n"
        ": > \"$build_dir/outputs/apk/debug/sample__app-x86_64-debug.apk\"\n"
        ": > \"$build_dir/outputs/apk/debug/sample__app-arm64-debug.apk\"\n"
        "exit 0\n"
    );
    write_executable_script(
        fake_bin / "cmake",
        "#!/usr/bin/env bash\n"
        "build_dir=\"\"\n"
        "target=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  case \"$1\" in\n"
        "    --build)\n"
        "      build_dir=\"$2\"\n"
        "      shift 2\n"
        "      ;;\n"
        "    --target)\n"
        "      target=\"$2\"\n"
        "      shift 2\n"
        "      ;;\n"
        "    *)\n"
        "      shift\n"
        "      ;;\n"
        "  esac\n"
        "done\n"
        "mkdir -p \"$build_dir\"\n"
        "if [ \"$target\" = \"apk\" ]; then\n"
        "  mkdir -p \"$build_dir/android-build\"\n"
        "  : > \"$build_dir/android-build/sample__app.apk\"\n"
        "fi\n"
        "exit 0\n"
    );
    write_executable_script(
        fake_bin / "adb",
        "#!/usr/bin/env bash\n"
        "if [ \"$1\" = \"devices\" ]; then\n"
        "  echo \"List of devices attached\"\n"
        "  echo \"emulator-5554 device product:fake model:FakeDevice\"\n"
        "  exit 0\n"
        "fi\n"
        "if [ \"$3\" = \"wait-for-device\" ]; then\n"
        "  exit 0\n"
        "fi\n"
        "if [ \"$3\" = \"shell\" ] && [ \"$4\" = \"getprop\" ] && [ \"$5\" = "
        "\"sys.boot_completed\" ]; then\n"
        "  echo 1\n"
        "  exit 0\n"
        "fi\n"
        "printf '%s\\n' \"$*\" >> \"$FAKE_ADB_LOG\"\n"
        "exit 0\n"
    );
    write_executable_script(
        fake_bin / "aapt",
        "#!/usr/bin/env bash\n"
        "echo \"package: name='org.example.sample' versionCode='1'\"\n"
    );

    const std::string original_path = []() {
        const char* value = std::getenv("PATH");
        return value == nullptr ? std::string() : std::string(value);
    }();
    scoped_env path_env("PATH", fake_bin.string() + ":" + original_path);
    scoped_env sdk_env("ANDROID_SDK_ROOT", sdk_root.string());
    scoped_env ndk_env("ANDROID_NDK_ROOT", ndk_root.string());
    scoped_env qt_cmake_env(
        "ANDROID_CMAKE_BIN", (fake_bin / "qt-cmake").string()
    );
    scoped_env adb_env("ADB_BIN", (fake_bin / "adb").string());
    scoped_env aapt_env("AAPT_BIN", (fake_bin / "aapt").string());
    scoped_env adb_log_env("FAKE_ADB_LOG", adb_log.string());
    scoped_env boot_timeout_env("ANDROID_EMULATOR_BOOT_TIMEOUT", "5");

    const cli_result result
        = run_cli(root.path(), "run android --android-mode emulator");
    require_true(
        result.exit_code == 0,
        "ecos run android must deploy a Qt Android artifact"
    );
    require_contains(
        result.output, "ran sample:app",
        "android ecos run must report the deployed artifact"
    );
    require_contains(
        read_text(adb_log), "install -r",
        "android ecos run must install the APK through adb"
    );
    require_contains(
        read_text(adb_log), "android-build/sample__app.apk",
        "android ecos run must rebuild and select the requested target APK "
        "instead of a stale package"
    );
    require_contains(
        read_text(adb_log), "shell monkey -p org.example.sample",
        "android ecos run must launch the installed package through adb"
    );
}

void test_cli_check_naming_reports_identifier_overflow_and_honors_allowlist() {
    temp_dir root;
    write_sample_build_project(
        root.path(), "sample",
        "int before_widget_local_display_bytes_estimated = 0;\n"
        "int after_widget_local_display_bytes_estimated = 0;\n"
        "int this_identifier_is_far_too_long_for_policy = 0;\n"
        "int main() { return this_identifier_is_far_too_long_for_policy; }\n"
    );
    write_text(
        root.path() / "docs/naming_allowlist.txt",
        "before_widget_local_display_bytes_estimated\n"
        "after_widget_local_display_bytes_estimated\n"
    );

    const cli_result result = run_cli(root.path(), "check naming");
    require_true(
        result.exit_code == 0,
        "personal naming findings must succeed as advisory diagnostics"
    );
    require_contains(
        result.output, "this_identifier_is_far_too_long_for_policy",
        "ecos check naming must report the violating identifier"
    );
    require_not_contains(
        result.output, "before_widget_local_display_bytes_estimated",
        "ecos check naming must honor entries from docs/naming_allowlist.txt"
    );
}

void test_personal_naming_uses_declarations_and_exact_policy_boundaries() {
    temp_dir root;
    write_text(
        root.path() / "manifest.json",
        json(
            { { "id", "naming_sample" },
              { "description", "Naming acceptance" },
              { "facade", "core:app" },
              { "artifacts",
                json::array(
                    { { { "id", "core:app" },
                        { "kind", "exe" },
                        { "owns",
                          json::array({ "BadDirectory", "tests/names.cpp" }) },
                        { "entry", "src/main.cpp" } },
                      { { "id", "core:other" },
                        { "kind", "exe" },
                        { "owns", json::array() },
                        { "entry", "src/other_main.cpp" } } }
                ) } }
        ).dump(2)
    );
    write_text(
        root.path() / "include/BadDirectory/a_b_c_d_e.hpp",
        "#pragma once\nstruct named_type { int m_value; };\n"
    );
    write_text(
        root.path() / "src/main.cpp",
        "#include \"BadDirectory/a_b_c_d_e.hpp\"\n"
        "// comment_with_far_too_many_words_only\n"
        "const char *text = R\"tag(string_with_far_too_many_words_only)tag\";\n"
        "int a_b_c_d = 0;\nint a_b_c_d_e = 0;\n"
        "int abcdefghijklmnopqrstuvwxy = 0;\nint abcdefghijklmnopqrstuvwxyz = "
        "0;\n"
        "int _leading = 0;\nint trailing_ = 0;\nint camelName = 0;\n"
        "int operator_helper_with_many_words() { return 0; }\n"
        "int main() { return a_b_c_d_e + a_b_c_d_e; }\n"
    );
    write_text(
        root.path() / "tests/names.cpp",
        "int a_b_c_d_e_f_g = 0;\nint a_b_c_d_e_f_g_h = 0;\n"
        "int deliberatelylongtestsymbolwithoutacharacterbound = 0;\n"
    );
    write_text(root.path() / "src/other_main.cpp", "#error unselected owner\n");
    const auto result = run_engels_cli(root.path(), "check naming core:app");
    require_true(
        result.exit_code == 0,
        "advisory naming must analyze a selected owner:\n" + result.output
    );
    const auto path = root.path() / ".ecosystem/reports/naming.json";
    const auto report = json::parse(read_text(path));
    require_true(
        report.at("status") == "findings" && report.at("errors").empty(),
        "findings must remain separate from analysis failures"
    );
    const auto count = [&](const std::string& entity, const std::string& rule,
                           const std::string& kind = "symbol") {
        return std::count_if(
            report.at("findings").begin(), report.at("findings").end(),
            [&](const auto& finding) {
                return finding.at("entity") == entity
                    && finding.at("rule") == rule
                    && finding.at("entity_kind") == kind;
            }
        );
    };
    require_true(
        count("a_b_c_d", "naming.words") == 0
            && count("a_b_c_d_e", "naming.words") == 1,
        "word limits must use >4 and report the declaration once, not "
        "references"
    );
    require_true(
        count("operator_helper_with_many_words", "naming.words") == 1,
        "ordinary names beginning with operator must not be mistaken for "
        "overloaded operators"
    );
    require_true(
        count("abcdefghijklmnopqrstuvwxy", "naming.characters") == 0
            && count("abcdefghijklmnopqrstuvwxyz", "naming.characters") == 1,
        "character limit must use >25"
    );
    require_true(
        count("a_b_c_d_e_f_g", "naming.words") == 0
            && count("a_b_c_d_e_f_g_h", "naming.words") == 1,
        "test word limit must use >7"
    );
    require_true(
        count(
            "deliberatelylongtestsymbolwithoutacharacterbound",
            "naming.characters"
        ) == 0,
        "uncalibrated test character bounds must remain disabled"
    );
    for (const auto& [entity, rule] :
         std::vector<std::pair<std::string, std::string>> {
             { "_leading", "naming.leading_underscore" },
             { "trailing_", "naming.trailing_underscore" },
             { "m_value", "naming.scope_prefix" },
             { "camelName", "naming.snake_case" } })
        require_true(
            count(entity, rule) == 1,
            "required naming rule must identify its declaration"
        );
    require_true(
        count("BadDirectory", "naming.snake_case", "directory") == 1
            && count("a_b_c_d_e", "naming.words", "file") == 1,
        "filesystem policy must include owned file and directory names"
    );
    require_not_contains(
        report.dump(), "comment_with_far_too_many_words_only",
        "comments must not become symbols"
    );
    require_not_contains(
        report.dump(), "string_with_far_too_many_words_only",
        "raw strings must not become symbols"
    );
    require_not_contains(
        report.dump(), "other_main",
        "artifact filters must not widen to a sibling owner"
    );
    for (const auto& finding : report.at("findings")) {
        require_true(
            finding.at("authority") == "manifesto"
                && finding.at("enforcement") == "advisory"
                && finding.at("artifact") == "core:app"
                && finding.at("line").get<unsigned>() > 0,
            "findings must retain policy and location attribution"
        );
    }
    write_text(root.path() / "docs/naming_allowlist.txt", "a_b_c_d_e\n");
    require_true(
        run_engels_cli(root.path(), "check naming core:app").exit_code == 0,
        "sparse symbol exceptions must work"
    );
    const auto excepted = json::parse(read_text(path));
    require_true(
        std::none_of(
            excepted.at("findings").begin(), excepted.at("findings").end(),
            [](const auto& finding) {
                return finding.at("entity") == "a_b_c_d_e"
                    && finding.at("entity_kind") == "symbol";
            }
        ),
        "allowlist must suppress only the named symbol"
    );
    require_true(
        std::any_of(
            excepted.at("findings").begin(), excepted.at("findings").end(),
            [](const auto& finding) {
                return finding.at("entity") == "a_b_c_d_e"
                    && finding.at("entity_kind") == "file";
            }
        ),
        "symbol exceptions must not waive filesystem policy"
    );
    const auto before_invalid = read_text(path);
    require_true(
        run_engels_cli(root.path(), "check naming core:absent").exit_code == 2
            && read_text(path) == before_invalid,
        "unknown scopes must fail before replacing reports"
    );
}

void test_personal_naming_reports_analysis_failures_and_owned_headers() {
    temp_dir root;
    write_sample_build_project(
        root.path(), "sample",
        "#include \"external.hpp\"\nint main() { return dependencyName(); }\n"
    );
    write_text(
        root.path() / "include/external.hpp",
        "inline int dependencyName() { return 0; }\n"
    );
    auto result = run_engels_cli(root.path(), "check naming");
    require_true(
        result.exit_code == 0,
        "unowned declarations must stay outside personal naming:\n"
            + result.output
    );
    require_not_contains(
        result.output, "dependencyName",
        "included external declarations must not receive project naming "
        "findings"
    );
    auto authored = json::parse(read_text(root.path() / "manifest.json"));
    authored["artifacts"][0]["owns"] = json::array({ "api" });
    write_text(root.path() / "manifest.json", authored.dump(2));
    write_text(
        root.path() / "include/api.hpp",
        "struct public_type { int m_field; };\n"
    );
    write_text(
        root.path() / "include/external.hpp",
        "namespace dependency {\n#include \"api.hpp\"\n}\n"
    );
    write_text(
        root.path() / "src/main.cpp",
        "#include \"external.hpp\"\nint main() { return 0; }\n"
    );
    result = run_engels_cli(root.path(), "check naming");
    require_true(
        result.exit_code == 0,
        "owned headers inside dependency namespace contexts must remain "
        "analyzable"
    );
    require_contains(
        result.output, "m_field",
        "namespace ownership must not hide declarations in an included owned "
        "file"
    );
    write_text(
        root.path() / "src/main.cpp",
        "#include \"absent.hpp\"\nint main() { return 0; }\n"
    );
    result = run_engels_cli(root.path(), "check naming");
    require_true(
        result.exit_code == 5,
        "unparseable analysis inputs must fail the operation"
    );
    const auto report = json::parse(
        read_text(root.path() / ".ecosystem/reports/naming.json")
    );
    require_true(
        report.at("status") == "failed" && !report.at("errors").empty(),
        "analysis failure must persist distinctly from findings"
    );
    require_contains(
        result.output, "absent.hpp",
        "parse errors must identify the missing input"
    );

    const auto header_project = root.path() / "headers";
    write_text(
        header_project / "manifest.json",
        json(
            { { "id", "headers" },
              { "description", "Header-only naming" },
              { "facade", "core:lib" },
              { "artifacts",
                json::array(
                    { { { "id", "core:lib" },
                        { "kind", "interface_lib" },
                        { "owns", json::array({ "api" }) } } }
                ) } }
        ).dump(2)
    );
    write_text(
        header_project / "include/api.hpp",
        "#pragma once\nstruct public_type { int m_field; };\n"
    );
    result = run_engels_cli(header_project, "check naming");
    require_true(
        result.exit_code == 0,
        "standalone owned headers must be analyzable:\n" + result.output
    );
    require_contains(
        result.output, "m_field", "header-only declarations must be covered"
    );
    const auto loaded
        = ecosystem::load_manifest(header_project / "manifest.json");
    fs::remove(header_project / "include/api.hpp");
    const auto missing = ecosystem::analyze_personal_checks(
        *loaded.value, header_project, std::nullopt
    );
    require_true(
        !missing.errors.empty(),
        "disappearing selected inputs must not silently become a clean empty "
        "scope"
    );
    fs::create_directories(header_project / "docs/naming_allowlist.txt");
    const auto invalid_exceptions = ecosystem::analyze_personal_checks(
        *loaded.value, header_project, std::nullopt
    );
    require_contains(
        join_lines(invalid_exceptions.errors),
        "unable to read naming exceptions",
        "unreadable exception inputs must not silently relax the analysis "
        "contract"
    );
}

void test_personal_style_measures_control_bodies_and_local_scopes() {
    temp_dir root;
    write_sample_build_project(
        root.path(), "sample",
        "namespace { int helper() { return 1; } int value = 0; }\n"
        "int main() {\n"
        "  int x = 0;\n"
        "  if (x) ++x; else --x;\n"
        "  while (x) --x;\n"
        "  do ++x; while (x < 0);\n"
        "  for (int y = 0; y < 1; ++y) x += y;\n"
        "  int values[] = { 1, 2 };\n"
        "  for (const auto y : values) x += y;\n"
        "  switch (x) { case 0: break; default: break; }\n"
        "  if (x) { ++x; }\n"
        "  auto small = [](int y) { if (y) { return 1; } return 0; };\n"
        "  auto large = [](int y) {\n"
        "    if (y > 0) {\n"
        "      while (y > 1) { --y; }\n"
        "    }\n"
        "    if (y < 0) { return 1; }\n"
        "    return y;\n"
        "  };\n"
        "  auto outer = []() { auto inner = [](int y) { if (y) { while (y) { "
        "--y; } } return y; }; return inner(0); };\n"
        "  const char *text = R\"tag(if (x) x++; namespace { fake_code(); "
        "})tag\";\n"
        "  return small(0) + large(0) + outer() + helper();\n"
        "}\n"
    );
    auto source = read_text(root.path() / "src/main.cpp");
    source += "int boundary() {\n" + std::string(57, '\n') + "return 0;\n}\n";
    source += "int excess() {\n" + std::string(58, '\n') + "return 0;\n}\n";
    for (const int decisions : { 9, 10 }) {
        source += "int complexity_" + std::to_string(decisions) + "(int x) {\n";
        for (int index = 0; index < decisions; ++index)
            source += "if (x == " + std::to_string(index) + ") { ++x; }\n";
        source += "return x;\n}\n";
    }
    write_text(root.path() / "src/main.cpp", source);
    const auto result = run_engels_cli(root.path(), "check style");
    require_true(
        result.exit_code == 0,
        "personal structural findings must remain advisory:\n" + result.output
    );
    const auto report
        = json::parse(read_text(root.path() / ".ecosystem/reports/style.json"));
    const auto count = [&](const std::string& rule) {
        return std::count_if(
            report.at("findings").begin(), report.at("findings").end(),
            [&](const auto& finding) { return finding.at("rule") == rule; }
        );
    };
    require_true(
        count("control.braces") == 6,
        "if/else/while/do/for/range-for bodies must be checked without "
        "treating strings as code"
    );
    require_true(
        count("namespace.anonymous") == 1,
        "each anonymous namespace must produce one locality finding"
    );
    require_true(
        count("function.span") == 1 && count("function.complexity") == 1,
        "ordinary function span and branch estimates must honor the exact "
        "60/10 boundaries"
    );
    require_true(
        count("lambda.span") == 1 && count("lambda.complexity") == 2
            && count("lambda.nesting") == 2,
        "lambda span, branch estimate and nesting must retain independent "
        "bounds"
    );
    bool saw_outer = false;
    bool saw_small = false;
    for (const auto& scope : report.at("scopes")) {
        if (scope.at("kind") == "lambda" && scope.at("line") == 20
            && scope.at("column") == 16) {
            saw_outer = true;
            require_true(
                scope.at("measurements").at("estimated_complexity") == 1
                    && scope.at("measurements").at("nesting") == 0,
                "nested lambda decisions must not inflate their parent's "
                "complexity"
            );
        }
        if (scope.at("kind") == "lambda" && scope.at("line") == 12) {
            saw_small = true;
            require_true(
                scope.at("measurements").at("estimated_complexity") == 2
                    && scope.at("measurements").at("nesting") == 1,
                "lambda boundary values must be measured without excess "
                "findings"
            );
        }
    }
    require_true(
        saw_outer && saw_small,
        "scope measurements must identify both boundary and nested lambdas"
    );
    for (const auto& finding : report.at("findings")) {
        if (finding.at("rule") == "namespace.anonymous")
            require_true(
                finding.at("measurements").at("declarations") == 2
                    && finding.at("measurements").at("effective_lines") == 1,
                "namespace locality must include declaration and source-volume "
                "evidence"
            );
    }
    require_not_contains(
        report.dump(), "function.nesting",
        "undefined ordinary-function nesting thresholds must remain disabled"
    );
    require_contains(
        result.output, "largest owned files",
        "style must expose useful volume measurements in the terminal"
    );
}

void test_personal_style_indexes_referents_and_measures_volume_without_size_gates() {
    temp_dir root;
    write_text(
        root.path() / "manifest.json",
        json(
            { { "id", "tree_sample" },
              { "description", "Referent metrics" },
              { "facade", "core:lib" },
              { "artifacts",
                json::array(
                    { { { "id", "core:lib" },
                        { "kind", "static_lib" },
                        { "owns",
                          json::array(
                              { "math", "flat", "deep", "tests", "benchmarks",
                                "src/no_referent" }
                          ) } } }
                ) } }
        ).dump(2)
    );
    write_text(
        root.path() / "include/math.hpp",
        "#pragma once\n\n// header comment\nint meaning();\n"
    );
    write_text(root.path() / "include/math.tpp", "// template companion\n\n");
    write_text(
        root.path() / "src/math.cpp",
        "#include \"math.hpp\"\n/* comment\nstill comment */\nint meaning() { "
        "return 42; }\n"
    );
    write_text(
        root.path() / "tests/math_tests.cpp",
        "// test\nint test_math() { return 0; }\n"
    );
    write_text(
        root.path() / "benchmarks/math_benchmarks.cpp",
        "int bench_math() { return 0; }\n"
    );
    for (const auto* name : { "a", "b", "c" })
        write_text(
            root.path() / (std::string("include/flat/") + name + ".hpp"),
            std::string("#pragma once\nstruct ") + name + " {};\n"
        );
    write_text(
        root.path() / "include/deep/one/two/only.hpp",
        "#pragma once\nstruct only {};\n"
    );
    write_text(
        root.path() / "src/no_referent/unrelated.cpp",
        std::string(300, '\n') + "int unrelated() { return 0; }\n"
    );
    write_text(
        root.path() / "src/no_referent/raw.cpp",
        "const char *text = R\"(first\n// string text\nlast)\";\n"
    );
    const auto result = run_engels_cli(root.path(), "check style");
    require_true(
        result.exit_code == 0,
        "referent and volume findings must succeed:\n" + result.output
    );
    const auto report
        = json::parse(read_text(root.path() / ".ecosystem/reports/style.json"));
    require_true(
        report.at("referents").size() == 5,
        "only headers under include must form the reduced referent tree"
    );
    for (const auto& file : report.at("files")) {
        if (file.at("file") == "src/no_referent/raw.cpp")
            require_true(
                file.at("physical_lines") == 3
                    && file.at("effective_lines") == 3,
                "multiline literals must count as effective source even when "
                "their content resembles comments"
            );
    }
    bool saw_math = false;
    for (const auto& ref : report.at("referents")) {
        if (ref.at("id") == "math") {
            saw_math = true;
            require_true(
                ref.at("physical_lines") == 10
                    && ref.at("effective_lines") == 4,
                "production volume must include header/source/template "
                "companions and exclude comments, tests and benchmarks"
            );
            require_true(
                ref.at("files").size() == 5,
                "the referent index must retain test and benchmark "
                "relationships"
            );
        }
    }
    require_true(
        saw_math, "conventional companion stems must identify a referent"
    );
    bool saw_flat = false, saw_deep = false, saw_root = false;
    for (const auto& module : report.at("modules")) {
        const auto& metrics = module.at("measurements");
        if (module.at("module") == "flat") {
            saw_flat = true;
            require_true(
                metrics.at("referents") == 3 && metrics.at("children") == 3
                    && module.at("targets").at("children") == 2
                    && metrics.at("width_excess") == 0.5,
                "flatness must use the manifesto square-root target"
            );
        }
        if (module.at("module") == "deep") {
            saw_deep = true;
            require_true(
                metrics.at("referents") == 1 && metrics.at("depth") == 3
                    && module.at("targets").at("depth") == 1,
                "one-child chains must retain their depth evidence"
            );
        }
        if (module.at("module") == ".") {
            saw_root = true;
            require_true(
                metrics.at("referents") == 5 && metrics.at("depth") == 4
                    && module.at("targets").at("depth") == 2,
                "root depth must derive from descendant referents"
            );
        }
        require_not_contains(
            module.dump(), "no_referent",
            "non-referent subtrees must not create artificial modules"
        );
    }
    require_true(
        saw_flat && saw_deep && saw_root,
        "the index must expose module-level structural measurements"
    );
    require_true(
        std::any_of(
            report.at("findings").begin(), report.at("findings").end(),
            [](const auto& finding) {
                return finding.at("rule") == "tree.width";
            }
        ),
        "structural excess must produce an advisory hint"
    );
    require_true(
        std::none_of(
            report.at("findings").begin(), report.at("findings").end(),
            [](const auto& finding) {
                return finding.at("rule")
                    .template get<std::string>()
                    .starts_with("volume.");
            }
        ),
        "uncalibrated file-volume bounds must not become findings or gates"
    );
}

void test_cli_personal_style_preserves_workspace_scope_and_failure_status() {
    temp_dir root;
    write_sample_dual_run_workspace_project(root.path(), "alpha");
    write_sample_dual_run_workspace_project(root.path(), "beta");
    write_sample_dual_run_workspace_project(root.path(), "gamma");
    write_sample_workspace_config(root.path());
    write_text(root.path() / "broken/manifest.json", "invalid JSON\n");
    write_text(
        root.path() / "alpha/src/main.cpp",
        "int main() { int x = 0; if (x) ++x; return x; }\n"
    );
    write_text(
        root.path() / "alpha/src/cli_main.cpp", "#error unselected artifact\n"
    );
    auto result = run_engels_cli(root.path(), "check style");
    require_true(
        result.exit_code == 2
            && !fs::exists(root.path() / "alpha/.ecosystem/reports/style.json"),
        "workspace validity must be checked before selected reports are written"
    );
    result = run_engels_cli(
        root.path(), "check style alpha/app:app beta/tool:cli"
    );
    require_true(
        result.exit_code == 0,
        "multiple scoped workspace style checks must accept advisory "
        "findings:\n"
            + result.output
    );
    require_contains(
        result.output, "control.braces",
        "selected personal findings must remain visible"
    );
    const auto report_path
        = root.path() / "alpha/.ecosystem/reports/style.json";
    require_not_contains(
        read_text(report_path), "cli_main",
        "style must not widen selected artifact ownership"
    );
    result = run_engels_cli(root.path(), "check style --group core");
    require_true(
        result.exit_code == 5,
        "analysis failures in an explicitly selected group must fail the "
        "operation"
    );
    require_contains(
        result.output, "unselected artifact",
        "group analysis errors must retain source attribution"
    );
    fs::remove(report_path);
    fs::create_directory(report_path);
    result = run_engels_cli(root.path(), "check style alpha/app:app");
    require_true(
        result.exit_code == 5,
        "failure to persist a personal report must fail the operation"
    );
}

void test_cli_check_leaks_runs_sanitized_tests() {
    temp_dir root;
    write_sample_leak_check_project(root.path());

    const fs::path copied_engels = root.path() / "bin" / "engels";
    fs::create_directories(copied_engels.parent_path());
    fs::copy_file(
        engels_binary_path(), copied_engels,
        fs::copy_options::overwrite_existing
    );
    fs::permissions(
        copied_engels,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add
    );

    // This probe deliberately copies only the actor; supply its matching data
    // explicitly.
    scoped_env templates(
        "MANIFESTO_TEMPLATE_ROOT",
        (fs::path(ECOS_TEST_SOURCE_DIR) / "templates").string()
    );
    const cli_result result
        = run_cli_with_binary(copied_engels, root.path(), "check leaks");
    if (result.exit_code == 0) {
        require_contains(
            result.output, "leak check passed",
            "ecos check leaks must report success after the sanitized test run"
        );
        return;
    }
    if (result.output.find(
            "sanitizer-backed leak checks are unavailable while "
            "the process is being traced"
        )
        != std::string::npos) {
        return;
    }
    require_contains(
        result.output, "LeakSanitizer does not work under ptrace",
        "ecos check leaks must either preflight traced environments "
        "or surface the underlying LSAN ptrace limitation"
    );
}

void test_cli_check_tidy_uses_clang_tidy_when_available() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path cmake_log = fake_root / "cmake.log";
    const fs::path clang_tidy_log = fake_root / "clang-tidy.log";
    fs::create_directories(fake_bin);
    write_fake_configure_cmake_with_compile_database(fake_bin / "cmake");
    write_fake_clang_tidy_tool(fake_bin / "clang-tidy");
    write_executable_script(fake_bin / "clang++", "#!/bin/bash\nexit 0\n");
    write_executable_script(fake_bin / "clang", "#!/bin/bash\nexit 0\n");

    scoped_env path_env("PATH", fake_bin.string() + ":" + current_path_env());
    scoped_env cmake_log_env("FAKE_CMAKE_LOG", cmake_log.string());
    scoped_env clang_tidy_log_env(
        "FAKE_CLANG_TIDY_LOG", clang_tidy_log.string()
    );

    const cli_result result = run_cli(root.path(), "check tidy");
    require_true(
        result.exit_code == 0,
        "ecos check tidy must succeed when clang-tidy is available and "
        "reports no diagnostics"
    );
    require_contains(
        read_text(clang_tidy_log), "--warnings-as-errors=*",
        "ecos check tidy must run clang-tidy with warnings promoted to "
        "errors"
    );
    require_contains(
        read_text(clang_tidy_log), "src/main.cpp",
        "ecos check tidy must pass the selected manifest-declared source "
        "files to clang-tidy"
    );

    const json report
        = json::parse(read_text(root.path() / ".ecosystem/reports/tidy.json"));
    require_true(
        report.at("clang_tidy_available").get<bool>()
            && report.at("clang_tidy_used").get<bool>()
            && report.at("compilation_database_available").get<bool>(),
        "tidy report must record clang-tidy execution when the tool and "
        "compilation database are available"
    );
    require_true(
        report.at("clang_tidy_exit_code").get<int>() == 0,
        "tidy report must record a successful clang-tidy exit code"
    );
}

void test_cli_check_tidy_fails_when_clang_tidy_reports_diagnostics() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path cmake_log = fake_root / "cmake.log";
    fs::create_directories(fake_bin);
    write_fake_configure_cmake_with_compile_database(fake_bin / "cmake");
    write_fake_clang_tidy_tool(fake_bin / "clang-tidy");
    write_executable_script(fake_bin / "clang++", "#!/bin/bash\nexit 0\n");
    write_executable_script(fake_bin / "clang", "#!/bin/bash\nexit 0\n");

    scoped_env path_env("PATH", fake_bin.string() + ":" + current_path_env());
    scoped_env cmake_log_env("FAKE_CMAKE_LOG", cmake_log.string());
    scoped_env clang_tidy_output_env(
        "FAKE_CLANG_TIDY_OUTPUT",
        "src/main.cpp:1:1: warning: fake tidy warning [fake-check]"
    );
    scoped_env clang_tidy_exit_env("FAKE_CLANG_TIDY_EXIT_CODE", "1");

    const cli_result result = run_cli(root.path(), "check tidy");
    require_true(
        result.exit_code != 0,
        "ecos check tidy must fail when clang-tidy reports diagnostics"
    );
    require_contains(
        result.output, "clang-tidy reported diagnostics",
        "ecos check tidy must surface clang-tidy failures distinctly"
    );
    require_not_contains(
        result.output, "Clang analysis reported diagnostics",
        "clang-tidy failures must not be misreported as libclang analysis "
        "failures"
    );

    const json report
        = json::parse(read_text(root.path() / ".ecosystem/reports/tidy.json"));
    require_true(
        report.at("clang_tidy_used").get<bool>()
            && report.at("clang_tidy_exit_code").get<int>() == 1,
        "tidy report must persist clang-tidy failure state"
    );
    require_true(
        !report.at("clang_tidy_output").empty(),
        "tidy report must persist clang-tidy output lines"
    );
}

void test_tidy_requires_tool_and_complete_compilation_database() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    const auto loaded = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(loaded.value.has_value(), "tidy fixture must load");
    const auto database = ecosystem::local_build_dir(root.path(), "debug")
        / "compile_commands.json";
    const auto bin = root.path() / "bin";
    fs::create_directory(bin);
    const auto inspect = [&]() {
        return ecosystem::run_tidy_check(
            *loaded.value, root.path(), std::nullopt, true, false, "debug"
        );
    };
    {
        scoped_env path("PATH", bin.string());
        const auto report = inspect();
        require_true(
            !report.clang_tidy_used && !report.clang_tidy_available,
            "missing clang-tidy must remain an unexecuted check"
        );
        require_contains(
            report.clang_tidy_skip_reason, "clang-tidy is not available",
            "missing analyzer must have an actionable reason"
        );
    }
    write_fake_clang_tidy_tool(bin / "clang-tidy");
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    auto report = inspect();
    require_true(
        !report.clang_tidy_used && !report.compilation_database_available,
        "missing database must not trigger fallback analysis"
    );
    for (const std::string contents :
         { "not JSON", "{}", "[]",
           "[{\"directory\":\"/"
           "tmp\",\"file\":\"absent.cpp\",\"command\":\"clang++ -c "
           "absent.cpp\"}]" }) {
        write_text(database, contents);
        report = inspect();
        require_true(
            !report.clang_tidy_used && !report.clang_tidy_skip_reason.empty(),
            "invalid or incomplete databases must not silently use guessed "
            "flags"
        );
    }

    write_fake_configure_and_build_cmake(bin / "cmake");
    write_executable_script(bin / "clang++", "#!/bin/sh\nexit 0\n");
    write_executable_script(bin / "clang", "#!/bin/sh\nexit 0\n");
    scoped_env cmake_log(
        "FAKE_CMAKE_LOG", (root.path() / "cmake.log").string()
    );
    fs::remove(database);
    auto result = run_engels_cli(root.path(), "check tidy");
    require_true(
        result.exit_code == 5, "CLI must fail an unexecuted required tidy check"
    );
    require_contains(
        result.output, "compile_commands.json is not available",
        "CLI must name missing configured inputs"
    );
    require_contains(
        result.output,
        "tidy report:", "failed checks must locate their persisted report"
    );

    fs::remove(bin / "clang-tidy");
    {
        scoped_env missing("PATH", bin.string());
        result = run_engels_cli(root.path(), "check tidy");
        require_true(
            result.exit_code == 4,
            "CLI must classify missing clang-tidy as missing tooling"
        );
        require_contains(
            result.output, "clang-tidy is not available",
            "missing tool must not be reported as success"
        );
    }
}

void test_cli_check_tidy_honors_full_artifact_identity_and_configuration() {
    temp_dir root;
    const auto project = root.path() / "project with spaces";
    write_text(
        project / "manifest.json",
        json(
            { { "id", "tidy_sample" },
              { "description", "Required tidy scope" },
              { "facade", "core:app" },
              { "artifacts",
                json::array(
                    { { { "id", "core:app" },
                        { "kind", "exe" },
                        { "owns", json::array() },
                        { "entry", "src/main.cpp" } },
                      { { "id", "core:other" },
                        { "kind", "exe" },
                        { "owns", json::array() },
                        { "entry", "src/other_main.cpp" } } }
                ) } }
        ).dump(2)
    );
    const std::string clean
        = "#ifndef REQUIRED_FROM_FLAGS\n#error configured compiler definition "
          "missing\n#endif\nint main() { return 0; }\n";
    write_text(project / "src/main.cpp", clean);
    write_text(
        project / "src/other_main.cpp",
        "#error unselected artifact must not be analyzed\nint main() { return "
        "0; }\n"
    );
    write_text(
        project / ".clang-tidy",
        "Checks: '-*,clang-analyzer-core.NullDereference'\n"
    );
    scoped_env flags("CXXFLAGS", "-DREQUIRED_FROM_FLAGS=1");
    auto result = run_engels_cli(project, "check tidy core:app");
    require_true(
        result.exit_code == 0,
        "native tidy must honor selected configured inputs:\n" + result.output
    );
    const auto report_path = project / ".ecosystem/reports/tidy.json";
    auto report = json::parse(read_text(report_path));
    require_true(
        report.at("clang_tidy_used").get<bool>()
            && report.at("files_analyzed") == 1,
        "both analyzers must select only the requested artifact owner"
    );
    require_true(
        report.at("sources").at(0).at("file") == "src/main.cpp",
        "report must preserve the selected source identity"
    );
    const auto before_invalid = read_text(report_path);
    result = run_engels_cli(project, "check tidy core:absent");
    require_true(
        result.exit_code == 2 && read_text(report_path) == before_invalid,
        "unknown artifacts must fail before analysis or report replacement"
    );

    write_text(
        project / "src/main.cpp",
        "int main() { int *value = nullptr; return *value; }\n"
    );
    result = run_engels_cli(project, "check tidy core:app");
    require_true(
        result.exit_code == 5,
        "actual clang-tidy diagnostics must fail the operation"
    );
    require_contains(
        result.output, "clang-analyzer-core.NullDereference",
        "terminal output must show the actual diagnostic and check"
    );
    require_contains(
        result.output, "src/main.cpp",
        "terminal output must identify the failing source"
    );
    report = json::parse(read_text(report_path));
    require_true(
        report.at("clang_tidy_exit_code").get<int>() != 0
            && !report.at("clang_tidy_output").empty(),
        "native diagnostic evidence must persist on failure"
    );

    write_text(project / "src/main.cpp", clean);
    write_text(project / ".clang-tidy", "UnknownConfigurationKey: true\n");
    result = run_engels_cli(project, "check tidy core:app");
    require_true(
        result.exit_code == 5,
        "invalid tidy configuration must fail rather than skip"
    );
    write_text(
        project / ".clang-tidy",
        "Checks: '-*,clang-analyzer-core.NullDereference'\n"
    );
    result = run_engels_cli(project, "check tidy");
    require_true(
        result.exit_code == 5,
        "whole-project tidy must still check the other artifact"
    );
    require_contains(
        result.output, "unselected artifact must not be analyzed",
        "whole-project diagnostics must retain the failing compiler message"
    );
}

void test_cli_build_release_uses_release_build_type() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path cmake_log = fake_root / "cmake.log";
    fs::create_directories(fake_bin);

    write_fake_configure_and_build_cmake(fake_bin / "cmake");
    write_executable_script(
        fake_bin / "clang++", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_executable_script(
        fake_bin / "clang", "#!/usr/bin/env bash\nexit 0\n"
    );

    scoped_env path_env("PATH", fake_bin.string() + ":" + current_path_env());
    scoped_env cmake_log_env("FAKE_CMAKE_LOG", cmake_log.string());

    const cli_result result = run_cli(root.path(), "build release");
    require_true(
        result.exit_code == 0,
        "ecos build release must succeed for a runnable sample"
    );
    require_contains(
        result.output,
        "built sample:app in " + relative_build_dir(root.path(), "release"),
        "ecos build release must report the release build directory"
    );
    require_contains(
        read_text(cmake_log), "-DCMAKE_BUILD_TYPE=Release",
        "ecos build release must configure CMake in Release mode"
    );
}

void test_build_tree_layout_nests_project_and_probe_profiles() {
    temp_dir root;

    const std::vector<std::pair<std::string, std::string>> project_cases {
        { "debug", ".ecosystem/build/project/desktop/debug/default" },
        { "release", ".ecosystem/build/project/desktop/release/default" },
        { "kde", ".ecosystem/build/project/desktop/debug/kde" },
        { "coverage", ".ecosystem/build/project/desktop/debug/coverage" },
        { "leaks", ".ecosystem/build/project/desktop/debug/leaks" },
        { "android", ".ecosystem/build/project/android/debug/default" },
    };

    for (const auto& [profile, expected_relative_path] : project_cases) {
        const fs::path expected_path = root.path() / expected_relative_path;
        require_true(
            ecosystem::local_build_dir(root.path(), profile) == expected_path,
            "project build layout must map profile `" + profile
                + "` to the nested reusable tree"
        );
        require_true(
            ecosystem::local_build_cache_path(root.path(), profile)
                == expected_path / "CMakeCache.txt",
            "build cache path must stay inside the nested build tree for `"
                + profile + "`"
        );
    }

    require_true(
        ecosystem::local_doctor_dir(root.path())
            == root.path() / ".ecosystem/build/probes",
        "doctor probe scope must live under the nested build root"
    );
}

void test_render_benchmark_svg_uses_template_backed_surface() {
    ecosystem::benchmark_summary summary;
    summary.series = {
        {
            "native<fast>",
            {
                { 32, 1.5 },
                { 64, 3.0 },
            },
        },
        {
            "transpose & swap",
            {
                { 32, 1.2 },
                { 64, 2.4 },
            },
        },
    };

    std::string error_message;
    const std::string svg = ecosystem::render_benchmark_svg(
        summary, "sample <plot> & run", &error_message
    );
    require_true(
        error_message.empty(),
        "benchmark SVG rendering must succeed with shared templates"
    );
    require_contains(
        svg, "sample &lt;plot&gt; &amp; run",
        "benchmark SVG rendering must escape the plot title"
    );
    require_contains(
        svg, "native&lt;fast&gt;",
        "benchmark SVG rendering must escape legend labels"
    );
    require_contains(
        svg, "transpose &amp; swap",
        "benchmark SVG rendering must preserve all series labels"
    );
    require_contains(
        svg, "<polyline",
        "benchmark SVG rendering must keep the plotted series line"
    );
    require_contains(
        svg, "<circle", "benchmark SVG rendering must keep the point markers"
    );
}

void test_cli_format_shares_verifier_selection_and_tool() {
    temp_dir root;
    const auto real_formatter = ecosystem::find_command_path("clang-format");
    require_true(
        !real_formatter.empty(), "format acceptance requires clang-format"
    );
    const auto project = root.path() / "project with spaces";
    const json authored
        = { { "id", "format_sample" },
            { "description", "Format selection" },
            { "facade", "core:app" },
            { "artifacts",
              json::array(
                  { { { "id", "core:lib" },
                      { "kind", "static_lib" },
                      { "owns",
                        json::array(
                            { "math", "tests/lib_tests.cpp",
                              "benchmarks/lib_bench.cpp" }
                        ) } },
                    { { "id", "core:app" },
                      { "kind", "exe" },
                      { "owns", json::array() },
                      { "entry", "src/main.cpp" },
                      { "dependencies", json::array({ "core:lib" }) } } }
              ) } };
    write_text(project / "manifest.json", authored.dump(2));
    const std::string unformatted = "int helper(){return 2;}\n";
    const std::vector<fs::path> selected {
        project / "include/math.hpp", project / "src/math.cpp",
        project / "tests/lib_tests.cpp", project / "benchmarks/lib_bench.cpp"
    };
    for (const auto& file : selected)
        write_text(file, unformatted);
    write_text(project / "src/main.cpp", "int main(){return 0;}\n");
    write_text(project / "src/unowned.cpp", unformatted);
    write_text(project / "build/generated.cpp", unformatted);
    const std::string style = "BasedOnStyle: LLVM\nIndentWidth: 2\n";
    write_text(project / ".clang-format", style);
    write_text(
        project / "tests/.clang-format",
        "BasedOnStyle: LLVM\nIndentWidth: 4\nAllowShortFunctionsOnASingleLine: "
        "None\n"
    );
    const auto bin = root.path() / "bin";
    const auto log = root.path() / "formatter.log";
    write_executable_script(
        bin / "clang-format",
        "#!/bin/sh\n"
        "if [ \"$1\" != '--version' ]; then\n"
        "  printf 'CALL\\n' >> \"$MANIFESTO_TEST_FORMAT_LOG\"\n"
        "  printf '%s\\n' \"$@\" >> \"$MANIFESTO_TEST_FORMAT_LOG\"\n"
        "fi\n"
        "exec \"$MANIFESTO_TEST_REAL_FORMATTER\" \"$@\"\n"
    );
    scoped_env tool("MANIFESTO_TEST_REAL_FORMATTER", real_formatter);
    scoped_env output("MANIFESTO_TEST_FORMAT_LOG", log.string());
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    auto verify = run_engels_cli(project, "check format core:lib");
    require_true(verify.exit_code != 0, "format drift must fail verification");
    const auto check_call = read_text(log);
    for (const auto& file : selected)
        require_true(
            read_text(file) == unformatted,
            "verification must not write source files"
        );
    write_text(log, "");
    auto formatted = run_marx_cli(project, "format core:lib");
    require_true(
        formatted.exit_code == 0,
        "canonical formatter must succeed:\n" + formatted.output
    );
    const auto format_call = read_text(log);
    require_contains(
        check_call, "CALL\n--dry-run\n--Werror\n-style=file\n",
        "verification must retain strict formatter flags"
    );
    require_contains(
        format_call, "CALL\n-i\n-style=file\n",
        "Marx must use the same formatter configuration"
    );
    const auto config_end = std::string("-style=file\n");
    require_true(
        check_call.substr(check_call.find(config_end) + config_end.size())
            == format_call.substr(
                format_call.find(config_end) + config_end.size()
            ),
        "formatter and verifier must receive exactly the same files"
    );
    for (const auto& file : selected) {
        require_contains(
            format_call, file.string(),
            "format selection must include every owned file kind"
        );
        require_true(
            read_text(file) != unformatted,
            "selected files must actually be formatted"
        );
    }
    require_contains(
        read_text(project / "tests/lib_tests.cpp"), "    return 2;",
        "formatting must honor the selected file's nested configuration"
    );
    for (const auto& file :
         { project / "src/unowned.cpp", project / "build/generated.cpp" })
        require_true(
            read_text(file) == unformatted,
            "unowned/generated files must stay outside formatting"
        );
    require_true(
        read_text(project / "src/main.cpp") == "int main(){return 0;}\n",
        "artifact formatting must not widen to another owner or dependency"
    );
    require_true(
        read_text(project / ".clang-format") == style
            && read_text(project / "manifest.json") == authored.dump(2),
        "format must preserve authored intent and configuration"
    );
    require_true(
        run_engels_cli(project, "check format core:lib").exit_code == 0,
        "format must repair the same verifier scope"
    );
    require_true(
        run_engels_cli(project, "check format core:app").exit_code != 0,
        "unselected drift must remain visible in its own scope"
    );
    formatted = run_marx_cli(project, "--quiet format core:app");
    require_true(
        formatted.exit_code == 0 && formatted.output.empty(),
        "formatter must support the common quiet front door"
    );
    require_true(
        run_engels_cli(project, "check format").exit_code == 0,
        "whole-project verification must pass after both scopes are fixed"
    );
    const auto before_invalid = read_text(log);
    require_true(
        run_marx_cli(project, "format ghost:lib").exit_code != 0
            && run_engels_cli(project, "check format ghost:lib").exit_code != 0,
        "unknown format scopes must fail"
    );
    require_true(
        read_text(log) == before_invalid,
        "unknown scopes must fail before tool execution"
    );
    require_contains(
        run_engels_cli(project, "format").output, "available via marx",
        "format ownership must be explicit"
    );
    const auto empty_bin = root.path() / "empty-bin";
    fs::create_directory(empty_bin);
    {
        scoped_env missing("PATH", empty_bin.string());
        require_true(
            run_marx_cli(project, "format").exit_code == 4
                && run_engels_cli(project, "check format").exit_code == 4,
            "formatter and verifier must report the same missing tool"
        );
    }
    write_text(project / ".clang-format", "UnknownStyleKey: true\n");
    require_true(
        run_marx_cli(project, "format core:app").exit_code != 0
            && run_engels_cli(project, "check format core:app").exit_code != 0,
        "invalid formatter policy must fail both operations"
    );
}

void test_cli_workspace_format_respects_selected_projects_and_artifacts() {
    temp_dir root;
    write_sample_dual_run_workspace_project(root.path(), "alpha");
    write_sample_dual_run_workspace_project(root.path(), "beta");
    write_sample_dual_run_workspace_project(root.path(), "gamma");
    write_sample_workspace_config(root.path());
    const std::string unformatted = "int main(){return 0;}\n";
    for (const auto* name : { "alpha", "beta", "gamma" }) {
        write_text(
            root.path() / name / ".clang-format", "BasedOnStyle: LLVM\n"
        );
        write_text(root.path() / name / "src/main.cpp", unformatted);
        write_text(root.path() / name / "src/cli_main.cpp", unformatted);
    }
    write_text(root.path() / "broken/manifest.json", "invalid JSON\n");
    require_true(
        run_marx_cli(root.path(), "format").exit_code != 0,
        "invalid workspace state must fail before any formatting"
    );
    require_true(
        read_text(root.path() / "alpha/src/main.cpp") == unformatted,
        "workspace validity must be checked before source writes"
    );
    const auto selected
        = run_marx_cli(root.path(), "format alpha/app:app beta/tool:cli");
    require_true(
        selected.exit_code == 0,
        "qualified formatting must succeed:\n" + selected.output
    );
    require_true(
        run_engels_cli(root.path(), "check format alpha/app:app beta/tool:cli")
                .exit_code
            == 0,
        "verification must use the same multiple artifact scope"
    );
    require_true(
        read_text(root.path() / "alpha/src/cli_main.cpp") == unformatted
            && read_text(root.path() / "beta/src/main.cpp") == unformatted,
        "qualified formatting must preserve unselected artifact files"
    );
    require_true(
        run_marx_cli(root.path(), "format --group core").exit_code == 0,
        "group formatting must succeed"
    );
    require_true(
        run_engels_cli(root.path(), "check format --group core").exit_code == 0,
        "group verification must pass after formatting"
    );
    require_true(
        read_text(root.path() / "gamma/src/main.cpp") == unformatted,
        "group formatting must preserve excluded projects"
    );
    require_true(
        run_marx_cli(root.path(), "format --project gamma --project absent")
                .exit_code
            != 0,
        "invalid selectors must be preflighted as a complete request"
    );
    require_true(
        read_text(root.path() / "gamma/src/main.cpp") == unformatted,
        "invalid combined selectors must not partially format projects"
    );
    require_true(
        run_marx_cli(root.path(), "format --project gamma").exit_code == 0
            && run_engels_cli(root.path(), "check format --project gamma")
                    .exit_code
                == 0,
        "explicit project selection must share its scope with verification"
    );
}

void test_cli_benchmark_accepts_generic_programs_and_preserves_results() {
    temp_dir root;
    const json authored = { { "id", "generic_bench" },
                            { "description", "Generic benchmarks" },
                            { "facade", "app:app" },
                            { "artifacts",
                              json::array(
                                  { { { "id", "app:app" },
                                      { "kind", "exe" },
                                      { "owns", json::array() },
                                      { "entry", "src/main.cpp" } },
                                    { { "id", "a_b:c" },
                                      { "kind", "exe" },
                                      { "name", "first" },
                                      { "owns", json::array() },
                                      { "entry", "benchmarks/first.cpp" } },
                                    { { "id", "a:b_c" },
                                      { "kind", "exe" },
                                      { "name", "second" },
                                      { "owns", json::array() },
                                      { "entry", "benchmarks/second.cpp" } } }
                              ) } };
    write_text(root.path() / "manifest.json", authored.dump(2));
    write_text(root.path() / "src/main.cpp", "int main() { return 0; }\n");
    const std::string source = R"cpp(#include <cstdlib>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]).starts_with("--benchmark_")) return 91;
        std::cout << '[' << argv[i] << "]\n";
    }
    const char* env = std::getenv("MANIFESTO_TEST_BENCHMARK_MODE");
    const std::string mode = env ? env : "";
    if (mode == "flops") std::cout << "generic/n:32_mean 1 ns FLOPs=1.5G/s\n";
    else if (mode == "malformed") std::cout
        << "generic/n:999999999999999999999_mean FLOPs=2G/s\n"
        << "generic/n:32_mean FLOPs=1e9999G/s\n"
        << "generic/n:64_mean FLOPs=1e+G/s\n"
        << "generic/n:128_mean FLOPs=-3G/s\n";
    else if (mode == "fail") { std::cerr << "intentional benchmark failure\n"; return 23; }
    else std::cout << "latency_ns=3\n";
    return 0;
}
)cpp";
    write_text(root.path() / "benchmarks/first.cpp", source);
    write_text(root.path() / "benchmarks/second.cpp", source);
    const auto reports = root.path() / ".ecosystem/reports/benchmark";
    const auto report = reports / "a_b/c";
    auto run = [&](const std::string& mode, const std::string& args) {
        scoped_env env("MANIFESTO_TEST_BENCHMARK_MODE", mode);
        return run_marx_cli(root.path(), "benchmark " + args);
    };
    auto result = run("", "a_b:c -- 'two words' --then marker");
    require_true(
        result.exit_code == 0,
        "a generic benchmark must accept only user arguments:\n" + result.output
    );
    require_contains(
        read_text(report / "bench_log.txt"), "[two words]\n[--then]\n[marker]",
        "benchmark argv must preserve spaces and batch separators"
    );
    auto metadata = json::parse(read_text(report / "result.json"));
    require_true(
        metadata.at("artifact") == "a_b:c" && metadata.at("status") == "passed"
            && metadata.at("exit_code") == 0,
        "generic results must record artifact and process status"
    );
    const auto argv = metadata.at("command").get<std::vector<std::string>>();
    require_true(
        argv.size() == 4 && argv[1] == "two words" && argv[2] == "--then"
            && argv[3] == "marker",
        "result metadata must preserve the exact command"
    );
    require_true(
        !fs::exists(report / "summary.json"),
        "generic results must not require FLOPs summaries"
    );
    for (const std::string mode : { "malformed", "fail" }) {
        require_true(
            run("flops", "a_b:c").exit_code == 0
                && fs::exists(report / "bench_plot.svg"),
            "recognized counters must retain optional plots"
        );
        result = run(mode, "a_b:c");
        metadata = json::parse(read_text(report / "result.json"));
        require_true(
            !fs::exists(report / "summary.json")
                && !fs::exists(report / "bench_plot.svg"),
            "new executions must retire stale optional reports"
        );
        if (mode == "malformed") {
            require_true(
                result.exit_code == 0 && metadata.at("status") == "passed",
                "malformed optional counters must not fail successful execution"
            );
        } else {
            require_true(
                result.exit_code != 0 && metadata.at("status") == "failed"
                    && metadata.at("exit_code") == 23,
                "process failures must be preserved in generic results"
            );
            require_contains(
                result.output, "a_b:c (exit 23)",
                "execution failure must identify the artifact and exit status"
            );
            require_contains(
                result.output, "benchmark log:",
                "execution failure must expose its captured log path"
            );
            require_contains(
                read_text(report / "bench_log.txt"),
                "intentional benchmark failure",
                "failed process output must remain available"
            );
        }
    }
    const auto overrides = root.path() / "templates";
    write_text(
        overrides / "benchmark/plot.svg.tpl", "{{missing_optional_binding}}\n"
    );
    {
        scoped_env env("MANIFESTO_TEMPLATE_ROOT", overrides.string());
        result = run("flops", "a_b:c");
        require_true(
            result.exit_code == 0,
            "optional plot errors must not fail benchmark execution"
        );
        require_contains(
            result.output, "warning: optional benchmark report skipped:",
            "optional report failures must be visible"
        );
        require_contains(
            result.output, "benchmark/plot.svg.tpl",
            "optional report warning must name its template"
        );
        require_true(
            !fs::exists(report / "summary.json")
                && !fs::exists(report / "bench_plot.svg"),
            "failed optional reports must not leave partial results"
        );
    }
    const auto first_metadata = read_text(report / "result.json");
    require_true(
        run("", "a:b_c").exit_code == 0, "second generic benchmark must run"
    );
    require_true(
        read_text(report / "result.json") == first_metadata
            && fs::exists(reports / "a/b_c/result.json"),
        "artifact identities with underscores must keep separate results"
    );
    write_text(report / "bench_plot.svg/obstruction", "keep\n");
    result = run("flops", "a_b:c");
    require_true(
        result.exit_code == 0,
        "optional cleanup errors must not block execution"
    );
    require_contains(
        result.output, "warning: unable to retire optional benchmark report",
        "optional cleanup failures must be visible"
    );
    require_true(
        read_text(report / "bench_plot.svg/obstruction") == "keep\n",
        "optional cleanup must not recursively remove an unexpected directory"
    );
    fs::rename(report / "result.json", report / "previous-result.json");
    fs::create_directory(report / "result.json");
    result = run("", "a_b:c");
    require_true(
        result.exit_code != 0,
        "core result persistence errors must fail the operation"
    );
    require_contains(
        result.output, "result.json",
        "result-write errors must identify the path"
    );
    require_true(
        run("", "").exit_code != 0, "ambiguous benchmark selection must fail"
    );
    require_true(
        run("", "app:app").exit_code != 0,
        "ordinary applications must not silently become benchmarks"
    );
}

void test_cli_benchmark_builds_release_benchmarks_and_writes_reports() {
    temp_dir root;
    write_sample_dependency_project(root.path());

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path cmake_log = fake_root / "cmake.log";
    fs::create_directories(fake_bin);

    write_executable_script(
        fake_bin / "cmake",
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "printf '%s\\n' \"$*\" >> \"$FAKE_CMAKE_LOG\"\n"
        "if [ \"${1:-}\" = \"--version\" ]; then\n"
        "  printf 'cmake version 3.30.0\\n'\n"
        "  exit 0\n"
        "fi\n"
        "if [ \"${1:-}\" = \"--build\" ]; then\n"
        "  build_dir=\"$2\"\n"
        "  target=\"\"\n"
        "  shift 2\n"
        "  while [ $# -gt 0 ]; do\n"
        "    if [ \"$1\" = \"--target\" ]; then\n"
        "      target=\"$2\"\n"
        "      shift 2\n"
        "      continue\n"
        "    fi\n"
        "    shift\n"
        "  done\n"
        "  mkdir -p \"$build_dir\"\n"
        "  if [ -n \"$target\" ]; then\n"
        "    output_name=\"${target##*__}\"\n"
        "    cat > \"$build_dir/$output_name\" <<'EOF'\n"
        "#!/usr/bin/env bash\n"
        "cat <<'REPORT'\n"
        "bench_dense_matrix/native/n:32_mean 1 ns 1 ns 1 FLOPs=1.5G/s\n"
        "bench_dense_matrix/native/n:64_mean 1 ns 1 ns 1 FLOPs=3.0G/s\n"
        "bench_dense_matrix/transpose/n:32_mean 1 ns 1 ns 1 FLOPs=1.2G/s\n"
        "bench_dense_matrix/transpose/n:64_mean 1 ns 1 ns 1 FLOPs=2.4G/s\n"
        "REPORT\n"
        "EOF\n"
        "    chmod +x \"$build_dir/$output_name\"\n"
        "  fi\n"
        "  exit 0\n"
        "fi\n"
        "build_dir=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  if [ \"$1\" = \"-B\" ]; then\n"
        "    build_dir=\"$2\"\n"
        "    shift 2\n"
        "    continue\n"
        "  fi\n"
        "  shift\n"
        "done\n"
        "if [ -n \"$build_dir\" ]; then\n"
        "  mkdir -p \"$build_dir\"\n"
        "fi\n"
        "exit 0\n"
    );
    write_executable_script(
        fake_bin / "clang++", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_executable_script(
        fake_bin / "clang", "#!/usr/bin/env bash\nexit 0\n"
    );

    scoped_env path_env("PATH", fake_bin.string() + ":" + current_path_env());
    scoped_env cmake_log_env("FAKE_CMAKE_LOG", cmake_log.string());

    const cli_result result = run_cli(root.path(), "benchmark");
    require_true(
        result.exit_code == 0,
        "ecos benchmark must succeed for a sample benchmark artifact"
    );
    require_contains(
        result.output, "benchmarked benchmarks:bench",
        "ecos benchmark must report the selected benchmark artifact"
    );
    require_contains(
        result.output,
        "benchmark plot: "
        ".ecosystem/reports/benchmark/benchmarks/bench/bench_plot.svg",
        "ecos benchmark must report the generated plot path"
    );
    require_contains(
        read_text(cmake_log), "-DCMAKE_BUILD_TYPE=Release",
        "ecos benchmark must configure CMake in Release mode"
    );
    require_contains(
        read_text(cmake_log), "-DECOSYSTEM_BUILD_BENCHMARKS=ON",
        "ecos benchmark must enable benchmark components in the "
        "developer surface"
    );

    const fs::path report_root = root.path() / ".ecosystem" / "reports"
        / "benchmark" / "benchmarks" / "bench";
    require_contains(
        read_text(report_root / "bench_log.txt"),
        "bench_dense_matrix/native/n:32_mean",
        "ecos benchmark must persist the raw benchmark log"
    );
    require_contains(
        read_text(report_root / "summary.json"), "\"algorithm\": \"native\"",
        "ecos benchmark must emit a machine-readable benchmark summary"
    );
    require_contains(
        read_text(report_root / "summary.json"), "\"algorithm\": \"transpose\"",
        "ecos benchmark must include each parsed benchmark series "
        "in the summary"
    );
    require_contains(
        read_text(report_root / "bench_plot.svg"), "<svg",
        "ecos benchmark must render an SVG plot without external "
        "plotting scripts"
    );
    require_contains(
        read_text(report_root / "bench_plot.svg"), "GFLOPs/s",
        "ecos benchmark plot must label the throughput axis"
    );
}

void test_cli_prerelease_builds_shareable_repos_and_auto_increments_version() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path cmake_log = fake_root / "cmake.log";
    fs::create_directories(fake_bin);

    write_fake_configure_and_build_cmake(fake_bin / "cmake");
    write_executable_script(
        fake_bin / "clang++", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_executable_script(
        fake_bin / "clang", "#!/usr/bin/env bash\nexit 0\n"
    );

    scoped_env path_env("PATH", fake_bin.string() + ":" + current_path_env());
    scoped_env cmake_log_env("FAKE_CMAKE_LOG", cmake_log.string());

    const cli_result first_result
        = run_cli(root.path(), "prerelease --version-base 1.2.3");
    require_true(
        first_result.exit_code == 0,
        "ecos prerelease must succeed for a runnable sample"
    );
    require_contains(
        first_result.output, "version 1.2.3-pre.1",
        "ecos prerelease must report the first prerelease version"
    );
    require_contains(
        read_text(cmake_log), "-DCMAKE_BUILD_TYPE=Release",
        "ecos prerelease must build with the release CMake profile"
    );

    const fs::path prerelease_root = root.path() / ".ecosystem" / "prerelease";
    const fs::path deb_repo = prerelease_root / "deb";
    const fs::path deb_packages_path
        = first_recursive_file_named(deb_repo, "Packages");
    const fs::path deb_packages_gz_path
        = first_recursive_file_named(deb_repo, "Packages.gz");
    const fs::path deb_release_path
        = first_recursive_file_named(deb_repo, "Release");
    require_true(
        !deb_packages_path.empty(),
        "ecos prerelease must write a Debian Packages index"
    );
    require_true(
        !deb_packages_gz_path.empty(),
        "ecos prerelease must write a compressed Debian Packages index"
    );
    require_true(
        !deb_release_path.empty(),
        "ecos prerelease must write Debian Release metadata"
    );
    require_contains(
        read_text(deb_packages_path), "Version: 1.2.3~pre.1",
        "ecos prerelease must publish the Debian prerelease version"
    );
    require_contains(
        read_text(deb_release_path), "Suite: prerelease",
        "ecos prerelease must describe the Debian prerelease suite"
    );
    require_contains(
        read_text(deb_packages_path), "Filename: pool/main/",
        "ecos prerelease must index Debian packages through the pool layout"
    );

    const fs::path deb_package
        = first_recursive_file_with_suffix(deb_repo, ".deb");
    require_true(
        !deb_package.empty(),
        "ecos prerelease must emit a shareable Debian package"
    );
    require_contains(
        deb_package.generic_string(), "/pool/main/",
        "ecos prerelease must place Debian packages inside the pool layout"
    );

    const fs::path pacman_package = first_recursive_file_with_suffix(
        prerelease_root / "pacman", ".pkg.tar.gz"
    );
    require_true(
        !pacman_package.empty(),
        "ecos prerelease must emit a shareable Pacman package"
    );
    const fs::path pacman_db = first_recursive_file_named(
        prerelease_root / "pacman", "sample-prerelease.db"
    );
    require_true(
        !pacman_db.empty(),
        "ecos prerelease must emit a Pacman repository database"
    );
    const fs::path pacman_files = first_recursive_file_named(
        prerelease_root / "pacman", "sample-prerelease.files"
    );
    require_true(
        !pacman_files.empty(),
        "ecos prerelease must emit a Pacman file list database"
    );
    require_contains(
        read_text(prerelease_root / "state.json"), "\"next_prerelease\": 2",
        "ecos prerelease must persist the next prerelease counter "
        "after the first run"
    );

    const cli_result second_result = run_cli(root.path(), "prerelease");
    require_true(
        second_result.exit_code == 0,
        "ecos prerelease must reuse the stored base version on later runs"
    );
    require_contains(
        second_result.output, "version 1.2.3-pre.2",
        "ecos prerelease must auto-increment the prerelease number"
    );
    require_contains(
        read_text(deb_packages_path), "Version: 1.2.3~pre.2",
        "ecos prerelease must refresh the Debian repository index "
        "to the latest prerelease"
    );
    require_contains(
        read_text(prerelease_root / "state.json"), "\"next_prerelease\": 3",
        "ecos prerelease must advance the stored prerelease counter "
        "after the second run"
    );
}

void test_prerelease_preserves_published_state_on_late_failures() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    const auto bin = root.path() / "fake-tools";
    write_fake_configure_and_build_cmake(bin / "cmake");
    write_fake_gpg_tool(bin / "working-gpg");
    const auto gzip = ecosystem::find_command_path("gzip");
    for (const std::string name : { "gzip", "gpg" }) {
        const auto real = std::string(name) == "gzip"
            ? gzip
            : (bin / "working-gpg").string();
        write_executable_script(
            bin / name,
            "#!/bin/sh\n"
            "if [ \"$1\" != '--version' ] && [ "
            "\"$MANIFESTO_FAIL_RELEASE_TOOL\" = '"
                + name + "' ] && { [ '" + name
                + "' != gzip ] || [ \"$1\" = '-n' ]; }; then\n"
                  "  printf 'injected release tool failure\\n' >&2\n  exit "
                  "9\nfi\n"
                  "'"
                + real
                + "' \"$@\" || exit $?\n"
                  "if [ \"$MANIFESTO_FAIL_RELEASE_TOOL\" = cutover ]; then\n"
                  "  for arg do\n"
                  "    case \"$arg\" in *-prerelease.files)\n"
                  "      repo=$(dirname \"$(dirname \"$arg\")\")\n"
                  "      mv \"$repo\" \"$repo-interrupted\" || exit $?;;\n"
                  "    esac\n"
                  "  done\nfi\n"
        );
    }
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    scoped_env cmake_log(
        "FAKE_CMAKE_LOG", (root.path() / "cmake.log").string()
    );
    scoped_env gpg_log("FAKE_GPG_LOG", (root.path() / "gpg.log").string());
    const auto release = ecosystem::local_prerelease_dir(root.path());
    const auto snapshot = [&]() {
        std::map<std::string, std::string> files;
        for (const auto& entry : { "deb", "pacman", "state.json" }) {
            const auto path = release / entry;
            if (!fs::exists(path))
                continue;
            if (fs::is_regular_file(path))
                files.emplace(entry, read_text(path));
            else
                for (const auto& file :
                     fs::recursive_directory_iterator(path)) {
                    if (file.is_regular_file())
                        files.emplace(
                            file.path()
                                .lexically_relative(release)
                                .generic_string(),
                            read_text(file.path())
                        );
                }
        }
        return files;
    };
    {
        scoped_env failure("MANIFESTO_FAIL_RELEASE_TOOL", "gpg");
        const auto result = run_marx_cli(
            root.path(), "prerelease --version-base 1.2.3 --sign"
        );
        require_true(
            result.exit_code == 5,
            "first failed signing must fail the release: " + result.output
        );
        require_true(
            snapshot().empty(),
            "a failed first release must publish no packages, metadata or "
            "counter"
        );
    }
    auto result
        = run_marx_cli(root.path(), "prerelease --version-base 1.2.3 --sign");
    require_true(
        result.exit_code == 0,
        "initial signed release must succeed: " + result.output
    );
    const auto previous = snapshot();
    require_true(
        !previous.empty(), "successful publication must retain its payload"
    );
    const auto compressed
        = first_recursive_file_named(release / "deb", "Packages.gz");
    require_true(
        ecosystem::capture_command_result({ gzip, "-t", compressed.string() })
                .exit_code
            == 0,
        "Debian index must be a valid binary gzip stream"
    );
    require_true(
        ecosystem::capture_command(
            { gzip, "-d", "-c", compressed.string() }
        ) == read_text(first_recursive_file_named(release / "deb", "Packages")),
        "compressed index must preserve all metadata bytes"
    );
    for (const std::string tool : { "gzip", "gpg", "cutover" }) {
        scoped_env failure("MANIFESTO_FAIL_RELEASE_TOOL", tool);
        result = run_marx_cli(root.path(), "prerelease --sign");
        require_true(
            result.exit_code == 5,
            "late tool failure must fail the release: " + result.output
        );
        require_contains(
            result.output,
            tool == "cutover" ? "unable to publish release"
                              : "injected release tool failure",
            "native failure output must survive"
        );
        require_true(
            snapshot() == previous,
            "late failures must preserve all published bytes, signatures and "
            "version state"
        );
    }
    result = run_marx_cli(root.path(), "prerelease --sign");
    require_true(result.exit_code == 0, "retry must succeed: " + result.output);
    require_contains(
        result.output, "version 1.2.3-pre.2",
        "failed attempts must not consume versions"
    );
    require_true(
        !fs::exists(release / "work/publication/previous"),
        "successful cutover must retire backup state"
    );
}

void test_prerelease_retains_recovery_state_and_rejects_publication_aliases() {
    temp_dir root;
    auto value = sample_dual_run_manifest();
    const auto binary = root.path() / "app";
    write_text(binary, "payload");
    const auto resolved = ecosystem::resolve_artifact(
        value, ecosystem::artifact_ref { "app", "app" }
    );
    require_true(resolved.has_value(), "primary must resolve");
    ecosystem::prerelease_version version;
    ecosystem::prerelease_artifacts artifacts;
    std::string error;
    const auto release = ecosystem::local_prerelease_dir(root.path());
    const auto recovery = release / "work/publication/previous";
    write_text(recovery / "state.json", "recovery marker");
    auto result = ecosystem::create_prerelease_packages(
        root.path(), value, *resolved, binary, "1.2.3", {}, &version,
        &artifacts, &error
    );
    require_true(
        result == ecosystem::command_error::task_failed,
        "pending recovery must stop a new attempt"
    );
    require_contains(
        error, "awaits recovery",
        "failure must locate recoverable release state"
    );
    require_true(
        read_text(recovery / "state.json") == "recovery marker",
        "retry must not discard backups"
    );
    fs::rename(recovery, root.path() / "saved-recovery");
    const auto outside = root.path() / "external";
    write_text(outside / "Packages", "external marker");
    fs::create_directories(release / "deb");
    fs::create_directory_symlink(outside, release / "deb/alias");
    result = ecosystem::create_prerelease_packages(
        root.path(), value, *resolved, binary, "1.2.3", {}, &version,
        &artifacts, &error
    );
    require_true(
        result == ecosystem::command_error::task_failed,
        "publication aliases must fail before candidate writes"
    );
    require_contains(
        error, "unsupported release publication entry",
        "alias rejection must name its path"
    );
    require_true(
        read_text(outside / "Packages") == "external marker",
        "publication must not follow aliases"
    );
    fs::remove(release / "deb/alias");
    fs::rename(release / "work", root.path() / "saved-work");
    write_text(release / "deb/keep", "published marker");
    fs::create_directory_symlink(release / "deb", release / "work");
    result = ecosystem::create_prerelease_packages(
        root.path(), value, *resolved, binary, "1.2.3", {}, &version,
        &artifacts, &error
    );
    require_true(
        result == ecosystem::command_error::task_failed,
        "internally aliased work storage must not overlap published state"
    );
    require_contains(
        error, "release publication path is a symlink",
        "overlap rejection must identify the storage alias"
    );
    require_true(
        read_text(release / "deb/keep") == "published marker"
            && !fs::exists(release / "deb/publication"),
        "work preflight must leave the published tree untouched"
    );
}

void test_prerelease_rejects_colliding_install_payloads() {
    temp_dir root;
    auto value = sample_dual_run_manifest();
    value.install_artifacts = { "app:app", "tool:cli" };
    value.components.back().artifacts.front().name = "app";
    const fs::path primary = root.path() / "primary/app";
    const fs::path companion
        = ecosystem::local_build_dir(root.path(), "release") / "app";
    write_text(primary, "primary output");
    write_text(companion, "companion output");
    const auto resolved = ecosystem::resolve_artifact(
        value, ecosystem::artifact_ref { "app", "app" }
    );
    require_true(resolved.has_value(), "primary artifact must resolve");
    ecosystem::prerelease_version version;
    ecosystem::prerelease_artifacts packages;
    std::string error;
    const auto status = ecosystem::create_prerelease_packages(
        root.path(), value, *resolved, primary, "1.2.3", {}, &version,
        &packages, &error
    );
    require_true(
        status == ecosystem::command_error::task_failed,
        "duplicate installed filenames must fail packaging"
    );
    require_contains(
        error,
        "package payload collision:", "collision must identify its destination"
    );
    require_contains(
        error, "tool:cli", "collision must identify the companion artifact"
    );
    const fs::path release = ecosystem::local_prerelease_dir(root.path());
    const fs::path staged = first_recursive_file_named(release / "work", "app");
    require_true(
        !staged.empty() && read_text(staged) == "primary output",
        "the first payload must not be overwritten"
    );
    require_true(
        !fs::exists(release / "state.json"),
        "collision must not advance release state"
    );
    require_true(
        !fs::exists(release / "deb") && !fs::exists(release / "pacman"),
        "collision must fail before publishing packages"
    );
}

void test_cli_prerelease_includes_install_companions() {
    temp_dir root;
    write_sample_dual_run_project(root.path());
    auto value = sample_dual_run_manifest();
    value.install_artifacts = { "app:app", "tool:cli" };
    std::string error;
    require_true(
        ecosystem::save_manifest(root.path() / "manifest.json", value, &error),
        error
    );
    const fs::path fake_bin = root.path() / "fake-tools";
    const fs::path log = root.path() / "cmake.log";
    write_fake_configure_and_build_cmake(fake_bin / "cmake");
    scoped_env path_env("PATH", fake_bin.string() + ":" + current_path_env());
    scoped_env log_env("FAKE_CMAKE_LOG", log.string());
    const auto result
        = run_marx_cli(root.path(), "prerelease --version-base 1.2.3");
    require_true(
        result.exit_code == 0,
        "public companion packaging must succeed: " + result.output
    );
    const fs::path work = root.path() / ".ecosystem/prerelease";
    require_true(
        !first_recursive_file_named(work, "app").empty()
            && !first_recursive_file_named(work, "cli").empty(),
        "package payload must retain both independently built install artifacts"
    );
    require_contains(
        read_text(log), "--target tool__cli",
        "companion must be built before packaging"
    );
}

void test_cli_prerelease_signs_repo_metadata_when_requested() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path cmake_log = fake_root / "cmake.log";
    const fs::path gpg_log = fake_root / "gpg.log";
    fs::create_directories(fake_bin);

    write_fake_configure_and_build_cmake(fake_bin / "cmake");
    write_fake_gpg_tool(fake_bin / "gpg");
    write_executable_script(
        fake_bin / "clang++", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_executable_script(
        fake_bin / "clang", "#!/usr/bin/env bash\nexit 0\n"
    );

    scoped_env path_env("PATH", fake_bin.string() + ":" + current_path_env());
    scoped_env cmake_log_env("FAKE_CMAKE_LOG", cmake_log.string());
    scoped_env gpg_log_env("FAKE_GPG_LOG", gpg_log.string());

    const cli_result result = run_cli(
        root.path(),
        "prerelease --version-base 1.2.3 --sign --sign-key prerelease-key"
    );
    require_true(
        result.exit_code == 0,
        "ecos prerelease --sign must succeed with gpg available"
    );
    require_contains(
        result.output, "signed prerelease metadata",
        "ecos prerelease --sign must report that signing ran"
    );

    const fs::path prerelease_root = root.path() / ".ecosystem" / "prerelease";
    const fs::path deb_repo = prerelease_root / "deb";
    const fs::path inrelease_path
        = first_recursive_file_named(deb_repo, "InRelease");
    const fs::path release_gpg_path
        = first_recursive_file_named(deb_repo, "Release.gpg");
    require_true(
        !inrelease_path.empty(),
        "ecos prerelease --sign must write a clearsigned Debian InRelease file"
    );
    require_true(
        !release_gpg_path.empty(),
        "ecos prerelease --sign must write a detached Debian Release signature"
    );

    const fs::path pacman_db = first_recursive_file_named(
        prerelease_root / "pacman", "sample-prerelease.db"
    );
    require_true(
        !pacman_db.empty(),
        "ecos prerelease --sign must still emit the Pacman repo database"
    );
    require_true(
        fs::exists(fs::path(pacman_db.string() + ".sig")),
        "ecos prerelease --sign must sign the Pacman repo database"
    );
    const fs::path pacman_files = first_recursive_file_named(
        prerelease_root / "pacman", "sample-prerelease.files"
    );
    require_true(
        !pacman_files.empty(),
        "ecos prerelease --sign must emit the Pacman file list database"
    );
    require_true(
        fs::exists(fs::path(pacman_files.string() + ".sig")),
        "ecos prerelease --sign must sign the Pacman file list database"
    );

    const fs::path pacman_package = first_recursive_file_with_suffix(
        prerelease_root / "pacman", ".pkg.tar.gz"
    );
    require_true(
        !pacman_package.empty(),
        "ecos prerelease --sign must still emit the Pacman package"
    );
    require_true(
        fs::exists(fs::path(pacman_package.string() + ".sig")),
        "ecos prerelease --sign must sign the Pacman package"
    );

    require_contains(
        read_text(gpg_log), "--clearsign",
        "ecos prerelease --sign must clear-sign Debian Release metadata"
    );
    require_contains(
        read_text(gpg_log), "--detach-sign",
        "ecos prerelease --sign must use detached signatures for repo artifacts"
    );
    require_contains(
        read_text(gpg_log), "key=prerelease-key",
        "ecos prerelease --sign-key must forward the selected signing key"
    );
}

void test_cli_check_java_builds_shared_libraries_and_runs_gradle_tests() {
    temp_dir root;
    write_sample_java_binding_project(root.path());

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path cmake_log = fake_root / "cmake.log";
    const fs::path gradle_log = fake_root / "gradle.log";
    fs::create_directories(fake_bin);

    write_executable_script(
        fake_bin / "cmake",
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "if [ \"$1\" = \"--build\" ]; then\n"
        "  build_dir=\"$2\"\n"
        "  target=\"\"\n"
        "  shift 2\n"
        "  while [ $# -gt 0 ]; do\n"
        "    if [ \"$1\" = \"--target\" ]; then\n"
        "      target=\"$2\"\n"
        "      shift 2\n"
        "      continue\n"
        "    fi\n"
        "    shift\n"
        "  done\n"
        "  mkdir -p \"$build_dir\"\n"
        "  printf '%s\\n' \"$target\" >> \"$FAKE_CMAKE_LOG\"\n"
        "  exit 0\n"
        "fi\n"
        "build_dir=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  if [ \"$1\" = \"-B\" ]; then\n"
        "    build_dir=\"$2\"\n"
        "    shift 2\n"
        "    continue\n"
        "  fi\n"
        "  shift\n"
        "done\n"
        "mkdir -p \"$build_dir\"\n"
        "exit 0\n"
    );
    write_executable_script(
        fake_bin / "gradle",
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "printf '%s\\n' \"$*\" >> \"$FAKE_GRADLE_LOG\"\n"
        "exit 0\n"
    );
    write_executable_script(fake_bin / "java", "#!/usr/bin/env bash\nexit 0\n");
    write_executable_script(
        fake_bin / "javac", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_executable_script(
        fake_bin / "clang++", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_executable_script(
        fake_bin / "clang", "#!/usr/bin/env bash\nexit 0\n"
    );

    const std::string original_path = []() {
        const char* value = std::getenv("PATH");
        return value == nullptr ? std::string() : std::string(value);
    }();
    scoped_env path_env("PATH", fake_bin.string() + ":" + original_path);
    scoped_env cmake_log_env("FAKE_CMAKE_LOG", cmake_log.string());
    scoped_env gradle_log_env("FAKE_GRADLE_LOG", gradle_log.string());

    const cli_result result = run_cli(root.path(), "check java");
    require_true(
        result.exit_code == 0,
        "ecos check java must run the Gradle-backed Java test workflow"
    );
    require_contains(
        result.output, "java tests passed",
        "ecos check java must report success after the Gradle test workflow"
    );
    require_contains(
        read_text(cmake_log), "api__api",
        "ecos check java must build the shared API library"
    );
    require_contains(
        read_text(cmake_log), "jni__jni",
        "ecos check java must build the JNI bridge library"
    );
    require_contains(
        read_text(gradle_log), "-PnativeLibraryPath=",
        "ecos check java must pass the native library path into Gradle"
    );
}

void test_cli_check_sphinx_generates_local_conf_with_rtd_theme() {
    temp_dir root;
    write_sample_sphinx_project(root.path());

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path sphinx_log = fake_root / "sphinx.log";
    fs::create_directories(fake_bin);

    write_executable_script(
        fake_bin / "sphinx-build",
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "printf 'theme=%s\\n' "
        "\"${MANIFESTO_SPHINX_THEME:-${ECOSYSTEM_SPHINX_THEME:-}}\" >> "
        "\"$FAKE_SPHINX_LOG\"\n"
        "printf '%s\\n' \"$*\" >> \"$FAKE_SPHINX_LOG\"\n"
        "build_dir=\"\"\n"
        "conf_dir=\"\"\n"
        "source_dir=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  case \"$1\" in\n"
        "    -b)\n"
        "      shift 2\n"
        "      ;;\n"
        "    -c)\n"
        "      conf_dir=\"$2\"\n"
        "      shift 2\n"
        "      ;;\n"
        "    *)\n"
        "      if [ -z \"$source_dir\" ]; then\n"
        "        source_dir=\"$1\"\n"
        "      elif [ -z \"$build_dir\" ]; then\n"
        "        build_dir=\"$1\"\n"
        "      fi\n"
        "      shift\n"
        "      ;;\n"
        "  esac\n"
        "done\n"
        "test -f \"$conf_dir/conf.py\"\n"
        "test -f \"$source_dir/index.md\"\n"
        "mkdir -p \"$build_dir\"\n"
        "exit 0\n"
    );

    const std::string original_path = []() {
        const char* value = std::getenv("PATH");
        return value == nullptr ? std::string() : std::string(value);
    }();
    scoped_env path_env("PATH", fake_bin.string() + ":" + original_path);
    scoped_env sphinx_log_env("FAKE_SPHINX_LOG", sphinx_log.string());

    const cli_result result = run_cli(root.path(), "check sphinx");
    require_true(
        result.exit_code == 0,
        "ecos check sphinx must build a conventional docs tree"
    );
    require_contains(
        result.output, "sphinx generated in .ecosystem/sphinx/html",
        "ecos check sphinx must report the generated html output path"
    );
    require_contains(
        read_text(root.path() / ".ecosystem/sphinx/conf.py"),
        "MANIFESTO_SPHINX_THEME",
        "ecos check sphinx must default to the Read the Docs theme"
    );
    require_contains(
        read_text(root.path() / ".ecosystem/sphinx/conf.py"), "'myst_parser'",
        "ecos check sphinx must enable MyST when markdown docs are present"
    );
    require_contains(
        read_text(sphinx_log), "-b html",
        "ecos check sphinx must invoke sphinx-build in html mode"
    );
}

void test_cli_check_sphinx_theme_flag_sets_theme_override() {
    temp_dir root;
    write_sample_sphinx_project(root.path());

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path sphinx_log = fake_root / "sphinx.log";
    fs::create_directories(fake_bin);

    write_executable_script(
        fake_bin / "sphinx-build",
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "printf 'theme=%s\\n' "
        "\"${MANIFESTO_SPHINX_THEME:-${ECOSYSTEM_SPHINX_THEME:-}}\" >> "
        "\"$FAKE_SPHINX_LOG\"\n"
        "printf '%s\\n' \"$*\" >> \"$FAKE_SPHINX_LOG\"\n"
        "build_dir=\"\"\n"
        "conf_dir=\"\"\n"
        "source_dir=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  case \"$1\" in\n"
        "    -b)\n"
        "      shift 2\n"
        "      ;;\n"
        "    -c)\n"
        "      conf_dir=\"$2\"\n"
        "      shift 2\n"
        "      ;;\n"
        "    *)\n"
        "      if [ -z \"$source_dir\" ]; then\n"
        "        source_dir=\"$1\"\n"
        "      elif [ -z \"$build_dir\" ]; then\n"
        "        build_dir=\"$1\"\n"
        "      fi\n"
        "      shift\n"
        "      ;;\n"
        "  esac\n"
        "done\n"
        "test -f \"$conf_dir/conf.py\"\n"
        "test -f \"$source_dir/index.md\"\n"
        "mkdir -p \"$build_dir\"\n"
        "exit 0\n"
    );

    const std::string original_path = []() {
        const char* value = std::getenv("PATH");
        return value == nullptr ? std::string() : std::string(value);
    }();
    scoped_env path_env("PATH", fake_bin.string() + ":" + original_path);
    scoped_env sphinx_log_env("FAKE_SPHINX_LOG", sphinx_log.string());

    const cli_result result = run_cli(root.path(), "check sphinx --theme furo");
    require_true(
        result.exit_code == 0,
        "ecos check sphinx --theme must accept an explicit theme override"
    );
    require_contains(
        read_text(sphinx_log), "theme=furo",
        "ecos check sphinx --theme must forward the theme override "
        "into the Sphinx environment"
    );
}

void test_cli_check_repo_rejects_legacy_repository_entries() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    write_text(
        root.path() / "scripts/build.sh", "#!/usr/bin/env bash\nexit 0\n"
    );
    write_text(root.path() / "Makefile", "all:\n\t@true\n");
    write_text(root.path() / "cli/run.sh", "#!/usr/bin/env bash\nexit 0\n");

    const cli_result result = run_cli(root.path(), "check repo");
    require_true(
        result.exit_code != 0,
        "ecos check repo must fail on legacy repository scaffolding"
    );
    require_contains(
        result.output, "forbidden repository entries:",
        "ecos check repo must print the repository policy header"
    );
    require_contains(
        result.output, "scripts/: script directories are forbidden",
        "ecos check repo must report committed scripts directories"
    );
    require_contains(
        result.output, "Makefile: Makefiles are forbidden",
        "ecos check repo must report committed Makefiles"
    );
    require_contains(
        result.output, "cli/run.sh: committed shell wrappers are forbidden",
        "ecos check repo must report committed shell wrappers"
    );
}

void test_cli_check_repo_rejects_tracked_surface_drift() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");
    require_sync_success(
        root.path(),
        "sample project must sync before tracked-surface drift "
        "checks"
    );

    write_text(root.path() / ".github/workflows/tests.yml", "broken\n");

    const cli_result result = run_cli(root.path(), "check repo");
    require_true(
        result.exit_code != 0,
        "ecos check repo must fail on tracked-surface drift"
    );
    require_contains(
        result.output, "tracked surfaces out of sync:",
        "ecos check repo must report tracked-surface drift"
    );
    require_contains(
        result.output,
        ".github/workflows/tests.yml is out of sync with ecosystem "
        "defaults; run `marx sync`",
        "ecos check repo must report generated workflow drift"
    );
}

void test_cli_workspace_check_repo_rejects_gitlink_project_scripts() {
    if (!ecosystem::command_exists("git")) {
        return;
    }

    temp_dir root;
    const fs::path workspace_root = root.path() / "workspace";
    const fs::path project_root = workspace_root / "sample";
    write_sample_build_project(project_root, "sample");
    write_text(
        project_root / "scripts/build.sh", "#!/usr/bin/env bash\nexit 0\n"
    );

    require_true(
        ecosystem::run_command({ "git", "init", "-q" }, workspace_root) == 0,
        "workspace gitlink repository test must initialize a temporary git repo"
    );
    require_true(
        ecosystem::run_command(
            {
                "git",
                "update-index",
                "--add",
                "--cacheinfo",
                "160000,1111111111111111111111111111111111111111,sample",
            },
            workspace_root
        ) == 0,
        "workspace gitlink repository test must add a synthetic gitlink entry"
    );

    const cli_result result = run_cli(workspace_root, "check repo");
    require_true(
        result.exit_code != 0,
        "workspace ecos check repo must reject gitlink-backed managed "
        "projects with committed scripts"
    );
    require_contains(
        result.output, "scripts/: script directories are forbidden",
        "workspace ecos check repo must surface scripts committed "
        "inside gitlink-backed managed projects"
    );
}

void test_generated_builds_reject_compiler_warnings_in_c_and_cxx() {
    temp_dir root;
    write_text(
        root.path() / "manifest.json",
        json(
            { { "id", "warning_sample" },
              { "description", "Required compiler warnings" },
              { "facade", "sample:app" },
              { "artifacts",
                json::array(
                    { { { "id", "sample:app" },
                        { "kind", "exe" },
                        { "owns", json::array({ "helper" }) },
                        { "entry", "src/main.cpp" } } }
                ) } }
        ).dump(2)
    );
    write_text(
        root.path() / "src/main.cpp",
        "int main() { int unused_value = 42; return 0; }\n"
    );
    write_text(
        root.path() / "src/helper.c", "int helper(void) { return 0; }\n"
    );
    for (const auto* profile : { "debug", "release" }) {
        const auto result
            = run_marx_cli(root.path(), std::string("build ") + profile);
        require_true(
            result.exit_code == 5,
            "compiler warnings must fail local builds:\n" + result.output
        );
        require_contains(
            result.output, "unused_value",
            "build failure must retain the compiler diagnostic"
        );
    }
    require_true(
        run_marx_cli(root.path(), "sync").exit_code == 0,
        "warning fixture must generate its visitor surface"
    );
    const auto visitor = root.path() / "visitor";
    require_true(
        ecosystem::run_command(
            { "cmake", "-S", root.path().string(), "-B", visitor.string(),
              "-DCMAKE_C_COMPILER=gcc", "-DCMAKE_CXX_COMPILER=g++" },
            root.path()
        ) == 0,
        "GNU visitor fixture must configure"
    );
    const auto build_visitor = [&]() {
        return ecosystem::capture_command_result(
            { "cmake", "--build", visitor.string(), "--parallel", "2" },
            root.path()
        );
    };
    auto visitor_result = build_visitor();
    require_true(
        visitor_result.exit_code != 0,
        "visitor builds must enforce the same compiler contract"
    );
    require_contains(
        visitor_result.output, "unused_value",
        "GNU must report its selected warning"
    );
    write_text(root.path() / "src/main.cpp", "int main() { return 0; }\n");
    require_true(
        run_marx_cli(root.path(), "build debug").exit_code == 0
            && build_visitor().exit_code == 0,
        "clean C and C++ sources must build through both compilers"
    );
    write_text(
        root.path() / "src/helper.c",
        "int helper(void) { int unused_c_value = 42; return 0; }\n"
    );
    const auto native = run_marx_cli(root.path(), "build debug");
    visitor_result = build_visitor();
    require_true(
        native.exit_code == 5 && visitor_result.exit_code != 0,
        "C warnings must be hard errors without applying C++-only options"
    );
    require_contains(
        native.output, "unused_c_value",
        "Clang must retain C diagnostic attribution"
    );
    require_contains(
        visitor_result.output, "unused_c_value",
        "GNU must retain C diagnostic attribution"
    );
}

void test_cli_required_checks_propagate_failures_and_reject_empty_ctest_runs() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_sample_leak_check_project(project);
    require_true(
        run_marx_cli(project, "sync --then format").exit_code == 0,
        "aggregate check fixture must prepare canonical policy and formatting"
    );
    auto result = run_engels_cli(project, "check ci");
    require_true(
        result.exit_code == 0,
        "all required external checks must pass on clean inputs:\n"
            + result.output
    );
    require_contains(
        result.output, "ci checks passed",
        "aggregate success must follow all required stages"
    );
    const auto tests = project / "tests/main_tests.cpp";
    const auto clean_tests = read_text(tests);
    write_text(tests, "int main() { return 1; }\n");
    result = run_engels_cli(project, "check ci");
    require_true(
        result.exit_code == 5,
        "failing required tests must fail aggregate checks"
    );
    require_contains(
        result.output, "tests failed",
        "aggregate test failure must retain stage attribution"
    );
    require_not_contains(
        result.output,
        "tidy report:", "aggregate checks must stop at failed required tests"
    );
    write_text(tests, "int main() { int unused_test_value = 0; return 0; }\n");
    result = run_engels_cli(project, "check tests");
    require_true(
        result.exit_code == 5,
        "test compilation must also enforce compiler warnings"
    );
    require_contains(
        result.output, "unused_test_value",
        "test compiler errors must remain visible"
    );
    write_text(tests, clean_tests);

    const auto bin = root.path() / "bin";
    fs::create_directory(bin);
    write_fake_clang_tidy_tool(bin / "clang-tidy");
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    {
        scoped_env tidy_failure("FAKE_CLANG_TIDY_EXIT_CODE", "1");
        scoped_env tidy_output(
            "FAKE_CLANG_TIDY_OUTPUT", "required tidy fixture failure"
        );
        result = run_engels_cli(project, "check ci");
        require_true(
            result.exit_code == 5,
            "aggregate checks must propagate analyzer failure"
        );
        require_contains(
            result.output, "required tidy fixture failure",
            "aggregate failures must preserve tool output"
        );
        require_not_contains(
            result.output, "format check passed",
            "aggregate checks must stop at tidy failure"
        );
    }
    write_text(tests, "int main(){return 0;}\n");
    result = run_engels_cli(project, "check ci");
    require_true(
        result.exit_code == 5, "format drift must fail aggregate checks"
    );
    require_contains(
        result.output, "clang-format reported formatting drift",
        "format stage must remain a hard contract"
    );
    require_true(
        run_marx_cli(project, "format").exit_code == 0,
        "canonical formatting must repair predictable drift"
    );
    require_true(
        run_engels_cli(project, "check ci").exit_code == 0,
        "local repair must restore required-check success"
    );

    const auto ctest = ecosystem::find_command_path("ctest");
    scoped_env real_ctest("MANIFESTO_TEST_REAL_CTEST", ctest);
    write_executable_script(
        bin / "ctest",
        "#!/bin/sh\n"
        "if [ \"$1\" != '--version' ]; then\n"
        "  printf '# deliberately missing test registration\\n' > "
        "CTestTestfile.cmake\n"
        "fi\n"
        "exec \"$MANIFESTO_TEST_REAL_CTEST\" \"$@\"\n"
    );
    for (const auto* request :
         { "check tests", "check coverage", "check ci" }) {
        result = run_engels_cli(project, request);
        require_true(
            result.exit_code == 5,
            "no registered tests must fail required CTest execution:\n"
                + result.output
        );
        require_contains(
            result.output, "No tests were found",
            "real CTest must reject the empty selected test run"
        );
        require_not_contains(
            result.output, "ci checks passed",
            "an empty test run cannot satisfy aggregate acceptance"
        );
    }
}

void test_cli_check_ci_fails_fast_on_repository_policy_drift() {
    temp_dir root;
    write_sample_build_project(root.path(), "sample");

    write_text(
        root.path() / "scripts/build.sh", "#!/usr/bin/env bash\nexit 0\n"
    );

    const cli_result result = run_cli(root.path(), "check ci");
    require_true(
        result.exit_code != 0,
        "ecos check ci must fail when repository policy drift is present"
    );
    require_contains(
        result.output, "forbidden repository entries:",
        "ecos check ci must run repository policy checks before "
        "external tooling checks"
    );
    require_not_contains(
        result.output, "clang-format is not available",
        "ecos check ci must fail on repository drift before "
        "probing format tooling"
    );
    require_not_contains(
        result.output, "clang-tidy is not available",
        "ecos check ci must fail on repository drift before "
        "probing tidy tooling"
    );
}

void test_cli_doctor_reports_declared_dependency_guidance() {
    temp_dir root;
    write_sample_dependency_project(root.path());
    require_sync_success(
        root.path(), "sample dependency project must sync before doctor"
    );
    write_sample_cmake_cache(
        root.path(), "debug", "ECOSYSTEM_PROFILE_KDE:BOOL=OFF\n"
    );

    const cli_result result = run_cli(root.path(), "doctor");
    require_true(
        result.exit_code == 0,
        "ecos doctor must succeed on a complete sample dependency project"
    );
    require_contains(
        result.output, "declared dependencies:",
        "doctor must describe declared dependency guidance"
    );
    require_contains(
        result.output, "nlohmann_json 3.12 CONFIG package",
        "doctor must report manifest-declared JSON package requirements"
    );
    require_contains(
        result.output, "LLVM CONFIG + Clang CONFIG + libclang target",
        "doctor must report manifest-declared LLVM/Clang requirements"
    );
    require_contains(
        result.output, "Qt6 6.0 CONFIG components: Core Widgets",
        "doctor must report manifest-declared Qt requirements"
    );
    require_contains(
        result.output, "KF6 CONFIG components: CoreAddons",
        "doctor must report manifest-declared KDE requirements"
    );
    require_contains(
        result.output, "KDEGames6 package",
        "doctor must report manifest-declared KDEGames6 requirements"
    );
    require_contains(
        result.output, "JNI package",
        "doctor must report manifest-declared JNI requirements"
    );
    require_contains(
        result.output, "OpenCV package",
        "doctor must report manifest-declared optional OpenCV guidance"
    );
    require_contains(
        result.output, "Eigen3 3.3 package",
        "doctor must report manifest-declared optional Eigen guidance"
    );
    require_contains(
        result.output, "GTest package",
        "doctor must report manifest-declared GTest guidance"
    );
    require_contains(
        result.output, "benchmark package",
        "doctor must report manifest-declared benchmark guidance"
    );
}

void test_cli_doctor_artifact_scope_filters_declared_dependencies() {
    temp_dir root;
    write_sample_dependency_project(root.path());
    require_sync_success(
        root.path(),
        "sample dependency project must sync before artifact-scoped doctor"
    );
    write_sample_cmake_cache(
        root.path(), "debug", "ECOSYSTEM_PROFILE_KDE:BOOL=OFF\n"
    );

    const cli_result result = run_cli(root.path(), "doctor app:app");
    require_true(
        result.exit_code == 0,
        "artifact-scoped ecos doctor must succeed on a complete sample project"
    );
    require_contains(
        result.output, "artifact: app:app",
        "artifact-scoped doctor must report the selected artifact"
    );
    require_contains(
        result.output, "Qt6 6.0 CONFIG components: Core Widgets",
        "artifact-scoped doctor must include dependency closure requirements"
    );
    require_contains(
        result.output, "nlohmann_json 3.12 CONFIG package",
        "artifact-scoped doctor must include linked JSON requirements"
    );
    require_contains(
        result.output, "OpenCV package",
        "artifact-scoped doctor must include linked optional OpenCV guidance"
    );
    require_not_contains(
        result.output, "LLVM CONFIG + Clang CONFIG + libclang target",
        "artifact-scoped doctor must exclude dependencies "
        "outside the selected artifact closure"
    );
    require_not_contains(
        result.output, "KF6 CONFIG components: CoreAddons",
        "artifact-scoped doctor must exclude unrelated KDE guidance"
    );
    require_not_contains(
        result.output, "JNI package",
        "artifact-scoped doctor must exclude unrelated JNI guidance"
    );
    require_not_contains(
        result.output, "GTest package",
        "artifact-scoped doctor must exclude unrelated test-only guidance"
    );
    require_not_contains(
        result.output, "benchmark package",
        "artifact-scoped doctor must exclude unrelated benchmark guidance"
    );
    require_not_contains(
        result.output, "Eigen3 3.3 package",
        "artifact-scoped doctor must exclude unrelated "
        "benchmark dependency guidance"
    );
}

void test_cli_doctor_reports_configured_package_state_from_cache() {
    temp_dir root;
    write_sample_dependency_project(root.path());
    require_sync_success(
        root.path(),
        "sample dependency project must sync before cache-driven doctor"
    );
    write_sample_cmake_cache(
        root.path(), "debug",
        "ECOSYSTEM_PROFILE_KDE:BOOL=OFF\n"
        "Clang_DIR:PATH=/deps/clang\n"
        "LLVM_DIR:PATH=/deps/llvm\n"
        "nlohmann_json_DIR:PATH=/deps/json\n"
        "Qt6Core_DIR:PATH=/deps/qtcore\n"
        "Qt6Widgets_DIR:PATH=/deps/qtwidgets\n"
        "OpenCV_DIR:PATH=OpenCV_DIR-NOTFOUND\n"
        "Eigen3_DIR:PATH=/deps/eigen\n"
        "GTest_DIR:PATH=/deps/gtest\n"
        "benchmark_DIR:PATH=benchmark_DIR-NOTFOUND\n"
        "JAVA_INCLUDE_PATH:PATH=/deps/jni/include\n"
        "JAVA_JVM_LIBRARY:FILEPATH=/deps/jni/libjvm.so\n"
    );

    const cli_result result = run_cli(root.path(), "doctor");
    require_true(
        result.exit_code == 0,
        "ecos doctor must read configure data from the debug cache"
    );
    require_contains(
        result.output,
        "configured package state: "
            + relative_build_cache_path(root.path(), "debug"),
        "doctor must identify the configure cache it inspected"
    );
    require_contains(
        result.output, "nlohmann_json 3.12 CONFIG package: detected",
        "doctor must report detected JSON package state from cache data"
    );
    require_contains(
        result.output, "LLVM CONFIG + Clang CONFIG + libclang target: detected",
        "doctor must report detected LLVM/Clang package state from cache data"
    );
    require_contains(
        result.output, "Qt6 6.0 CONFIG components: detected Core Widgets",
        "doctor must report detected Qt component state from cache data"
    );
    require_contains(
        result.output, "JNI package: detected",
        "doctor must report detected JNI state from cache data"
    );
    require_contains(
        result.output,
        "KF6 CONFIG components: profile disabled in this configure data",
        "doctor must distinguish KDE profile packages from a non-kde cache"
    );
    require_contains(
        result.output,
        "KDEGames6 package: profile disabled in this configure data",
        "doctor must distinguish KDEGames6 from a non-kde cache"
    );
    require_contains(
        result.output, "OpenCV package: missing",
        "doctor must report missing optional OpenCV state from cache data"
    );
    require_contains(
        result.output, "Eigen3 3.3 package: detected",
        "doctor must report detected Eigen state from cache data"
    );
    require_contains(
        result.output, "GTest package: detected",
        "doctor must report detected GTest state from cache data"
    );
    require_contains(
        result.output, "benchmark package: missing",
        "doctor must report missing benchmark state from cache data"
    );
}

void test_cli_doctor_artifact_scope_filters_configured_package_state() {
    temp_dir root;
    write_sample_dependency_project(root.path());
    require_sync_success(
        root.path(),
        "sample dependency project must sync "
        "before artifact-scoped cache doctor"
    );
    write_sample_cmake_cache(
        root.path(), "debug",
        "ECOSYSTEM_PROFILE_KDE:BOOL=OFF\n"
        "nlohmann_json_DIR:PATH=/deps/json\n"
        "Qt6Core_DIR:PATH=/deps/qtcore\n"
        "Qt6Widgets_DIR:PATH=/deps/qtwidgets\n"
        "OpenCV_DIR:PATH=OpenCV_DIR-NOTFOUND\n"
        "Clang_DIR:PATH=/deps/clang\n"
        "LLVM_DIR:PATH=/deps/llvm\n"
        "GTest_DIR:PATH=/deps/gtest\n"
        "JAVA_INCLUDE_PATH:PATH=/deps/jni/include\n"
        "JAVA_JVM_LIBRARY:FILEPATH=/deps/jni/libjvm.so\n"
        "benchmark_DIR:PATH=/deps/benchmark\n"
        "Eigen3_DIR:PATH=/deps/eigen\n"
    );

    const cli_result result = run_cli(root.path(), "doctor app:app");
    require_true(
        result.exit_code == 0, "artifact-scoped doctor must read configure data"
    );
    require_contains(
        result.output,
        "configured package state: "
            + relative_build_cache_path(root.path(), "debug"),
        "artifact-scoped doctor must report the inspected cache"
    );
    require_contains(
        result.output, "nlohmann_json 3.12 CONFIG package: detected",
        "artifact-scoped doctor must include linked JSON package state"
    );
    require_contains(
        result.output, "Qt6 6.0 CONFIG components: detected Core Widgets",
        "artifact-scoped doctor must include linked Qt package state"
    );
    require_contains(
        result.output, "OpenCV package: missing",
        "artifact-scoped doctor must include linked optional OpenCV state"
    );
    require_not_contains(
        result.output, "LLVM CONFIG + Clang CONFIG + libclang target: detected",
        "artifact-scoped doctor must exclude unrelated LLVM/Clang package state"
    );
    require_not_contains(
        result.output, "JNI package: detected",
        "artifact-scoped doctor must exclude unrelated JNI package state"
    );
    require_not_contains(
        result.output, "GTest package: detected",
        "artifact-scoped doctor must exclude unrelated test package state"
    );
    require_not_contains(
        result.output, "benchmark package:",
        "artifact-scoped doctor must exclude unrelated benchmark package state"
    );
    require_not_contains(
        result.output, "Eigen3 3.3 package:",
        "artifact-scoped doctor must exclude unrelated Eigen package state"
    );
}

void test_cli_doctor_uses_requested_kde_cache_for_package_state() {
    temp_dir root;
    write_sample_dependency_project(root.path());
    require_sync_success(
        root.path(),
        "sample dependency project must sync before kde cache doctor"
    );
    write_sample_cmake_cache(
        root.path(), "kde",
        "ECOSYSTEM_PROFILE_KDE:BOOL=ON\n"
        "KF6_DIR:PATH=/deps/kf6\n"
        "KDEGames6_DIR:PATH=KDEGames6_DIR-NOTFOUND\n"
        "Qt6Core_DIR:PATH=/deps/qtcore\n"
        "Qt6Widgets_DIR:PATH=/deps/qtwidgets\n"
    );

    const cli_result result = run_cli(root.path(), "doctor kde");
    require_true(
        result.exit_code == 0,
        "profile-scoped doctor must use the requested profile cache"
    );
    require_contains(
        result.output,
        "configured package state: "
            + relative_build_cache_path(root.path(), "kde"),
        "doctor must use the kde cache for the kde profile"
    );
    require_contains(
        result.output, "KF6 CONFIG components: detected",
        "doctor must report detected KF6 state from the kde cache"
    );
    require_contains(
        result.output, "KDEGames6 package: missing",
        "doctor must report missing KDEGames6 state from the kde cache"
    );
}

void test_cli_doctor_reports_tracked_surface_drift() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );
    write_text(root.path() / "src/main.cpp", "int main() { return 0; }\n");

    const cli_result result = run_cli(root.path(), "doctor");
    require_true(
        result.exit_code != 0, "doctor must reject missing tracked surfaces"
    );
    require_contains(
        result.output, "tracked surfaces out of sync:",
        "doctor must report tracked surface drift"
    );
    require_contains(
        result.output,
        "CMakeLists.txt is missing or unreadable; run `marx sync`",
        "doctor must report a missing tracked CMake facade"
    );
    require_contains(
        result.output, ".gitignore is missing or unreadable; run `marx sync`",
        "doctor must report a missing tracked gitignore"
    );
}

void test_cli_doctor_materializes_missing_configure_cache() {
    temp_dir root;
    write_sample_json_project(root.path());
    require_sync_success(
        root.path(), "json sample project must sync before doctor refresh"
    );

    const fs::path cache_path
        = ecosystem::local_build_cache_path(root.path(), "debug");
    require_true(
        !fs::exists(cache_path),
        "configure cache must start missing for materialization test"
    );

    const cli_result result = run_cli(root.path(), "doctor");
    require_true(
        result.exit_code == 0, "doctor must materialize missing configure cache"
    );
    require_true(
        fs::exists(cache_path),
        "doctor must generate the missing configure cache"
    );
    require_contains(
        result.output,
        "configured package state: "
            + relative_build_cache_path(root.path(), "debug") + " (refreshed)",
        "doctor must report that it refreshed missing configure data"
    );
    require_contains(
        result.output, "nlohmann_json 3.12 CONFIG package: detected",
        "doctor must report configured package state after "
        "materializing the cache"
    );
}

void test_cli_doctor_refreshes_stale_configure_cache() {
    temp_dir root;
    write_sample_json_project(root.path());
    require_sync_success(
        root.path(), "json sample project must sync before stale cache test"
    );

    const fs::path cache_path
        = ecosystem::local_build_cache_path(root.path(), "debug");
    write_sample_cmake_cache(
        root.path(), "debug",
        "nlohmann_json_DIR:PATH=nlohmann_json_DIR-NOTFOUND\n"
    );
    age_file_by_seconds(cache_path, std::chrono::seconds(5));

    ecosystem::manifest manifest_value = sample_json_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must rewrite manifest to make the cache stale"
    );

    const cli_result result = run_cli(root.path(), "doctor");
    require_true(
        result.exit_code == 0, "doctor must refresh stale configure cache"
    );
    require_contains(
        result.output,
        "configured package state: "
            + relative_build_cache_path(root.path(), "debug") + " (refreshed)",
        "doctor must report that stale configure data was refreshed"
    );
    require_contains(
        result.output, "nlohmann_json 3.12 CONFIG package: detected",
        "doctor must replace stale configured package state with refreshed data"
    );
    require_not_contains(
        result.output, "nlohmann_json 3.12 CONFIG package: missing",
        "doctor must not keep stale package-state data after refresh"
    );
}

void test_cli_doctor_artifact_scope_refresh_uses_scoped_probe_cache() {
    temp_dir root;
    write_sample_scoped_probe_project(root.path());
    require_sync_success(
        root.path(),
        "scoped probe sample project must sync before doctor refresh"
    );

    const cli_result result
        = run_cli(root.path(), "doctor sample:app --profile kde");
    require_true(
        result.exit_code == 0,
        "artifact-scoped doctor must use a scoped probe cache when the "
        "project kde cache is unavailable"
    );
    require_contains(
        result.output,
        "configured package state: "
            + relative_doctor_probe_cache_path(
                root.path(), ecosystem::artifact_ref { "sample", "app" }, "kde"
            )
            + " (refreshed)",
        "artifact-scoped doctor must report the scoped probe cache path"
    );
    require_contains(
        result.output, "nlohmann_json 3.12 CONFIG package: detected",
        "artifact-scoped doctor must refresh package state for the "
        "selected artifact closure"
    );
    require_not_contains(
        result.output, "KF6 CONFIG components",
        "artifact-scoped doctor must not pull unrelated KDE "
        "requirements into the selected closure"
    );
}

void test_cli_doctor_refreshes_stale_configure_state_without_cache_changes() {
    temp_dir root;
    write_sample_json_project(root.path());
    require_sync_success(
        root.path(),
        "json sample project must sync before configure-state refresh test"
    );

    const cli_result initial_result = run_cli(root.path(), "doctor");
    require_true(
        initial_result.exit_code == 0,
        "doctor must materialize configure state before the "
        "stale-configure-state regression test"
    );

    const fs::path stamp_path = ecosystem::local_build_dir(root.path(), "debug")
        / ".ecosystem_configured.stamp";
    require_true(
        fs::exists(stamp_path), "doctor refresh must create the configure stamp"
    );
    age_file_by_seconds(stamp_path, std::chrono::seconds(5));

    ecosystem::manifest manifest_value = sample_json_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must rewrite manifest to make the configure state stale"
    );

    const cli_result refreshed_result = run_cli(root.path(), "doctor");
    require_true(
        refreshed_result.exit_code == 0,
        "doctor must refresh stale configure state even when cache "
        "contents do not change"
    );
    require_contains(
        refreshed_result.output,
        "configured package state: "
            + relative_build_cache_path(root.path(), "debug") + " (refreshed)",
        "doctor must report refreshed configure state after "
        "updating the configure stamp"
    );
    require_not_contains(
        refreshed_result.output, "configured package state: unavailable",
        "doctor must not report stale configure state as "
        "unavailable after a successful refresh"
    );
}

void write_cxx_report_project(const fs::path& root, const std::string& id) {
    write_text(
        root / "manifest.json",
        json(
            { { "id", id },
              { "description", "Artifact-scoped report fixture" },
              { "facade", "core:app" },
              { "artifacts",
                json::array(
                    { { { "id", "core:app" },
                        { "kind", "exe" },
                        { "owns", json::array() },
                        { "entry", "src/main.cpp" } },
                      { { "id", "core:other" },
                        { "kind", "exe" },
                        { "owns", json::array() },
                        { "entry", "src/other.cpp" } } }
                ) } }
        ).dump(2)
    );
    write_text(root / "src/main.cpp", "int main() { return 0; }\n");
    write_text(root / "src/other.cpp", "#error unselected artifact\n");
}

void test_cli_report_cxx_preserves_artifact_scope_and_failure_status() {
    temp_dir root;
    scoped_env flags("CXXFLAGS", "-Wno-error=unused-variable");
    write_cxx_report_project(root.path(), "reports");
    const auto report_path = root.path() / ".ecosystem/reports/cxx.json";
    auto result = run_engels_cli(root.path(), "report cxx core:app --json");
    require_true(
        result.exit_code == 0,
        "scoped C++ report must succeed:\n" + result.output
    );
    auto report = json::parse(read_text(report_path));
    require_true(
        json::parse(result.output) == report,
        "fresh CMake output must not contaminate machine-readable stdout"
    );
    require_true(
        report.at("status") == "clean" && report.at("files_analyzed") == 1
            && report.at("artifact") == "core:app"
            && report.at("sources").at(0).at("artifact") == "core:app",
        "report and source must retain full artifact identity without sibling "
        "sources"
    );
    require_not_contains(
        result.output, "other.cpp", "terminal must not widen artifact scope"
    );
    const auto before = read_text(report_path);
    for (const std::string command :
         { "report cxx core:absent", "report matrix core:absent",
           "report toolchains core:app" }) {
        result = run_engels_cli(root.path(), command);
        require_true(
            result.exit_code == 2 && read_text(report_path) == before,
            "invalid report requests must fail before replacing results: "
                + command
        );
    }
    result = run_engels_cli(root.path(), "report cxx core:other");
    report = json::parse(read_text(report_path));
    require_true(
        result.exit_code == 5 && report.at("status") == "failed"
            && !report.at("errors").empty()
            && report.at("total_errors").get<int>() > 0,
        "source parse errors must persist an incomplete report and fail the "
        "operation"
    );
    require_true(
        !report.at("findings").empty(),
        "compiler diagnostics must use structured findings"
    );
    const auto& finding = report.at("findings").at(0);
    require_true(
        finding.at("authority") == "external"
            && finding.at("enforcement") == "required"
            && finding.at("severity") == "error"
            && finding.at("file") == "src/other.cpp" && finding.at("line") == 1
            && finding.at("artifact") == "core:other"
            && finding.at("entity_kind") == "translation_unit",
        "compiler findings must preserve authority, rule context, severity, "
        "location and owner"
    );
    require_contains(
        result.output, finding.at("rule").get<std::string>(),
        "terminal and JSON must identify the same compiler diagnostic"
    );
    require_contains(
        result.output, "unselected artifact",
        "failure must preserve Clang's reason"
    );

    write_text(
        root.path() / "src/main.cpp",
        "int main() { int unused = 0; return 0; }\n"
    );
    result = run_engels_cli(root.path(), "report cxx core:app --json");
    report = json::parse(read_text(report_path));
    require_true(
        result.exit_code == 0 && report.at("status") == "findings"
            && report.at("total_warnings").get<int>() > 0
            && report.at("errors").empty(),
        "successful analysis with warnings must remain distinct from failed "
        "analysis:\n"
            + result.output
    );
    temp_dir failed;
    write_cxx_report_project(failed.path(), "failed_configure");
    write_text(
        failed.path() / ".ecosystem/reports/cxx.json",
        "{\"status\":\"clean\"}\n"
    );
    scoped_env invalid_flags("CXXFLAGS", "-fmanifesto-invalid-compiler-option");
    result = run_engels_cli(failed.path(), "report cxx core:app --json");
    report
        = json::parse(read_text(failed.path() / ".ecosystem/reports/cxx.json"));
    require_true(
        result.exit_code != 0 && report.at("status") == "failed"
            && report.at("artifact") == "core:app"
            && report.at("files_analyzed") == 0,
        "configure failures must replace stale success with a failed scoped "
        "report"
    );
    require_true(
        json::parse(result.output) == report,
        "failed configuration must also emit one complete JSON object"
    );
    require_contains(
        report.at("errors").dump(), "-fmanifesto-invalid-compiler-option",
        "report must retain captured compiler configuration diagnostics"
    );
    require_contains(
        report.at("errors").dump(), "configure failed",
        "failed report must retain the configure failure reason"
    );
}

void test_cli_workspace_report_cxx_preserves_each_selected_owner() {
    temp_dir root;
    write_cxx_report_project(root.path() / "alpha", "alpha");
    write_cxx_report_project(root.path() / "beta", "beta");
    write_text(root.path() / "broken/manifest.json", "invalid JSON\n");
    const auto report_path
        = root.path() / ".ecosystem/reports/workspace_cxx.json";
    auto result = run_engels_cli(root.path(), "report cxx");
    require_true(
        result.exit_code == 2 && !fs::exists(report_path),
        "analysis must preflight selected workspace validity before writing "
        "reports"
    );
    result = run_engels_cli(
        root.path(), "report cxx alpha/core:app beta/core:app --json"
    );
    require_true(
        result.exit_code == 0,
        "qualified workspace C++ reports must succeed:\n" + result.output
    );
    auto report = json::parse(read_text(report_path));
    require_true(
        json::parse(result.output) == report,
        "workspace stdout must equal the complete persisted JSON result"
    );
    require_true(
        report.at("status") == "clean" && report.at("projects").size() == 2,
        "workspace must retain each selected analysis"
    );
    for (const auto& project : report.at("projects"))
        require_true(
            project.at("artifact") == "core:app"
                && project.at("files_analyzed") == 1
                && project.at("sources").at(0).at("artifact") == "core:app",
            "each project result must select the full owner, including shared "
            "namespaces"
        );
    result = run_engels_cli(
        root.path(), "report cxx alpha/core:app beta/core:other"
    );
    report = json::parse(read_text(report_path));
    require_true(
        result.exit_code == 5 && report.at("status") == "failed"
            && report.at("projects").at(0).at("status") == "clean"
            && report.at("projects").at(1).at("status") == "failed",
        "workspace failures must retain successful peers and per-project status"
    );
}

void test_cli_personal_reports_share_findings_and_presentation() {
    temp_dir root;
    write_cxx_report_project(root.path(), "reports");
    auto manifest = json::parse(read_text(root.path() / "manifest.json"));
    manifest["artifacts"][0]["owns"] = json::array({ "include/model.hpp" });
    write_text(root.path() / "manifest.json", manifest.dump(2));
    write_text(
        root.path() / "include/model.hpp",
        "#pragma once\ninline int BadName(int value) { if (value) return 1; "
        "return 0; }\n"
    );
    write_text(
        root.path() / "src/main.cpp",
        "#include \"model.hpp\"\nint main() { return BadName(0); }\n"
    );
    for (const std::string profile : { "naming", "style" }) {
        auto result
            = run_engels_cli(root.path(), "check " + profile + " core:app");
        require_true(
            result.exit_code == 0,
            "personal checks must accept advisory findings"
        );
        const auto path
            = root.path() / (".ecosystem/reports/" + profile + ".json");
        const auto checked = json::parse(read_text(path));
        result = run_engels_cli(
            root.path(), "report " + profile + " core:app --json"
        );
        require_true(
            result.exit_code == 0 && json::parse(result.output) == checked
                && json::parse(read_text(path)) == checked
                && checked.at("status") == "findings",
            "check, report, persisted JSON and stdout must use the same result"
        );
        const auto terminal
            = run_engels_cli(root.path(), "report " + profile + " core:app");
        require_true(
            terminal.exit_code == 0,
            "terminal reporting must accept advisory findings"
        );
        for (const auto& finding : checked.at("findings")) {
            require_true(
                finding.at("authority") == "manifesto"
                    && finding.at("enforcement") == "advisory"
                    && finding.at("severity") == "hint"
                    && !finding.at("category").get<std::string>().empty(),
                "personal diagnostics must expose their policy and severity"
            );
            require_contains(
                terminal.output, finding.at("rule").get<std::string>(),
                "terminal must preserve the rule"
            );
            require_contains(
                terminal.output, finding.at("message").get<std::string>(),
                "terminal must preserve the explanation"
            );
            require_contains(
                terminal.output,
                "artifact=" + finding.at("artifact").get<std::string>(),
                "terminal must preserve full ownership"
            );
            const auto location = finding.at("file").get<std::string>() + ":"
                + std::to_string(finding.at("line").get<unsigned>()) + ":"
                + std::to_string(finding.at("column").get<unsigned>());
            require_contains(
                terminal.output, location,
                "terminal must preserve the source location"
            );
            if (!finding.at("measurements").empty())
                require_contains(
                    terminal.output, finding.at("measurements").dump(),
                    "terminal must preserve measurements"
                );
            if (!finding.at("thresholds").empty())
                require_contains(
                    terminal.output, finding.at("thresholds").dump(),
                    "terminal must preserve thresholds"
                );
            if (profile == "style") {
                require_true(
                    !finding.at("referent").is_null(),
                    "header diagnostics must retain their indexed referent"
                );
                require_contains(
                    terminal.output,
                    "referent=" + finding.at("referent").get<std::string>(),
                    "terminal must identify the referent"
                );
            }
        }
        require_not_contains(
            terminal.output, "other.cpp", "reports must not widen ownership"
        );
        const auto before = read_text(path);
        for (const std::string suffix :
             { "core:absent --json", "core:app --json --json",
               "core:app --unsupported" }) {
            result = run_engels_cli(
                root.path(), "report " + profile + " " + suffix
            );
            require_true(
                result.exit_code == 2 && read_text(path) == before,
                "invalid report requests must preserve the last result"
            );
        }
    }
    auto result
        = run_engels_cli(root.path(), "report naming core:other --json");
    const auto failed = json::parse(result.output);
    require_true(
        result.exit_code == 5 && failed.at("status") == "failed"
            && !failed.at("errors").empty(),
        "personal parser failures must retain a machine-readable result and "
        "fail"
    );
    const auto path = root.path() / ".ecosystem/reports/style.json";
    fs::remove(path);
    fs::create_directory(path);
    result = run_engels_cli(root.path(), "report style core:app --json");
    require_true(
        result.exit_code == 5,
        "unwritable report destinations must fail the operation"
    );
    result = run_engels_cli(root.path(), "report matrix core:app --json");
    require_true(
        result.exit_code == 0
            && json::parse(result.output).at("artifacts").size() == 1,
        "matrix must retain JSON inspection with explicit output selection"
    );
}

void test_cli_workspace_personal_reports_keep_all_selected_results() {
    temp_dir root;
    write_cxx_report_project(root.path() / "alpha", "alpha");
    write_cxx_report_project(root.path() / "beta", "beta");
    write_text(root.path() / "broken/manifest.json", "invalid JSON\n");
    write_text(
        root.path() / "alpha/src/main.cpp",
        "int main() { int BadName = 0; if (BadName) ++BadName; return BadName; "
        "}\n"
    );
    write_text(
        root.path() / "alpha/src/other.cpp", "int main() { return 0; }\n"
    );
    write_text(
        root.path() / "manifesto.workspace.json",
        "{\"groups\":{\"apps\":[\"alpha\",\"beta\"]}}\n"
    );
    for (const std::string kind : { "naming", "style" }) {
        const auto path
            = root.path() / (".ecosystem/reports/workspace_" + kind + ".json");
        auto result = run_engels_cli(root.path(), "report " + kind + " --json");
        require_true(
            result.exit_code == 2 && !fs::exists(path),
            "invalid workspace selection must fail before reporting"
        );
        result = run_engels_cli(
            root.path(),
            "report " + kind
                + " alpha/core:app alpha/core:other beta/core:app --json"
        );
        const auto report = json::parse(result.output);
        require_true(
            result.exit_code == 0 && report == json::parse(read_text(path))
                && report.at("status") == "findings"
                && report.at("projects").size() == 3,
            "workspace reports must retain every artifact request, including "
            "two owners in one project"
        );
        require_true(
            report.at("projects").at(0).at("artifact") == "core:app"
                && report.at("projects").at(1).at("artifact") == "core:other"
                && report.at("projects").at(1).at("status") == "clean",
            "same-project requests must not overwrite each other"
        );
        result = run_engels_cli(
            root.path(), "report " + kind + " --group apps --json"
        );
        const auto failed = json::parse(result.output);
        require_true(
            result.exit_code == 5 && failed.at("status") == "failed"
                && failed.at("projects").at(0).at("status") == "findings"
                && failed.at("projects").at(1).at("status") == "failed",
            "workspace group failures must retain findings and failures "
            "separately"
        );
        const auto terminal
            = run_engels_cli(root.path(), "report " + kind + " alpha/core:app");
        require_contains(
            terminal.output, "alpha (alpha) [core:app]",
            "terminal must qualify project, root and artifact"
        );
        require_contains(
            terminal.output, "workspace_" + kind + ".json",
            "terminal must locate the complete workspace report"
        );
    }
}

void test_clang_analysis_reports_unusable_inputs() {
    temp_dir root;
    write_cxx_report_project(root.path(), "reports");
    const auto manifest
        = ecosystem::load_manifest(root.path() / "manifest.json");
    require_true(manifest.value.has_value(), "fixture manifest must load");
    const auto analyze = [&] {
        return ecosystem::analyze_project_sources(
            *manifest.value, root.path(),
            ecosystem::artifact_ref { "core", "app" }, false, false
        );
    };
    fs::remove(root.path() / "src/main.cpp");
    auto report = analyze();
    require_true(
        !report.errors.empty()
            && ecosystem::to_json(report).at("status") == "failed",
        "missing selected inputs must fail analysis"
    );
    fs::create_directory(root.path() / "src/main.cpp");
    report = analyze();
    require_true(
        !report.errors.empty() && report.files_analyzed == 0,
        "non-file source paths must fail without pretending to analyze a "
        "translation unit"
    );
    fs::remove(root.path() / "src/main.cpp");
    write_text(root.path() / "src/main.cpp", "int main() { return 0; }\n");
    write_text(
        ecosystem::local_build_dir(root.path(), "debug")
            / "compile_commands.json",
        "invalid JSON\n"
    );
    report = analyze();
    require_true(
        !report.errors.empty() && report.files_analyzed == 0,
        "unreadable configured analysis inputs must not silently use guessed "
        "compiler arguments"
    );
}

void test_clang_analysis_runs_on_sample_project() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_manifest();
    manifest_value.components.front().modules.push_back("sample/core");
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );
    write_text(
        root.path() / "include/sample/core.hpp",
        "#pragma once\nint meaning();\n"
    );
    write_text(
        root.path() / "src/sample/core.cpp",
        "#include \"sample/core.hpp\"\nint meaning() { return 42; }\n"
    );
    write_text(root.path() / "src/main.cpp", "int main() { return 0; }\n");

    const ecosystem::cxx_analysis_report report
        = ecosystem::analyze_project_sources(
            manifest_value, root.path(), std::nullopt, false, false
        );
    require_true(
        report.errors.empty(),
        "Clang analysis should succeed on a simple project"
    );
    require_true(
        report.files_analyzed == 2,
        "analysis should include module and source-only files"
    );
    require_true(
        report.core_files == 1 && report.runtime_files == 1
            && report.test_files == 0 && report.benchmark_files == 0,
        "analysis should categorize module and runtime sources"
    );
    require_true(
        report.total_functions >= 2, "analysis should report structural counts"
    );
    require_true(
        !report.tests_included && !report.benchmarks_included,
        "analysis metadata should reflect disabled optional categories"
    );
}

void test_clang_analysis_skips_benchmarks_and_generated_sources_by_default() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_dependency_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );
    write_sample_dependency_project(root.path());
    write_text(
        root.path() / "tests/generated_auto.cpp", "this is not valid cxx\n"
    );

    const ecosystem::cxx_analysis_report report
        = ecosystem::analyze_project_sources(
            manifest_value, root.path(), std::nullopt, true, false
        );
    require_true(
        report.errors.empty(),
        "analysis should ignore undeclared generated sources"
    );
    require_true(
        report.files_analyzed == 4,
        "analysis should skip benchmark-only components by default"
    );
    require_true(
        report.core_files == 2 && report.runtime_files == 1
            && report.test_files == 1 && report.benchmark_files == 0,
        "analysis should keep declared test sources while excluding "
        "benchmark-only sources"
    );
    require_true(
        find_source_analysis(report, "tests/generated_auto.cpp") == nullptr,
        "undeclared generated sources must not be analyzed"
    );
    require_true(
        find_source_analysis(report, "benchmarks/bench_all.cpp") == nullptr,
        "benchmark-only sources must stay excluded by default"
    );
    require_true(
        find_source_analysis(report, "tests/test_main.cpp") != nullptr,
        "declared test sources must remain analyzable"
    );

    const ecosystem::component_analysis_summary* tests_summary
        = find_component_analysis_summary(report, "tests");
    require_true(
        tests_summary != nullptr && tests_summary->test_files == 1,
        "component summaries should retain test categorization"
    );
}

void test_clang_analysis_includes_benchmark_component_when_requested() {
    temp_dir root;
    ecosystem::manifest manifest_value = sample_dependency_manifest();
    std::string error_message;
    require_true(
        ecosystem::save_manifest(
            root.path() / "manifest.json", manifest_value, &error_message
        ),
        "must write temporary manifest"
    );
    write_sample_dependency_project(root.path());

    const ecosystem::cxx_analysis_report report
        = ecosystem::analyze_project_sources(
            manifest_value, root.path(),
            ecosystem::artifact_ref { "benchmarks", "bench" }, true, true
        );
    require_true(
        report.errors.empty(),
        "benchmark-only analysis should succeed when explicitly enabled"
    );
    require_true(
        report.files_analyzed == 1 && report.benchmark_files == 1,
        "benchmark-only analysis should include the selected benchmark "
        "source"
    );

    const ecosystem::source_analysis* benchmark_source
        = find_source_analysis(report, "benchmarks/bench_all.cpp");
    require_true(
        benchmark_source != nullptr
            && benchmark_source->component_id == "benchmarks"
            && benchmark_source->category == "benchmarks",
        "benchmark source metadata should report the owning component and "
        "category"
    );

    const ecosystem::component_analysis_summary* benchmark_summary
        = find_component_analysis_summary(report, "benchmarks");
    require_true(
        benchmark_summary != nullptr && benchmark_summary->files_analyzed == 1
            && benchmark_summary->benchmark_files == 1,
        "component summaries should include benchmark-only analysis state"
    );
}

} // namespace ecosystem_test_support

using namespace ecosystem_test_support;

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--workspace-conformance") {
        if (argc != 3) {
            std::cerr << "usage: " << argv[0] << " --workspace-conformance <root>\n";
            return 2;
        }
        const auto errors = workspace_conformance_errors(fs::absolute(argv[2]));
        for (const auto& error : errors)
            std::cerr << "[failed] " << error << "\n";
        if (errors.empty())
            std::cout << "workspace conformance passed\n";
        return errors.empty() ? 0 : 1;
    }
    if (argc > 2 || (argc == 2 && std::string(argv[1]).starts_with("--"))) {
        std::cerr << "usage: " << argv[0] << " [test-name-filter]\n";
        return 2;
    }
    const std::vector<std::pair<std::string, std::function<void()>>> tests {
        { "file_writers_report_late_io_failures",
          test_file_writers_report_late_io_failures },
        { "self_manifest_loads", test_self_manifest_loads },
        { "manifest_rejects_unsafe_paths_and_invalid_dependency_graphs",
          test_manifest_rejects_unsafe_paths_and_invalid_dependency_graphs },
        { "owned_paths_reject_symlink_escapes",
          test_owned_paths_reject_symlink_escapes },
        { "owned_paths_allow_internal_symlinks_and_missing_files",
          test_owned_paths_allow_internal_symlinks_and_missing_files },
        { "owned_paths_preflight_mutations_without_changes",
          test_owned_paths_preflight_mutations_without_changes },
        { "owned_paths_preflight_sync_and_manifest_destinations",
          test_owned_paths_preflight_sync_and_manifest_destinations },
        { "manifest_rejects_generated_test_collisions",
          test_manifest_rejects_generated_test_collisions },
        { "authored_artifacts_round_trip_and_mutate",
          test_authored_artifacts_round_trip_and_mutate },
        { "authored_artifacts_reject_obsolete_and_ambiguous_intent",
          test_authored_artifacts_reject_obsolete_and_ambiguous_intent },
        { "owned_test_targets_are_independent",
          test_owned_test_targets_are_independent },
        { "owned_scope_discovery_and_build",
          test_owned_scope_discovery_and_build },
        { "manifest_rejects_output_and_target_collisions",
          test_manifest_rejects_output_and_target_collisions },
        { "installed_libraries_are_relocatable",
          test_installed_libraries_are_relocatable },
        { "install_artifacts_build_and_install_independent_executables",
          test_install_artifacts_build_and_install_independent_executables },
        { "visitor_facade_tracks_selected_artifact",
          test_visitor_facade_tracks_selected_artifact },
        { "visitor_facade_handles_runtime_output_layouts",
          test_visitor_facade_handles_runtime_output_layouts },
        { "visitor_facade_rejects_conflicting_output_names",
          test_visitor_facade_rejects_conflicting_output_names },
        { "sync_matches_tracked_surfaces", test_sync_matches_tracked_surfaces },
        { "package_surface_includes_qttest_component",
          test_package_surface_includes_qttest_component },
        { "tracked_facade_limits_surface_to_entry_artifact_closure",
          test_tracked_facade_limits_surface_to_entry_artifact_closure },
        { "external_project_generates_imported_library",
          test_external_project_generates_imported_library },
        { "source_dependency_local_override_builds_and_installs_before_"
          "consumer",
          test_source_dependency_local_override_builds_and_installs_before_consumer },
        { "source_dependency_repository_selection_is_stable_and_explicit",
          test_source_dependency_repository_selection_is_stable_and_explicit },
        { "source_dependency_consumer_exports_are_relocatable",
          test_source_dependency_consumer_exports_are_relocatable },
        { "source_dependency_shared_and_interface_usage_requirements",
          test_source_dependency_shared_and_interface_usage_requirements },
        { "source_dependency_rejects_invalid_provider_contracts_before_"
          "consumer_configure",
          test_source_dependency_rejects_invalid_provider_contracts_before_consumer_configure },
        { "source_dependency_rejects_ambiguous_authored_metadata",
          test_source_dependency_rejects_ambiguous_authored_metadata },
        { "library_facade_installs_library_target_and_headers",
          test_library_facade_installs_library_target_and_headers },
        { "interface_library_facade_installs_headers_without_target",
          test_interface_library_facade_installs_headers_without_target },
        { "runnable_facade_skips_linked_static_library_install_surface",
          test_runnable_facade_skips_linked_static_library_install_surface },
        { "runnable_facade_installs_linked_shared_library_closure",
          test_runnable_facade_installs_linked_shared_library_closure },
        { "project_assets_are_staged_and_installed",
          test_project_assets_are_staged_and_installed },
        { "android_application_id_is_manifest_owned",
          test_android_application_id_is_manifest_owned },
        { "developer_surface_materializes_full_build_graph",
          test_developer_surface_materializes_full_build_graph },
        { "forbidden_repository_entries_report_legacy_scaffolding",
          test_forbidden_repository_entries_report_legacy_scaffolding },
        { "forbidden_repository_entries_fall_back_for_gitlink_roots",
          test_forbidden_repository_entries_fall_back_for_gitlink_roots },
        { "component_package_link_targets_include_qttest_target",
          test_component_package_link_targets_include_qttest_target },
        { "component_package_link_targets_include_cxxopts_target",
          test_component_package_link_targets_include_cxxopts_target },
        { "component_package_link_targets_follow_descriptor_rules",
          test_component_package_link_targets_follow_descriptor_rules },
        { "summarize_dependencies_follows_descriptor_sources",
          test_summarize_dependencies_follows_descriptor_sources },
        { "summarize_dependencies_collects_cxxopts_stack_values",
          test_summarize_dependencies_collects_cxxopts_stack_values },
        { "declared_package_sections_group_kde_by_profile",
          test_declared_package_sections_group_kde_by_profile },
        { "configured_package_sections_report_component_and_profile_state",
          test_configured_package_sections_report_component_and_profile_state },
        { "package_descriptors_bind_dependency_ids",
          test_package_descriptors_bind_dependency_ids },
        { "package_descriptors_bind_rule_types",
          test_package_descriptors_bind_rule_types },
        { "registered_packages_keep_stable_order",
          test_registered_packages_keep_stable_order },
        { "enabled_surface_blocks_deduplicate_kde_block",
          test_enabled_surface_blocks_deduplicate_kde_block },
        { "dependency_entry_for_id_returns_matching_entry",
          test_dependency_entry_for_id_returns_matching_entry },
        { "package_surface_block_order_is_stable",
          test_package_surface_block_order_is_stable },
        { "package_group_heading_is_stable",
          test_package_group_heading_is_stable },
        { "configured_status_for_package_reports_profile_disabled_kde",
          test_configured_status_for_package_reports_profile_disabled_kde },
        { "configured_status_for_package_requires_all_jni_cache_keys",
          test_configured_status_for_package_requires_all_jni_cache_keys },
        { "configured_status_for_package_detects_cxxopts_dir",
          test_configured_status_for_package_detects_cxxopts_dir },
        { "find_package_surface_rule_returns_qt_rule",
          test_find_package_surface_rule_returns_qt_rule },
        { "render_package_surface_block_keeps_header_only_json_cross_"
          "compilable",
          test_render_package_surface_block_keeps_header_only_json_cross_compilable },
        { "render_package_surface_block_renders_kde_support",
          test_render_package_surface_block_renders_kde_support },
        { "render_package_surface_block_renders_qt_support",
          test_render_package_surface_block_renders_qt_support },
        { "render_package_surface_block_renders_llvm_support",
          test_render_package_surface_block_renders_llvm_support },
        { "render_package_surface_block_renders_opencv_support",
          test_render_package_surface_block_renders_opencv_support },
        { "render_package_surface_block_renders_cxxopts_support",
          test_render_package_surface_block_renders_cxxopts_support },
        { "workspace_conformance_reports_all_projects",
          test_workspace_conformance_reports_all_projects },
        { "tracked_surface_generation_uses_github_vars_file",
          test_tracked_surface_generation_uses_github_vars_file },
        { "tracked_surface_generation_supports_remote_setup_action",
          test_tracked_surface_generation_supports_remote_setup_action },
        { "sync_project_removes_obsolete_tracked_surface_files",
          test_sync_project_removes_obsolete_tracked_surface_files },
        { "required_template_failures_preserve_generated_state",
          test_required_template_failures_preserve_generated_state },
        { "required_developer_template_failure_is_reported",
          test_required_developer_template_failure_is_reported },
        { "required_missing_and_unreadable_templates_fail",
          test_required_missing_and_unreadable_templates_fail },
        { "mutate_add_module_updates_manifest_and_files",
          test_mutate_add_module_updates_manifest_and_files },
        { "ci_stage_reports_intermediate_and_pipeline_failures",
          test_ci_stage_reports_intermediate_and_pipeline_failures },
        { "ci_required_result_enforces_failures_and_missing_evidence",
          test_ci_required_result_enforces_failures_and_missing_evidence },
        { "github_bootstrap_vars_validate_explicit_selection",
          test_github_bootstrap_vars_validate_explicit_selection },
        { "ci_bootstrap_builds_reviewed_checkout_and_repository_layouts",
          test_ci_bootstrap_builds_reviewed_checkout_and_repository_layouts },
        { "ci_bootstrap_rejects_missing_inputs_and_escaping_layouts",
          test_ci_bootstrap_rejects_missing_inputs_and_escaping_layouts },
        { "installed_actors_use_their_template_bundle",
          test_installed_actors_use_their_template_bundle },
        { "doxygen_configuration_uses_exact_ownership_and_service_state",
          test_doxygen_configuration_uses_exact_ownership_and_service_state },
        { "cli_doxygen_generates_native_scoped_documentation",
          test_cli_doxygen_generates_native_scoped_documentation },
        { "cli_doxy_propagates_native_graphviz_errors_with_zero_tool_exit",
          test_cli_doxy_propagates_native_graphviz_errors_with_zero_tool_exit },
        { "cli_doxy_retains_tool_failures_and_requires_fresh_output",
          test_cli_doxy_retains_tool_failures_and_requires_fresh_output },
        { "cli_doxy_rejects_service_output_aliases_before_writing",
          test_cli_doxy_rejects_service_output_aliases_before_writing },
        { "cli_workspace_doxy_keeps_selected_artifact_outputs_separate",
          test_cli_workspace_doxy_keeps_selected_artifact_outputs_separate },
        { "cli_check_doxy_rejects_failed_configuration",
          test_cli_check_doxy_rejects_failed_configuration },
        { "template_loader_renders_repo_owned_templates",
          test_template_loader_renders_repo_owned_templates },
        { "ensure_local_artifacts_keeps_tracked_style_surfaces_sync_owned",
          test_ensure_local_artifacts_keeps_tracked_style_surfaces_sync_owned },
        { "mutate_add_component_scaffolds_templates",
          test_mutate_add_component_scaffolds_templates },
        { "mutate_add_component_supports_custom_artifact_ids_and_links",
          test_mutate_add_component_supports_custom_artifact_ids_and_links },
        { "mutate_add_component_infers_facade_library_closure",
          test_mutate_add_component_infers_facade_library_closure },
        { "mutate_add_component_infers_facade_component_library_closure",
          test_mutate_add_component_infers_facade_component_library_closure },
        { "mutate_add_component_infers_direct_runnable_library_links",
          test_mutate_add_component_infers_direct_runnable_library_links },
        { "mutate_add_file_unit_supports_h_variants",
          test_mutate_add_file_unit_supports_h_variants },
        { "cli_mutate_add_component_supports_artifact_id_and_link_options",
          test_cli_mutate_add_component_supports_artifact_id_and_link_options },
        { "cli_list_artifacts", test_cli_list_artifacts },
        { "cli_actor_batches_preserve_ownership",
          test_cli_actor_batches_preserve_ownership },
        { "cli_actor_help_and_queue_validation",
          test_cli_actor_help_and_queue_validation },
        { "cli_all_operations_have_one_actor",
          test_cli_all_operations_have_one_actor },
        { "cli_engels_rejects_marx_commands",
          test_cli_engels_rejects_marx_commands },
        { "cli_marx_rejects_engels_commands",
          test_cli_marx_rejects_engels_commands },
        { "cli_report_matrix", test_cli_report_matrix },
        { "cli_workspace_list", test_cli_workspace_list },
        { "invalid_workspace_projects_remain_visible",
          test_invalid_workspace_projects_remain_visible },
        { "invalid_workspace_group_and_root_manifest",
          test_invalid_workspace_group_and_root_manifest },
        { "cli_workspace_list_groups", test_cli_workspace_list_groups },
        { "cli_workspace_group_filter_selects_configured_projects",
          test_cli_workspace_group_filter_selects_configured_projects },
        { "cli_workspace_report_matrix_records_group_selection",
          test_cli_workspace_report_matrix_records_group_selection },
        { "cli_workspace_group_filter_rejects_unknown_groups",
          test_cli_workspace_group_filter_rejects_unknown_groups },
        { "cli_workspace_config_rejects_unknown_group_project_selector",
          test_cli_workspace_config_rejects_unknown_group_project_selector },
        { "cli_workspace_report_matrix", test_cli_workspace_report_matrix },
        { "cli_workspace_build_on_sample_workspace",
          test_cli_workspace_build_on_sample_workspace },
        { "cli_workspace_build_supports_multiple_artifact_filters",
          test_cli_workspace_build_supports_multiple_artifact_filters },
        { "cli_workspace_sync_supports_project_filter",
          test_cli_workspace_sync_supports_project_filter },
        { "cli_workspace_sync_supports_multi_project_filter",
          test_cli_workspace_sync_supports_multi_project_filter },
        { "cli_workspace_report_matrix_supports_filters",
          test_cli_workspace_report_matrix_supports_filters },
        { "cli_workspace_report_matrix_supports_multiple_artifact_filters",
          test_cli_workspace_report_matrix_supports_multiple_artifact_filters },
        { "cli_workspace_report_matrix_supports_multi_project_filters",
          test_cli_workspace_report_matrix_supports_multi_project_filters },
        { "cli_workspace_unqualified_artifact_filter_rejects_multi_project_"
          "selection",
          test_cli_workspace_unqualified_artifact_filter_rejects_multi_project_selection },
        { "cli_run_executes_facade_entry", test_cli_run_executes_facade_entry },
        { "cli_run_executes_requested_artifact_with_passthrough_args",
          test_cli_run_executes_requested_artifact_with_passthrough_args },
        { "cli_workspace_run_executes_selected_project",
          test_cli_workspace_run_executes_selected_project },
        { "cli_workspace_run_rejects_multiple_artifact_filters",
          test_cli_workspace_run_rejects_multiple_artifact_filters },
        { "cli_workspace_check_ci_rejects_multiple_artifact_filters",
          test_cli_workspace_check_ci_rejects_multiple_artifact_filters },
        { "cli_run_android_deploys_selected_artifact",
          test_cli_run_android_deploys_selected_artifact },
        { "cli_check_naming_reports_identifier_overflow_and_honors_allowlist",
          test_cli_check_naming_reports_identifier_overflow_and_honors_allowlist },
        { "personal_naming_uses_declarations_and_exact_policy_boundaries",
          test_personal_naming_uses_declarations_and_exact_policy_boundaries },
        { "personal_naming_reports_analysis_failures_and_owned_headers",
          test_personal_naming_reports_analysis_failures_and_owned_headers },
        { "personal_style_measures_control_bodies_and_local_scopes",
          test_personal_style_measures_control_bodies_and_local_scopes },
        { "personal_style_indexes_referents_and_measures_volume_without_size_"
          "gates",
          test_personal_style_indexes_referents_and_measures_volume_without_size_gates },
        { "cli_personal_style_preserves_workspace_scope_and_failure_status",
          test_cli_personal_style_preserves_workspace_scope_and_failure_status },
        { "cli_check_leaks_runs_sanitized_tests",
          test_cli_check_leaks_runs_sanitized_tests },
        { "cli_check_tidy_uses_clang_tidy_when_available",
          test_cli_check_tidy_uses_clang_tidy_when_available },
        { "cli_check_tidy_fails_when_clang_tidy_reports_diagnostics",
          test_cli_check_tidy_fails_when_clang_tidy_reports_diagnostics },
        { "tidy_requires_tool_and_complete_compilation_database",
          test_tidy_requires_tool_and_complete_compilation_database },
        { "cli_check_tidy_honors_full_artifact_identity_and_configuration",
          test_cli_check_tidy_honors_full_artifact_identity_and_configuration },
        { "cli_build_release_uses_release_build_type",
          test_cli_build_release_uses_release_build_type },
        { "build_tree_layout_nests_project_and_probe_profiles",
          test_build_tree_layout_nests_project_and_probe_profiles },
        { "render_benchmark_svg_uses_template_backed_surface",
          test_render_benchmark_svg_uses_template_backed_surface },
        { "cli_format_shares_verifier_selection_and_tool",
          test_cli_format_shares_verifier_selection_and_tool },
        { "cli_workspace_format_respects_selected_projects_and_artifacts",
          test_cli_workspace_format_respects_selected_projects_and_artifacts },
        { "cli_benchmark_accepts_generic_programs_and_preserves_results",
          test_cli_benchmark_accepts_generic_programs_and_preserves_results },
        { "cli_benchmark_builds_release_benchmarks_and_writes_reports",
          test_cli_benchmark_builds_release_benchmarks_and_writes_reports },
        { "cli_prerelease_builds_shareable_repos_and_auto_increments_version",
          test_cli_prerelease_builds_shareable_repos_and_auto_increments_version },
        { "prerelease_preserves_published_state_on_late_failures",
          test_prerelease_preserves_published_state_on_late_failures },
        { "prerelease_retains_recovery_state_and_rejects_publication_aliases",
          test_prerelease_retains_recovery_state_and_rejects_publication_aliases },
        { "prerelease_rejects_colliding_install_payloads",
          test_prerelease_rejects_colliding_install_payloads },
        { "cli_prerelease_includes_install_companions",
          test_cli_prerelease_includes_install_companions },
        { "cli_prerelease_signs_repo_metadata_when_requested",
          test_cli_prerelease_signs_repo_metadata_when_requested },
        { "cli_check_java_builds_shared_libraries_and_runs_gradle_tests",
          test_cli_check_java_builds_shared_libraries_and_runs_gradle_tests },
        { "cli_check_sphinx_generates_local_conf_with_rtd_theme",
          test_cli_check_sphinx_generates_local_conf_with_rtd_theme },
        { "cli_check_sphinx_theme_flag_sets_theme_override",
          test_cli_check_sphinx_theme_flag_sets_theme_override },
        { "cli_check_repo_rejects_legacy_repository_entries",
          test_cli_check_repo_rejects_legacy_repository_entries },
        { "cli_check_repo_rejects_tracked_surface_drift",
          test_cli_check_repo_rejects_tracked_surface_drift },
        { "cli_workspace_check_repo_rejects_gitlink_project_scripts",
          test_cli_workspace_check_repo_rejects_gitlink_project_scripts },
        { "generated_builds_reject_compiler_warnings_in_c_and_cxx",
          test_generated_builds_reject_compiler_warnings_in_c_and_cxx },
        { "cli_required_checks_propagate_failures_and_reject_empty_ctest_runs",
          test_cli_required_checks_propagate_failures_and_reject_empty_ctest_runs },
        { "cli_check_ci_fails_fast_on_repository_policy_drift",
          test_cli_check_ci_fails_fast_on_repository_policy_drift },
        { "cli_doctor_reports_declared_dependency_guidance",
          test_cli_doctor_reports_declared_dependency_guidance },
        { "cli_doctor_artifact_scope_filters_declared_dependencies",
          test_cli_doctor_artifact_scope_filters_declared_dependencies },
        { "cli_doctor_reports_configured_package_state_from_cache",
          test_cli_doctor_reports_configured_package_state_from_cache },
        { "cli_doctor_artifact_scope_filters_configured_package_state",
          test_cli_doctor_artifact_scope_filters_configured_package_state },
        { "cli_doctor_uses_requested_kde_cache_for_package_state",
          test_cli_doctor_uses_requested_kde_cache_for_package_state },
        { "cli_doctor_reports_tracked_surface_drift",
          test_cli_doctor_reports_tracked_surface_drift },
        { "cli_doctor_materializes_missing_configure_cache",
          test_cli_doctor_materializes_missing_configure_cache },
        { "cli_doctor_refreshes_stale_configure_cache",
          test_cli_doctor_refreshes_stale_configure_cache },
        { "cli_doctor_artifact_scope_refresh_uses_scoped_probe_cache",
          test_cli_doctor_artifact_scope_refresh_uses_scoped_probe_cache },
        { "cli_doctor_refreshes_stale_configure_state_without_cache_changes",
          test_cli_doctor_refreshes_stale_configure_state_without_cache_changes },
        { "cli_report_cxx_preserves_artifact_scope_and_failure_status",
          test_cli_report_cxx_preserves_artifact_scope_and_failure_status },
        { "cli_workspace_report_cxx_preserves_each_selected_owner",
          test_cli_workspace_report_cxx_preserves_each_selected_owner },
        { "cli_personal_reports_share_findings_and_presentation",
          test_cli_personal_reports_share_findings_and_presentation },
        { "cli_workspace_personal_reports_keep_all_selected_results",
          test_cli_workspace_personal_reports_keep_all_selected_results },
        { "clang_analysis_reports_unusable_inputs",
          test_clang_analysis_reports_unusable_inputs },
        { "clang_analysis_runs_on_sample_project",
          test_clang_analysis_runs_on_sample_project },
        { "clang_analysis_skips_benchmarks_and_generated_sources_by_default",
          test_clang_analysis_skips_benchmarks_and_generated_sources_by_default },
        { "clang_analysis_includes_benchmark_component_when_requested",
          test_clang_analysis_includes_benchmark_component_when_requested },
    };

    int failures = 0;
    int selected = 0;
    for (const auto& [name, test] : tests) {
        if (argc > 1 && name.find(argv[1]) == std::string::npos) {
            continue;
        }
        ++selected;
        try {
            test();
            std::cout << "[ok] " << name << "\n";
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[failed] " << name << ": " << error.what() << "\n";
        }
    }

    if (selected == 0) {
        std::cerr << "no tests match the requested filter\n";
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
