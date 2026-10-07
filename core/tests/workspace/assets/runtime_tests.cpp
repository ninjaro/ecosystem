#include "../../suite/test_support.hpp"
#include "../assets.hpp"
#include "workspace/assets.hpp"
#include "workspace/template_text.hpp"

namespace ecosystem_test_support {

void test_asset_runtime_surface() {
    for (const auto& [generator, directory] :
         std::vector<std::pair<std::string, std::string>> {
             { "Unix Makefiles", "project with spaces" },
             { "Ninja", "project with # spaces" } }) {
        temp_dir root;
        const auto project = root.path() / directory;
        write_sample_build_project(project, "asset_runtime");
        const auto report = ecosystem::load_manifest(project / "manifest.json");
        require_true(
            report.value.has_value() && report.errors.empty(),
            "runtime fixture must load"
        );
        auto value = *report.value;
        value.install_assets = true;
        std::string error;
        require_true(
            ecosystem::save_manifest(project / "manifest.json", value, &error),
            error
        );
        const std::vector<std::string> files {
            "brand/logo.svg",           "reports/run--latest.txt",
            "dataset/test/input.dat",   "dumps/test/output.dat",
            "sets/config.json",         "templates/message.txt",
            "sets/showcase/runtime.dat"
        };
        for (const auto& name : files)
            write_text(project / "assets" / name, "runtime fixture " + name);
        write_text(
            project / "assets/showcase/demo.pdf", "presentation fixture"
        );
        write_text(project / "assets/.gitignore", "/dumps/\n");
        const auto synced = ecosystem::sync_project(project, value);
        require_true(synced.errors.empty(), join_lines(synced.errors));
        const auto build = root.path() / "build";
        const auto prefix = root.path() / "installed";
        write_text(build / "assets/showcase/old.pdf", "stale presentation");
        write_text(
            root.path() / "retained/keep.txt", "retain outside the output"
        );
        fs::create_directory_symlink(
            root.path() / "retained", build / "assets/showcase/alias"
        );
        for (const auto& command : std::vector<std::vector<std::string>> {
                 { "cmake", "-G", generator, "-S", project.string(), "-B",
                   build.string(), "-DCMAKE_BUILD_TYPE=Debug" },
                 { "cmake", "--build", build.string(), "-j2" },
                 { "cmake", "--install", build.string(), "--prefix",
                   prefix.string() } }) {
            const auto result
                = ecosystem::capture_command_result(command, project);
            require_true(result.exit_code == 0, result.output);
        }
        for (const auto& name : files)
            require_true(
                fs::is_regular_file(build / "assets" / name)
                    && fs::is_regular_file(
                        prefix / "share/asset_runtime/assets" / name
                    ),
                "native stage/install must retain runtime category: " + name
            );
        require_true(
            !fs::exists(build / "assets/showcase")
                && !fs::exists(prefix / "share/asset_runtime/assets/showcase")
                && !fs::exists(build / "assets/.gitignore"),
            "showcase and root metadata must stay out of runtime assets"
        );
        require_true(
            read_text(project / "assets/showcase/demo.pdf")
                    == "presentation fixture"
                && read_text(root.path() / "retained/keep.txt")
                    == "retain outside the output",
            "retiring stale output must preserve media and alias targets"
        );
        const auto scripts = fs::directory_iterator(build);
        fs::path stage;
        for (const auto& entry : scripts)
            if (entry.path().filename().string().starts_with(
                    "manifesto_assets_"
                )
                && entry.path().extension() == ".cmake")
                stage = entry.path();
        require_true(!stage.empty(), "the build owns its staging script");
        const auto alias = root.path() / "aliased-assets";
        fs::create_directory_symlink(root.path() / "retained", alias);
        const auto result = ecosystem::capture_command_result(
            { "cmake", "-Dasset_source=" + (project / "assets").string(),
              "-Dasset_destination=" + alias.string(), "-P", stage.string() },
            project
        );
        require_true(
            result.exit_code != 0
                && read_text(root.path() / "retained/keep.txt")
                    == "retain outside the output",
            "staging must reject a destination alias before copying or "
            "removing "
            "output"
        );
    }
}

void test_asset_resource_inputs() {
    temp_dir root;
    const auto project = root.path() / "Qt inputs with # spaces";
    write_text(project / "assets/brand/logo.svg", "brand");
    write_text(project / "assets/sets/showcase/retained.dat", "runtime");
    write_text(project / "assets/showcase/excluded.pdf", "presentation");
    std::string categories;
    for (const auto& category : ecosystem::runtime_asset_categories())
        categories += category + ' ';
    const auto block = ecosystem::render_required_text_template(
        "cmake/assets_embed.tpl",
        { { "source_root_expression", "${CMAKE_CURRENT_SOURCE_DIR}" },
          { "project_id", "resource_probe" },
          { "runtime_categories", categories } }
    );
    write_text(
        project / "CMakeLists.txt",
        R"cmake(cmake_minimum_required(VERSION 3.20)
project(resource_probe LANGUAGES NONE)
function(qt_add_resources target_name)
    cmake_parse_arguments(resources "" "PREFIX;BASE" "FILES" ${ARGN})
    file(WRITE "${CMAKE_BINARY_DIR}/resource_files.txt" "${resources_FILES}")
endfunction()
function(embed target_name)
)cmake" + block
            + "endfunction()\nembed(probe)\n"
    );
    const auto build = root.path() / "build";
    const auto result = ecosystem::capture_command_result(
        { "cmake", "-S", project.string(), "-B", build.string() }, project
    );
    require_true(result.exit_code == 0, result.output);
    const auto selected = read_text(build / "resource_files.txt");
    require_contains(
        selected, "/assets/brand/logo.svg",
        "Qt inputs include runtime brand files"
    );
    require_contains(
        selected, "/assets/sets/showcase/retained.dat",
        "nested runtime directories may be named showcase"
    );
    require_not_contains(
        selected, "/assets/showcase/",
        "Qt resources must exclude root showcase media"
    );
}

} // namespace ecosystem_test_support
