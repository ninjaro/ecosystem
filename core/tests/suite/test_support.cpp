#include "test_support.hpp"

namespace ecosystem_test_support {

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
    const std::string& main_source
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

void write_runtime_package_project(const fs::path& project) {
    write_text(project / "manifest.json", R"({
        "id":"runtime_sample", "description":"Packaged runtime closure", "facade":"app:main",
        "install_artifacts":["tool:cli"],
        "artifacts":[
            {"id":"leaf:lib", "kind":"shared_lib", "name":"leaf", "owns":["leaf"]},
            {"id":"middle:lib", "kind":"shared_lib", "name":"middle", "owns":["middle"], "dependencies":["leaf:lib"]},
            {"id":"bridge:lib", "kind":"static_lib", "name":"bridge", "owns":["bridge"], "dependencies":["middle:lib"]},
            {"id":"extra:lib", "kind":"shared_lib", "name":"extra", "owns":["extra"]},
            {"id":"app:main", "kind":"exe", "name":"runtime_app", "owns":[], "entry":"src/main.cpp", "dependencies":["bridge:lib"]},
            {"id":"tool:cli", "kind":"exe", "name":"runtime_tool", "owns":[], "entry":"src/tool.cpp", "dependencies":["extra:lib"]},
            {"id":"spare:lib", "kind":"shared_lib", "name":"spare", "owns":["spare"]}
        ]
    })");
    write_text(project / "src/leaf.cpp", "int leaf() { return 40; }\n");
    write_text(
        project / "src/middle.cpp",
        "int leaf(); int middle() { return leaf() + 1; }\n"
    );
    write_text(
        project / "src/bridge.cpp",
        "int middle(); int answer() { return middle() + 1; }\n"
    );
    write_text(project / "src/extra.cpp", "int extra() { return 42; }\n");
    write_text(
        project / "src/main.cpp",
        "int answer(); int main() { return answer() == 42 ? 0 : 1; }\n"
    );
    write_text(
        project / "src/tool.cpp",
        "int extra(); int main() { return extra() == 42 ? 0 : 1; }\n"
    );
    write_text(
        project / "src/spare.cpp", "#error unselected library must not build\n"
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

} // namespace ecosystem_test_support
