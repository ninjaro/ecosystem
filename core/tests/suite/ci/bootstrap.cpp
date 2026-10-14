#include "../ci.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_ci_compiler_cache_configuration() {
    temp_dir root;
    const auto action = ecosystem::render_required_text_template(
        "tracked/.github/actions/setup-ecosystem/action.yml.tpl",
        { { "checkout_action", "actions/checkout@v4" },
          { "install_qt_action", "vendor/qt@pin" } }
    );
    const auto script = root.path() / "cache.sh";
    const auto environment = root.path() / "environment";
    write_text(script, ci_shell(action, "Configure compiler cache", 6));
    auto result = ecosystem::capture_command_result(
        { "bash", "-e", "-o", "pipefail", script.string() }, root.path(),
        { { "GITHUB_WORKSPACE", root.path().string() },
          { "GITHUB_ENV", environment.string() } }
    );
    require_true(result.exit_code == 0, result.output);
    std::map<std::string, std::string> values;
    std::istringstream lines(read_text(environment));
    std::string line;
    while (std::getline(lines, line)) {
        const auto split = line.find('=');
        require_true(
            split != std::string::npos, "cache export must be KEY=VALUE"
        );
        values.emplace(line.substr(0, split), line.substr(split + 1));
    }
    require_true(
        values.contains("CCACHE_NODIRECT") && values.contains("CCACHE_NODEPEND")
            && values.at("CCACHE_NODIRECT") == "1"
            && values.at("CCACHE_NODEPEND") == "1"
            && !values.contains("CCACHE_DIRECT")
            && !values.contains("CCACHE_DEPEND"),
        "negative boolean variables must select preprocessor caching"
    );
    require_true(
        values.at("CCACHE_COMPILERCHECK") == "content"
            && values.at("CCACHE_SLOPPINESS").empty()
            && values.at("CCACHE_MAXSIZE") == "500M"
            && values.at("CMAKE_C_COMPILER_LAUNCHER") == "ccache"
            && values.at("CMAKE_CXX_COMPILER_LAUNCHER") == "ccache"
            && !values.contains("CCACHE_DISABLE"),
        "bootstrap must retain safe caching for both compilers"
    );
    const auto ccache = ecosystem::find_command_path("ccache");
    if (ccache.empty()) {
        std::cout << "[not-run] actual ccache configuration/compiler probe: "
                     "ccache is unavailable\n";
        return;
    }
    std::vector<std::pair<std::string, std::string>> build_environment(
        values.begin(), values.end()
    );
    for (const auto* option : { "direct_mode", "depend_mode", "disable" }) {
        result = ecosystem::capture_command_result(
            { ccache, "--get-config", option }, root.path(), build_environment
        );
        require_true(
            result.exit_code == 0 && result.output == "false\n",
            std::string("actual cache option must be false: ")
                .append(option)
                .append("\n")
                .append(result.output)
        );
    }
    write_text(
        root.path() / "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(cache_probe LANGUAGES C CXX)\n"
        "add_executable(c_probe main.c)\n"
        "add_executable(cxx_probe main.cpp)\n"
    );
    for (const auto* source : { "main.c", "main.cpp" })
        write_text(root.path() / source, "int main(void) { return 0; }\n");
    const auto build = root.path() / "build";
    result = ecosystem::capture_command_result(
        { "cmake", "-S", root.path().string(), "-B", build.string(),
          "-DCMAKE_BUILD_TYPE=Debug" },
        root.path(), build_environment
    );
    require_true(
        result.exit_code == 0,
        "actual C/C++ compiler detection with cache must succeed:\n"
            + result.output
    );
    result = ecosystem::capture_command_result(
        { "cmake", "--build", build.string(), "--parallel", "2" }, root.path(),
        build_environment
    );
    require_true(result.exit_code == 0, result.output);
    for (const auto* binary : { "c_probe", "cxx_probe" }) {
        result
            = ecosystem::capture_command_result({ (build / binary).string() });
        require_true(result.exit_code == 0, "cached probe must execute");
    }
    build_environment.emplace_back("CCACHE_DISABLE", "1");
    result = ecosystem::capture_command_result(
        { "cmake", "--build", build.string(), "--clean-first", "--parallel",
          "2" },
        root.path(), build_environment
    );
    require_true(
        result.exit_code == 0,
        "CodeQL's uncached traced compilation must remain valid:\n"
            + result.output
    );
}

