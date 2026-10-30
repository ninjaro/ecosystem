#include "../../suite/test_support.hpp"
#include "../release.hpp"

namespace ecosystem_test_support {

void test_release_runtime_closure() {
    temp_dir root;
    const auto project = root.path() / "project with spaces";
    write_runtime_package_project(project);
    auto authored = json::parse(read_text(project / "manifest.json"));
    authored["artifacts"].back()["packages"]["qt"] = json::array({ "Core" });
    write_text(project / "manifest.json", authored.dump(2));
    const auto tools = root.path() / "tools";
    const auto real_cmake = ecosystem::find_command_path("cmake");
    write_executable_script(
        tools / "cmake",
        "#!/bin/sh\nif [ \"$1\" = -S ]; then\n"
        "exec \"$ECOSYSTEM_TEST_REAL_CMAKE\" \"$@\" "
        "-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON\n"
        "fi\nexec \"$ECOSYSTEM_TEST_REAL_CMAKE\" \"$@\"\n"
    );
    scoped_env cmake("ECOSYSTEM_TEST_REAL_CMAKE", real_cmake);
    scoped_env path("PATH", tools.string() + ":" + current_path_env());
    auto naming = sample_dual_run_manifest();
    auto unrelated = naming.components.back();
    unrelated.id = "unrelated";
    naming.components.push_back(unrelated);
    const auto before = ecosystem::to_json(naming);
    const auto selected_tool
        = ecosystem::distribution_build_manifest(naming, { "tool", "cli" });
    const auto tool = ecosystem::resolve_artifact(
        selected_tool, ecosystem::artifact_ref { "tool", "cli" }
    );
    require_true(
        tool && selected_tool.install_artifacts.empty()
            && !ecosystem::resolve_artifact(
                selected_tool, ecosystem::artifact_ref { "app", "app" }
            )
            && !ecosystem::resolve_artifact(
                selected_tool, ecosystem::artifact_ref { "unrelated", "cli" }
            ),
        "an explicit non-facade package must not acquire unrelated "
        "distribution roots"
    );
    require_true(
        tool
            && ecosystem::artifact_output_name(
                   selected_tool, *tool->component_value, *tool->artifact_value
               ) == "tool_cli",
        "project-wide output identities survive operation-local selection"
    );
    require_true(
        ecosystem::to_json(naming) == before,
        "distribution selection must not mutate authored intent"
    );
    const auto result
        = run_marx_cli(project, "prerelease --version-base 1.2.3");
    require_true(
        result.exit_code == 0,
        "native runtime packaging must succeed:\n" + result.output
    );
    const auto release = ecosystem::local_prerelease_dir(project);
    const auto input = prepared_input(project, result);
    const auto payload = prepared_payload(input);
    require_true(
        !fs::exists(release / "state.json") && !fs::exists(release / "deb")
            && !fs::exists(release / "pacman"),
        "runtime preparation is not publication"
    );
    const auto inventory = json::parse(read_text(
        first_recursive_file_named(release / "attempts", "runtime.json")
    ));
    require_true(
        inventory.at("bundled").size() == 3 && !inventory.at("system").empty(),
        "runtime inventory must distinguish three linked project libraries "
        "from system requirements"
    );
    const auto source_binary
        = ecosystem::local_build_dir(project, "release") / "runtime_app";
    const auto original = ecosystem::capture_command_result(
        { "readelf", "-d", source_binary.string() }
    );
    require_true(original.exit_code == 0, "source ELF inspection must succeed");
    require_contains(
        original.output, project.string(),
        "package relocation must not alter the original build binary"
    );

    const auto pacman = root.path() / "pacman-prefix";
    const auto debian = root.path() / "debian-prefix";
    fs::copy(payload, pacman, fs::copy_options::recursive);
    fs::copy(payload, debian, fs::copy_options::recursive);
    fs::rename(project, root.path() / "unavailable-project");
    for (const auto& prefix : { pacman, debian }) {
        for (const auto& library :
             { "libleaf.so", "libmiddle.so", "libextra.so" })
            require_true(
                fs::is_regular_file(prefix / "usr/lib" / library),
                "archive must contain linked runtime libraries"
            );
        require_true(
            !fs::exists(prefix / "usr/lib/libbridge.a")
                && !fs::exists(prefix / "usr/lib/libspare.so"),
            "runtime packaging must exclude intermediate archives and "
            "unrelated libraries"
        );
        for (const auto& executable : { "runtime_app", "runtime_tool" }) {
            const auto binary = prefix / "usr/bin" / executable;
            const auto dynamic = ecosystem::capture_command_result(
                { "readelf", "-d", binary.string() }
            );
            require_true(
                dynamic.exit_code == 0, "packaged ELF inspection must succeed"
            );
            require_not_contains(
                dynamic.output, "RPATH", "package must not retain build RPATH"
            );
            require_not_contains(
                dynamic.output, "RUNPATH",
                "package must not retain build RUNPATH"
            );
            const auto run = ecosystem::capture_command_result(
                { binary.string() }, root.path(),
                { { "LD_LIBRARY_PATH", (prefix / "usr/lib").string() },
                  { "LD_PRELOAD", "" } }
            );
            require_true(
                run.exit_code == 0,
                "extracted application must run using only its installed "
                "project libraries:\n"
                    + run.output
            );
        }
    }
}

void test_release_provider_runtime() {
    temp_dir root;
    write_source_dependency_fixture(root.path());
    const auto provider = root.path() / "provider";
    const auto consumer = root.path() / "consumer";
    for (const auto& project : { provider, consumer }) {
        auto authored = json::parse(read_text(project / "manifest.json"));
        authored["artifacts"][0]["kind"] = "shared_lib";
        if (project == consumer)
            authored["artifacts"][1]["name"] = "consumer";
        write_text(project / "manifest.json", authored.dump(2));
    }
    write_text(
        consumer / "ecosystem.local.json",
        json(
            { { "sources",
                { { "https://example.invalid/numbers.git",
                    provider.string() } } } }
        ).dump(2)
    );
    scoped_env override_path(
        "NUMBERS_SOURCE_DIR", (root.path() / "invalid-legacy-override").string()
    );
    const auto result
        = run_marx_cli(consumer, "prerelease --version-base 1.0.0");
    require_true(
        result.exit_code == 0,
        "source provider runtime must package:\n" + result.output
    );
    const auto release = ecosystem::local_prerelease_dir(consumer);
    const auto transcript = read_text(
        first_recursive_file_named(release / "attempts", "commands.log")
    );
    require_contains(
        transcript, "--install",
        "provider installation commands must join the attempt transcript"
    );
    require_contains(
        transcript, "numbers",
        "provider configure/build output must join the attempt transcript"
    );
    const auto inventory = json::parse(read_text(
        first_recursive_file_named(release / "attempts", "runtime.json")
    ));
    require_true(
        inventory.at("bundled").size() == 1,
        "installed provider shared library must be bundled once"
    );
    require_contains(
        inventory.at("bundled").at(0).at("source").get<std::string>(),
        "/install/release/",
        "provider runtime must come from its installed package"
    );
    const auto prefix = root.path() / "installed";
    fs::copy(
        prepared_payload(prepared_input(consumer, result)), prefix,
        fs::copy_options::recursive
    );
    for (const auto& entry : fs::recursive_directory_iterator(prefix)) {
        require_true(
            entry.path().filename() != "ecosystem.local.json",
            "release payload must exclude machine-local source mappings"
        );
    }
    fs::rename(provider, root.path() / "unavailable-provider");
    fs::rename(consumer, root.path() / "unavailable-consumer");
    const auto run = ecosystem::capture_command_result(
        { (prefix / "usr/bin/consumer").string() }, root.path(),
        { { "LD_LIBRARY_PATH", (prefix / "usr/lib").string() },
          { "LD_PRELOAD", "" } }
    );
    require_true(
        run.exit_code == 0,
        "packaged consumer must run without either project's "
        "source/build/install trees:\n"
            + run.output
    );
}

void test_release_runtime_aliases() {
    temp_dir root;
    const auto project = root.path() / "project";
    const auto first_lib = project / "version-one";
    const auto second_lib = project / "version-two";
    const auto primary = project / "binaries/app";
    const auto companion
        = ecosystem::local_build_dir(project, "release") / "cli";
    write_text(project / "value.cpp", "int value() { return 42; }\n");
    write_text(
        project / "main.cpp",
        "int value(); int main() { return value() == 42 ? 0 : 1; }\n"
    );
    auto command = [&](const std::vector<std::string>& arguments) {
        const auto run = ecosystem::capture_command_result(arguments, project);
        require_true(
            run.exit_code == 0,
            "versioned native fixture must build:\n" + run.output
        );
    };
    auto library = [&](const fs::path& directory) {
        fs::create_directories(directory);
        command(
            { "c++", "-shared", "-fPIC", (project / "value.cpp").string(),
              "-Wl,-soname,libversioned.so.3", "-o",
              (directory / "libversioned.so.3.2").string() }
        );
        fs::create_symlink(
            "libversioned.so.3.2", directory / "libversioned.so.3"
        );
        fs::create_symlink("libversioned.so.3", directory / "libversioned.so");
    };
    auto application = [&](const fs::path& output, const fs::path& directory) {
        fs::create_directories(output.parent_path());
        command(
            { "c++", (project / "main.cpp").string(), "-L" + directory.string(),
              "-lversioned", "-Wl,-rpath," + directory.string(), "-o",
              output.string() }
        );
    };
    library(first_lib);
    application(primary, first_lib);
    application(companion, first_lib);
    auto value = sample_dual_run_manifest();
    value.install_artifacts = { "tool:cli" };
    const auto selected = ecosystem::resolve_artifact(
        value, ecosystem::artifact_ref { "app", "app" }
    );
    require_true(selected.has_value(), "native fixture primary must resolve");
    std::string error;
    fs::path prepared;
    auto prepare = [&]() {
        try {
            prepared = prepare_fixture(project, value, *selected, primary);
            return ecosystem::command_error::ok;
        } catch (const std::exception& failure) {
            error = failure.what();
            return ecosystem::command_error::task_failed;
        }
    };
    require_true(
        prepare() == ecosystem::command_error::ok,
        "versioned runtime must prepare: " + error
    );
    const auto prefix = root.path() / "installed";
    fs::copy(
        prepared.parent_path() / "payload", prefix, fs::copy_options::recursive
    );
    for (const auto& name : { "libversioned.so.3", "libversioned.so.3.2" })
        require_true(
            fs::is_regular_file(prefix / "usr/lib" / name),
            "SONAME and target filename must both survive staging"
        );
    fs::rename(first_lib, project / "unavailable-version");
    const auto run = ecosystem::capture_command_result(
        { (prefix / "usr/bin/app").string() }, root.path(),
        { { "LD_LIBRARY_PATH", (prefix / "usr/lib").string() },
          { "LD_PRELOAD", "" } }
    );
    require_true(
        run.exit_code == 0,
        "installed SONAME must resolve without source library paths:\n"
            + run.output
    );
    fs::rename(project / "unavailable-version", first_lib);

    const auto release = ecosystem::local_prerelease_dir(project);
    write_text(release / "state.json", "untouched release state");
    const auto state = read_text(release / "state.json");
    library(second_lib);
    application(companion, second_lib);
    require_true(
        prepare() == ecosystem::command_error::task_failed,
        "ambiguous runtime names must fail publication"
    );
    require_contains(
        error, "Conflicting package runtime libraries",
        "collision must name the CMake dependency conflict"
    );
    require_contains(
        error, "libversioned.so.3", "conflict must identify the SONAME"
    );
    require_true(
        read_text(release / "state.json") == state,
        "runtime conflicts must preserve release state"
    );

    const auto outside = root.path() / "unmanaged-library";
    library(outside);
    application(primary, outside);
    application(companion, outside);
    require_true(
        prepare() == ecosystem::command_error::task_failed,
        "unmanaged non-system runtime dependencies must fail clearly"
    );
    require_contains(
        error, "outside the project and system library directories",
        "unsupported runtime location must have explicit guidance"
    );
    require_true(
        read_text(release / "state.json") == state,
        "unsupported runtime location must preserve release state"
    );
}

void test_release_runtime_recovery() {
    temp_dir root;
    const auto project = root.path() / "project";
    write_runtime_package_project(project);
    auto result = run_marx_cli(project, "prerelease --version-base 1.0.0");
    require_true(
        result.exit_code == 0,
        "initial runtime release must succeed:\n" + result.output
    );
    const auto release = ecosystem::local_prerelease_dir(project);
    write_text(release / "state.json", "untouched release state");
    const auto state = read_text(release / "state.json");
    const auto package = prepared_payload(prepared_input(project, result))
        / "usr/bin/runtime_app";
    const auto previous_package = read_text(package);
    const auto loaded = ecosystem::load_manifest(project / "manifest.json");
    require_true(loaded.value.has_value(), "runtime fixture must load");
    const auto resolved
        = ecosystem::resolve_artifact(*loaded.value, std::nullopt);
    const auto build = ecosystem::local_build_dir(project, "release");
    std::string error;
    auto prepare = [&]() {
        bool rejected = false;
        try {
            (void)prepare_fixture(
                project, *loaded.value, *resolved, build / "runtime_app"
            );
        } catch (const std::exception& failure) {
            rejected = true;
            error = failure.what();
        }
        require_true(
            rejected, "runtime inspection failure must stop preparation"
        );
        require_true(
            read_text(release / "state.json") == state
                && read_text(package) == previous_package,
            "runtime failures must preserve the previous package and version "
            "counter"
        );
    };
    fs::rename(build / "libleaf.so", root.path() / "unavailable-library.so");
    prepare();
    require_contains(
        error, "Unresolved package runtime libraries",
        "missing runtime must be diagnosed before publication"
    );
    require_contains(
        error, "libleaf.so",
        "runtime failure must identify its unresolved library"
    );
    fs::rename(root.path() / "unavailable-library.so", build / "libleaf.so");
    const auto bin = root.path() / "tools";
    write_executable_script(
        bin / "cmake",
        "#!/bin/sh\nprintf 'runtime scanner failed\\n' >&2\nexit 17\n"
    );
    scoped_env path("PATH", bin.string() + ":" + current_path_env());
    prepare();
    require_contains(
        error, "runtime scanner failed",
        "native scanner errors must remain attributable"
    );
    bool retained_failure = false;
    for (const auto& entry :
         fs::recursive_directory_iterator(release / "attempts"))
        if (entry.is_regular_file()
            && entry.path().filename() == "runtime-scan.log")
            retained_failure = retained_failure
                || read_text(entry.path()).find("runtime scanner failed")
                    != std::string::npos;
    require_true(
        retained_failure,
        "each failed preparation retains its own runtime scanner log"
    );
}

} // namespace ecosystem_test_support
