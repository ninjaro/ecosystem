#include "../../suite/test_support.hpp"
#include "../assets.hpp"
#include "workspace/assets.hpp"
#include "workspace/template_text.hpp"

#include <algorithm>

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
            if (entry.path().filename().string().starts_with("manifesto_assets_"
                )
                && entry.path().extension() == ".cmake")
                stage = entry.path();
        require_true(!stage.empty(), "the build owns its staging script");
        require_contains(
            read_text(stage), "cmake_minimum_required(VERSION 3.20)",
            "standalone post-build staging must establish its own CMake "
            "policies"
        );
        const auto policy_probe = root.path() / "stage-policy.cmake";
        write_text(
            policy_probe,
            read_text(stage)
                + "\ncmake_policy(GET CMP0057 stage_policy)\n"
                  "if (NOT stage_policy STREQUAL \"NEW\")\n"
                  "  message(FATAL_ERROR \"Staging lacks IN_LIST policy\")\n"
                  "endif ()\n"
        );
        const auto standalone = ecosystem::capture_command_result(
            { "cmake", "-Dasset_source=" + (project / "assets").string(),
              "-Dasset_destination="
                  + (root.path() / "standalone-assets").string(),
              "-P", policy_probe.string() },
            project
        );
        require_true(standalone.exit_code == 0, standalone.output);
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
    const auto inventory = ecosystem::render_required_text_template(
        "cmake/assets_inventory.tpl",
        { { "runtime_categories", categories },
          { "runtime_cache_pattern",
            "[==[" + ecosystem::runtime_asset_cache_pattern() + "]==]" } }
    );
    write_text(
        project / "CMakeLists.txt",
        R"cmake(cmake_minimum_required(VERSION 3.20)
project(resource_probe LANGUAGES NONE)
function(qt_add_resources target_name)
    cmake_parse_arguments(resources "" "PREFIX;BASE" "FILES" ${ARGN})
    file(WRITE "${CMAKE_BINARY_DIR}/resource_files.txt" "${resources_FILES}")
endfunction()
)cmake" + inventory
            + "\nfunction(embed target_name)\n" + block
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