void test_ci_bootstrap_options() {
    temp_dir root;
    const auto generate = [&](const json& config,
                              ecosystem::string_list* errors) {
        write_text(root.path() / "ecosystem.github.vars.json", config.dump());
        return ecosystem::generate_tracked_surface_files(
            sample_manifest(), root.path(), errors
        );
    };
    for (const auto& config : std::vector<json> {
             { { "ecosystem_bootstrap", "unknown" } },
             { { "ecosystem_repository", "example/tools" } },
             { { "ecosystem_ref", "reviewed-ref" } },
             { { "ecosystem_bootstrap", "checkout" },
               { "ecosystem_repository", "example/tools" },
               { "ecosystem_ref", "old" } },
             { { "ecosystem_source_path", "../tools" } },
             { { "ecosystem_source_path", "/tools" } },
             { { "ecosystem_source_path", "" } },
             { { "ecosystem_source_path", "tools\nnext" } },
             { { "ecosystem_source_path", "${{ github.ref }}" } },
             { { "ecosystem_build_parallelism", "0" } },
             { { "ecosystem_build_parallelism", "2\nnext" } } }) {
        ecosystem::string_list errors;
        generate(config, &errors);
        require_true(
            !errors.empty(),
            "invalid bootstrap inputs must fail generation: " + config.dump()
        );
    }
    for (const auto& config : std::vector<json> {
             json::object(),
             { { "ecosystem_bootstrap", "checkout" } },
             { { "ecosystem_repository", "example/tools" },
               { "ecosystem_ref", "release/preview" },
               { "ecosystem_source_path", "tools/ecosystem" } },
             { { "ecosystem_repository", "example/tools" },
               { "ecosystem_ref", "0123456789abcdef" },
               { "ecosystem_source_path", "tool source/#quoted" } } }) {
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
                workflow->contents, "ecosystem-ref: master",
                "no mutable implicit ref"
            );
            require_contains(
                workflow->contents,
                "bootstrap: "
                    + config.value("ecosystem_bootstrap", json("repository"))
                          .dump(),
                "workflow must forward explicit bootstrap mode as a YAML scalar"
            );
            require_contains(
                workflow->contents,
                "source-path: "
                    + config.value("ecosystem_source_path", json(".")).dump(),
                "workflow must preserve the selected source layout"
            );
        }
    }
}

void test_ci_bootstrap_layouts() {
    const auto action = ecosystem::render_required_text_template(
        "tracked/.github/actions/setup-ecosystem/action.yml.tpl",
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
    const auto prepare = ci_shell(action, "Set ecosystem paths", 6);
    const auto build = ci_shell(action, "Build Marx and Engels", 6);
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
    write_text(source / "assets/templates/version.txt", "runtime data\n");
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
        require_contains(
            action, "LOG_CFG: ${{ steps.paths.outputs.aqt-log-config }}",
            "the installer must use the ignored logging configuration"
        );
        const auto logging = ecosystem::capture_command_result(
            { "python3", "-c",
              "import logging.config, os; "
              "logging.config.fileConfig(os.environ['LOG_CFG']); "
              "logging.getLogger('aqt').info('bootstrap log probe')" },
            project, { { "LOG_CFG", values.at("aqt-log-config") } }
        );
        require_true(logging.exit_code == 0, logging.output);
        require_true(
            !fs::exists(project / "aqtinstall.log"),
            "Qt installer logging must not dirty the checkout root"
        );
        require_contains(
            read_text(
                project
                / ".ecosystem/github/reports/00-toolchain/aqtinstall.log"
            ),
            "bootstrap log probe", "installer diagnostics must be retained"
        );
        const auto build_script = project / "build.sh";
        write_text(build_script, build);
        const auto build_env
            = std::vector<std::pair<std::string, std::string>> {
                  { "CHECKOUT_ROOT", values.at("ecosystem-checkout-root") },
                  { "SOURCE_ROOT", values.at("ecosystem-source-root") },
                  { "BUILD_ROOT", values.at("ecosystem-build-root") },
                  { "GITHUB_WORKSPACE", project.string() },
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
        fs::remove_all(clone_root / "nested/tool source/assets/templates");
        result = ecosystem::capture_command_result(
            { "bash", "-e", "-o", "pipefail", build_script.string() }, project,
            build_env
        );
        require_true(
            result.exit_code != 0,
            "incompatible source revisions without runtime templates must fail"
        );
        write_text(
            clone_root / "nested/tool source/assets/templates/version.txt",
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

void test_ci_bootstrap_errors() {
    temp_dir root;
    const auto action = ecosystem::render_required_text_template(
        "tracked/.github/actions/setup-ecosystem/action.yml.tpl",
        { { "checkout_action", "actions/checkout@v4" },
          { "install_qt_action", "vendor/qt@pin" } }
    );
    const auto paths = root.path() / "paths.sh";
    write_text(paths, ci_shell(action, "Set ecosystem paths", 6));
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
    write_text(build, ci_shell(action, "Build Marx and Engels", 6));
    const auto result = ecosystem::capture_command_result(
        { "bash", "-e", "-o", "pipefail", build.string() }, root.path(),
        { { "CHECKOUT_ROOT", checkout.string() },
          { "SOURCE_ROOT", (checkout / "tooling").string() },
          { "BUILD_ROOT", (root.path() / "build").string() },
          { "GITHUB_WORKSPACE", root.path().string() },
          { "BUILD_PARALLELISM", "2" } }
    );
    require_true(
        result.exit_code != 0 && !fs::exists(root.path() / "build"),
        "symlinked source layouts cannot escape the selected revision"
    );
}

} // namespace ecosystem_test_support