void test_asset_cache_policy() {
    temp_dir root;
    const auto project = root.path() / "resource cache fixture with # spaces";
    const json authored {
        { "id", "cache_assets" },
        { "description", "Runtime cache policy" },
        { "facade", "app:probe" },
        { "install_assets", true },
        { "artifacts",
          json::array({ { { "id", "app:probe" },
                          { "kind", "qt_app" },
                          { "root", "app" },
                          { "owns", json::array() },
                          { "entry", "src/main.cpp" },
                          { "name", "resource_probe" },
                          { "packages", { { "qt", { "Core" } } } } } }) }
    };
    std::vector<std::string> retained {
        "brand/logo.svg",
        "sets/catalog.json",
        "sets/card_set_cache/art.svg",
        "sets/.card_set_cache_reference/notes.json",
        "sets/.card_set_cache.svg",
        "sets/.cache",
        "dataset/test/input.json",
        "dumps/test/generated.json",
        "reports/result--latest.json",
        "templates/message.json",
        "sets/showcase/nested.svg"
    };
    std::vector<std::string> caches;
    for (const auto* directory : { "sets/.card_set_cache", "brand/.cache",
                                   "dataset/test/.thumbnail_cache" })
        for (const auto* file :
             { "original.svg", "metadata.json", "download.bin" })
            caches.emplace_back(std::string(directory) + '/' + file);
    const auto svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" "
                     "height=\"8\"><rect width=\"8\" height=\"8\"/></svg>\n";
    write_text(project / "manifest.json", authored.dump(2));
    for (const auto& name : retained)
        write_text(
            project / "assets" / name,
            name.ends_with(".svg") ? svg : "{\"fixture\":true}\n"
        );
    for (const auto& name : caches)
        write_text(
            project / "assets" / name,
            name.ends_with(".svg")        ? svg
                : name.ends_with(".json") ? "{\"cache\":true}\n"
                                          : std::string("\0cache", 6)
        );
    write_text(project / "generated/result.json", "{\"generated\":true}\n");
    fs::create_symlink("../brand/logo.svg", project / "assets/sets/linked.svg");
    fs::create_directory_symlink("../brand", project / "assets/sets/icons");
    fs::create_symlink(
        "../../generated/result.json", project / "assets/sets/generated.json"
    );
    retained.insert(
        retained.end(),
        { "sets/linked.svg", "sets/icons/logo.svg", "sets/generated.json" }
    );
    // Safe aliases cannot launder files out of reserved physical cache paths.
    fs::create_symlink(
        "../brand/.cache/original.svg", project / "assets/sets/cache_ref.svg"
    );
    caches.emplace_back("sets/cache_ref.svg");
    write_text(project / ".work_cache/independent.svg", svg);
    write_text(project / "assets/showcase/demo.svg", svg);
    // Git-ignored source-generated output is still a supported runtime
    // category.
    write_text(project / "assets/.gitignore", "/dumps/\n");
    std::string main
        = "#include <QCoreApplication>\n#include <QFile>\n"
          "int main(int argc,char** argv) { QCoreApplication app(argc,argv);\n";
    for (const auto& name : retained)
        main += "if(!QFile::exists(QStringLiteral(\":/cache_assets/" + name
            + "\"))) return 1;\n";
    for (const auto& name : caches)
        main += "if(QFile::exists(QStringLiteral(\":/cache_assets/" + name
            + "\"))) return 2;\n";
    main += "return 0; }\n";
    write_text(project / "app/src/main.cpp", main);
    const auto loaded = ecosystem::load_manifest(project / "manifest.json");
    require_true(
        loaded.value.has_value() && loaded.errors.empty(),
        join_lines(loaded.errors)
    );
    const auto synced = ecosystem::sync_project(project, *loaded.value);
    require_true(synced.errors.empty(), join_lines(synced.errors));
    const auto selected = ecosystem::runtime_asset_files(project);
    require_true(
        selected.size() == retained.size(),
        "C++ package inventory must match CMake resources"
    );
    for (const auto& path : selected)
        require_true(
            std::find(
                retained.begin(), retained.end(),
                path.lexically_relative(project / "assets").generic_string()
            ) != retained.end(),
            "package inventory must contain only retained resources"
        );
    const auto ignore = read_text(project / ".gitignore");
    require_contains(ignore, "**/.cache/", "standalone caches must be ignored");
    require_contains(
        ignore, "**/.*_cache/", "named hidden caches must be ignored"
    );
    require_not_contains(
        ignore, "**/card_set_cache/",
        "authored lookalikes must remain trackable"
    );
    const auto build = root.path() / "build";
    const auto prefix = root.path() / "installed";
    write_text(build / "assets/sets/.card_set_cache/obsolete.svg", svg);
    const auto run = [&](const std::vector<std::string>& command) {
        const auto result = ecosystem::capture_command_result(command, project);
        require_true(result.exit_code == 0, result.output);
    };
    run({ "cmake", "-G", "Ninja", "-S", project.string(), "-B", build.string(),
          "-DCMAKE_BUILD_TYPE=Debug" });
    run({ "cmake", "--build", build.string(), "--parallel", "2" });
    run({ (build / "resource_probe").string() });
    run({ "cmake", "--install", build.string(), "--prefix", prefix.string() });
    const auto inputs = read_text(build / ".qt/rcc/cache_assets_assets.qrc");
    for (const auto& name : retained) {
        require_contains(inputs, name, "Qt resource must retain " + name);
        for (const auto& directory :
             { build / "assets", prefix / "share/cache_assets/assets" })
            require_true(
                read_text(directory / name)
                    == read_text(project / "assets" / name),
                "runtime payload bytes must match: " + name
            );
    }
    for (const auto& name : caches) {
        require_not_contains(inputs, name, "Qt resource must exclude " + name);
        require_true(
            !fs::exists(build / "assets" / name)
                && !fs::exists(prefix / "share/cache_assets/assets" / name),
            "cache must not enter stage/install: " + name
        );
    }
    require_true(
        !fs::exists(build / "assets/sets/.card_set_cache"),
        "previously staged cache directories must be retired"
    );
    for (const auto& directory :
         { build / "assets", prefix / "share/cache_assets/assets" }) {
        std::size_t count = 0;
        for (const auto& entry : fs::recursive_directory_iterator(directory))
            if (entry.is_regular_file())
                ++count;
        require_true(
            count == retained.size(), "exact stage/install resource inventory"
        );
    }
}

void test_asset_inventory_paths() {
    temp_dir root;
    // An ancestor's cache-like name must not exclude the selected project.
    const auto project = root.path() / ".project_cache";
    write_text(project / "assets/brand/icon.svg", "authored artwork");
    write_text(project / "generated/result.json", "{\"generated\":true}\n");
    fs::create_directories(project / "assets/sets/.cache");
    write_text(project / "assets/sets/.cache/skip.svg", "transient artwork");
    fs::create_symlink("../brand/icon.svg", project / "assets/sets/icon.svg");
    fs::create_symlink(
        "../../generated/result.json", project / "assets/sets/generated.json"
    );
    fs::create_directory_symlink("../brand", project / "assets/sets/icons");
    fs::create_symlink(
        ".cache/skip.svg", project / "assets/sets/cache_ref.svg"
    );
    fs::create_directory_symlink(".cache", project / "assets/sets/cache_alias");
    std::string categories;
    for (const auto& category : ecosystem::runtime_asset_categories())
        categories += category + ' ';
    const auto inventory = ecosystem::render_required_text_template(
        "cmake/assets_inventory.tpl",
        { { "runtime_categories", categories },
          { "runtime_cache_pattern",
            "[==[" + ecosystem::runtime_asset_cache_pattern() + "]==]" } }
    );
    const auto script = root.path() / "inventory.cmake";
    write_text(
        script,
        "cmake_minimum_required(VERSION 3.20)\n" + inventory
            + "\n_ecosystem_runtime_asset_files([==["
            + (project / "assets").string() + "]==] files)\nfile(WRITE [==["
            + (root.path() / "selected.txt").string() + "]==] \"${files}\")\n"
    );
    const std::vector<std::string> expected { "brand/icon.svg",
                                              "sets/generated.json",
                                              "sets/icon.svg",
                                              "sets/icons/icon.svg" };
    const auto cpp = ecosystem::runtime_asset_files(project);
    require_true(
        cpp.size() == expected.size(),
        "internal links keep logical aliases and reject physical caches"
    );
    const auto result
        = ecosystem::capture_command_result({ "cmake", "-P", script.string() });
    require_true(result.exit_code == 0, result.output);
    std::string selected;
    for (const auto& file : cpp) {
        require_true(
            std::find(
                expected.begin(), expected.end(),
                file.lexically_relative(project / "assets").generic_string()
            ) != expected.end(),
            "safe logical aliases must be retained"
        );
        if (!selected.empty())
            selected += ';';
        selected += file.string();
    }
    require_true(
        read_text(root.path() / "selected.txt") == selected,
        "CMake and C++ inventories must have identical ordering and aliases"
    );
    const auto rejects = [&] {
        bool failed = false;
        try {
            static_cast<void>(ecosystem::runtime_asset_files(project));
        } catch (const std::exception&) {
            failed = true;
        }
        require_true(failed, "C++ inventory must reject unsafe paths");
        const auto cmake
            = ecosystem::capture_command_result({ "cmake", "-P",
                                                  script.string() });
        require_true(
            cmake.exit_code != 0,
            "CMake inventory must reject the same unsafe paths"
        );
    };
    write_text(root.path() / "outside.svg", "outside selected root");
    fs::create_symlink(
        root.path() / "outside.svg", project / "assets/sets/escape.svg"
    );
    rejects();
    fs::remove(project / "assets/sets/escape.svg");
    fs::create_symlink("missing.svg", project / "assets/sets/dangling.svg");
    rejects();
    fs::remove(project / "assets/sets/dangling.svg");
    fs::create_directory_symlink(".", project / "assets/sets/cycle");
    rejects();
}

void test_asset_tooling_install_policy() {
    temp_dir root;
    const auto project = root.path() / "tooling-install";
    const json authored { { "id", "manifesto" },
                          { "description", "Tooling data install fixture" },
                          { "facade", "tools:marx" },
                          { "install_assets", false },
                          { "artifacts",
                            json::array({ { { "id", "tools:marx" },
                                            { "kind", "exe" },
                                            { "root", "core" },
                                            { "owns", json::array() },
                                            { "entry", "src/main.cpp" },
                                            { "name", "marx" } } }) } };
    write_text(project / "manifest.json", authored.dump(2));
    write_text(project / "core/src/main.cpp", "int main() { return 0; }\n");
    write_text(
        project / "assets/templates/tracked/.github/required.tpl",
        "required template\n"
    );
    write_text(
        project / "assets/templates/cache/reference.tpl", "authored lookalike\n"
    );
    write_text(
        project / "assets/templates/.cache/ignored.tpl", "transient template\n"
    );
    write_text(
        project / "assets/templates/nested/.renderer_cache/ignored.json", "{}\n"
    );
    const auto loaded = ecosystem::load_manifest(project / "manifest.json");
    require_true(
        loaded.value.has_value(), "tooling install manifest must load"
    );
    const auto synced = ecosystem::sync_project(project, *loaded.value);
    require_true(synced.errors.empty(), join_lines(synced.errors));
    const auto build = root.path() / "build";
    const auto prefix = root.path() / "installed";
    for (const auto& args : std::vector<std::vector<std::string>> {
             { "cmake", "-S", project.string(), "-B", build.string(), "-G",
               "Ninja" },
             { "cmake", "--build", build.string(), "--parallel", "2" },
             { "cmake", "--install", build.string(), "--prefix",
               prefix.string() } }) {
        const auto result = ecosystem::capture_command_result(args, project);
        require_true(result.exit_code == 0, result.output);
    }
    const auto bundle = prefix / "share/manifesto/templates";
    require_true(
        read_text(bundle / "tracked/.github/required.tpl")
            == "required template\n",
        "tooling installation must retain hidden required files independently "
        "of install_assets"
    );
    require_true(
        read_text(bundle / "cache/reference.tpl") == "authored lookalike\n",
        "tooling installation must retain authored lookalikes"
    );
    require_true(
        !fs::exists(bundle / ".cache")
            && !fs::exists(bundle / "nested/.renderer_cache"),
        "tooling installation must share the canonical cache exclusion"
    );
}

} // namespace ecosystem_test_support
