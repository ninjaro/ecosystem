#include "prepared_internal.hpp"
#include "release/internal.hpp"
#include "workspace/assets.hpp"
#include "workspace/template_text.hpp"

#include <fstream>
#include <sstream>

namespace ecosystem::prepared_detail {
namespace {

    void
    add(context& value, const fs::path& destination, const fs::path& source,
        const std::string& artifact, const std::string& role,
        const bool runtime = false) {
        const auto key = release_detail::contract_path(destination);
        require(
            fs::is_regular_file(source),
            "missing current reference: " + source.string()
        );
        const auto [position, inserted] = value.expected.emplace(
            key, reference_file { source, artifact, role, runtime }
        );
        require(
            inserted
                || (position->second.source == source
                    && position->second.role == role),
            "conflicting authoritative install destination: " + key
        );
    }

    std::vector<fs::path> scan(
        context& value, const std::map<std::string, reference_file>& files,
        const std::string& name, const fs::path& search
    ) {
        std::string executables;
        std::string libraries;
        for (const auto& [destination, file] : files) {
            if ((file.role == "data" && !file.runtime)
                || !release_detail::is_elf_file(file.source))
                continue;
            auto& group = fs::path(destination).parent_path() == "usr/bin"
                ? executables
                : libraries;
            group += "    " + release_detail::cmake_path_argument(file.source)
                + "\n";
        }
        if (executables.empty() && libraries.empty())
            return {};
        const auto cmake = find_command_path("cmake");
        if (cmake.empty() || find_command_path("objdump").empty())
            throw unavailable(
                "native runtime verification requires cmake and objdump"
            );
        const auto output = value.work / (name + ".txt");
        const auto script = value.work / (name + ".cmake");
        std::string error;
        const auto contents = render_text_template(
            "release/runtime_scan.cmake.tpl",
            { { "executables", executables },
              { "libraries", libraries },
              { "directories",
                search.empty() ? ""
                               : "    "
                        + release_detail::cmake_path_argument(search) + "\n" },
              { "resolved_path",
                release_detail::cmake_path_argument(output) } },
            &error
        );
        require(
            error.empty() && write_text_file(script, contents, &error), error
        );
        const string_list command { cmake, "-P", script.string() };
        const auto result = capture_command_result(command, value.root);
        value.commands.push_back(
            { { "command", command },
              { "exit_code", result.exit_code },
              { "output", result.output } }
        );
        require(
            result.exit_code == 0,
            "native runtime scan failed: " + result.output
        );
        const auto text = read_text_file(output, &error);
        require(error.empty(), error);
        std::istringstream input(text);
        std::vector<fs::path> dependencies;
        for (std::string line; std::getline(input, line);)
            if (!line.empty())
                dependencies.emplace_back(line);
        value.commands.back()["resolved_dependencies"] = json::array();
        for (const auto& dependency : dependencies)
            value.commands.back()["resolved_dependencies"].push_back(
                dependency.generic_string()
            );
        return dependencies;
    }

    std::map<std::string, reference_file>
    normalized_references(context& value) {
        auto files = value.expected;
        std::string elf_files;
        for (auto& [destination, file] : files) {
            if ((file.role == "data" && !file.runtime)
                || !release_detail::is_elf_file(file.source))
                continue;
            const auto copy = value.work / "comparison-only" / destination;
            fs::create_directories(copy.parent_path());
            fs::copy_file(file.source, copy);
            file.source = copy;
            elf_files
                += "    " + release_detail::cmake_path_argument(copy) + "\n";
        }
        if (!elf_files.empty()) {
            const auto script = value.work / "normalize-references.cmake";
            std::string error;
            const auto contents = render_text_template(
                "release/runtime_relocate.cmake.tpl",
                { { "files", elf_files } }, &error
            );
            require(
                error.empty() && write_text_file(script, contents, &error),
                error
            );
            const auto cmake = find_command_path("cmake");
            require(!cmake.empty(), "ELF reference comparison requires cmake");
            const string_list command { cmake, "-P", script.string() };
            const auto result = capture_command_result(command, value.root);
            value.commands.push_back(
                { { "command", command },
                  { "exit_code", result.exit_code },
                  { "output", result.output },
                  { "scope",
                    "private comparison copies only; prepared bytes are "
                    "read-only" } }
            );
            require(
                result.exit_code == 0,
                "reference normalization failed: " + result.output
            );
        }
        return files;
    }

} // namespace

void collect_inventory(context& value) {
    const auto primary = format_artifact_ref(value.selected);
    json declarations = json::array();
    for (const auto& ref :
         distribution_artifacts(value.project, value.selected)) {
        const auto item = resolve_artifact(value.project, ref);
        require(item.has_value(), "unresolved distribution member");
        const auto path = artifact_output_path(
            local_build_dir(value.root, "release"), *item->artifact_value,
            artifact_output_name(
                value.project, *item->component_value, *item->artifact_value
            )
        );
        require(
            path.has_value(),
            "missing required Release output: " + format_artifact_ref(ref)
        );
        require(
            path->filename() != "CMakeLists.txt"
                && path->extension() != ".cmake",
            "CMake surfaces cannot substitute for a product"
        );
        require(
            validate_project_paths(
                value.root, { path->lexically_relative(value.root) }
            )
                .empty(),
            "unsafe current build output"
        );
        const auto destination
            = fs::path(
                  release_detail::package_install_subdirectory(
                      *item->artifact_value
                  )
              )
            / path->filename();
        const auto identity = format_artifact_ref(ref);
        const auto role = identity == primary ? "primary" : "companion";
        declarations.push_back(
            { { "id", identity },
              { "kind", item->artifact_value->kind },
              { "role", role } }
        );
        add(value, destination, *path, identity, role);
        if (value.project.install_assets
            && (item->artifact_value->kind == "exe"
                || item->artifact_value->kind == "qt_app"))
            for (const auto& file : runtime_asset_files(value.root))
                add(value,
                    destination.parent_path() / "assets"
                        / file.lexically_relative(value.root / "assets"),
                    file, primary, "data");
    }
    const auto& declared = value.input.at("distribution").at("artifacts");
    require(
        declared.is_array() && declared.size() == declarations.size(),
        "incomplete artifact closure"
    );
    for (const auto& expected : declarations) {
        std::size_t count = 0;
        for (const auto& actual : declared)
            if (actual.is_object() && actual.size() == 3
                && actual.contains("id") && actual.contains("kind")
                && actual.contains("role")
                && actual.at("id") == expected.at("id")
                && actual.at("kind") == expected.at("kind")
                && actual.at("role") == expected.at("role"))
                ++count;
        require(
            count == 1,
            "artifact identity/kind/role differs from authoritative "
            "distribution"
        );
    }
    std::vector<fs::path> templates;
    std::string error;
    require(
        release_detail::collect_tooling_templates(
            value.root, value.project, value.selected, &templates, &error
        ),
        error
    );
    for (const auto& file : templates)
        add(value,
            fs::path("usr/share/ecosystem/templates")
                / file.lexically_relative(value.root / "assets/templates"),
            file, primary, "data");
    const auto references = value.expected;
    for (const auto& dependency :
         scan(value, references, "reference-runtime", {})) {
        const auto canonical = fs::canonical(dependency);
        if (!release_detail::path_within(
                canonical, fs::canonical(value.root)
            )) {
            require(
                release_detail::system_runtime_library(canonical),
                "unsupported external runtime reference: " + dependency.string()
            );
            continue;
        }
        auto source = dependency;
        std::set<fs::path> seen;
        while (seen.insert(source).second) {
            const auto destination = fs::path("usr/lib") / source.filename();
            const auto existing
                = value.expected.find(destination.generic_string());
            if (existing == value.expected.end())
                add(value, destination, source, primary, "data", true);
            else
                require(
                    fs::equivalent(existing->second.source, source),
                    "runtime destination collision"
                );
            if (!fs::is_symlink(source))
                break;
            const auto target = fs::read_symlink(source);
            source = (target.is_absolute() ? target
                                           : source.parent_path() / target)
                         .lexically_normal();
        }
    }
}

json check_inventory(context& value) {
    const auto& files = value.input.at("distribution").at("files");
    require(
        files.is_array() && files.size() == value.expected.size(),
        "incomplete or extra prepared file inventory"
    );
    std::set<std::string> seen;
    value.measurements = json::array();
    const auto references = normalized_references(value);
    std::map<std::string, reference_file> prepared;
    for (const auto& file : files) {
        require(
            file.is_object() && file.size() == 6,
            "prepared file requires exactly artifact, role, source, "
            "install_path, size and sha256"
        );
        const auto destination = release_detail::contract_path(
            file.at("install_path").get<std::string>()
        );
        require(
            seen.insert(destination).second, "duplicate prepared destination"
        );
        const auto expected = references.find(destination);
        require(
            expected != references.end(),
            "unexpected install destination: " + destination
        );
        require(
            file.at("artifact") == expected->second.artifact
                && file.at("role") == expected->second.role,
            "inconsistent file ownership/role"
        );
        const auto relative = release_detail::contract_path(
            file.at("source").get<std::string>()
        );
        const auto path = value.root / relative;
        require(
            path == value.payload / destination,
            "prepared file is outside its exact payload destination"
        );
        regular(path);
        require(file.at("size").is_number_unsigned(), "invalid declared size");
        const auto hash = digest(path);
        const auto reference = digest(expected->second.source);
        const auto permissions
            = fs::status(path).permissions() & fs::perms::all;
        const auto reference_permissions
            = fs::status(expected->second.source).permissions()
            & fs::perms::all;
        value.measurements.push_back(
            { { "artifact", expected->second.artifact },
              { "role", expected->second.role },
              { "source", relative },
              { "install_path", destination },
              { "size", fs::file_size(path) },
              { "sha256", hash },
              { "reference_sha256", reference },
              { "permissions", static_cast<unsigned int>(permissions) },
              { "reference_permissions",
                static_cast<unsigned int>(reference_permissions) },
              { "reference_source",
                value.expected.at(destination)
                    .source.lexically_relative(value.root)
                    .generic_string() },
              { "comparison",
                expected->second.source == value.expected.at(destination).source
                    ? "exact current reference"
                    : "current ELF reference with canonical RPATH removal on a "
                      "private copy" },
              { "declared_size", file.at("size") },
              { "declared_sha256", file.at("sha256") } }
        );
        require(
            fs::file_size(path) == file.at("size").get<std::uintmax_t>()
                && hash == file.at("sha256").get<std::string>(),
            "prepared size/digest mismatch: " + destination
        );
        require(
            hash == reference,
            "prepared bytes differ from current authoritative "
            "artifact/resource: "
                + destination
        );
        require(
            permissions == reference_permissions,
            "prepared permissions differ from current reference: " + destination
        );
        prepared.emplace(
            destination,
            reference_file { path, expected->second.artifact,
                             expected->second.role, expected->second.runtime }
        );
    }
    std::set<std::string> actual;
    for (const auto& entry : fs::recursive_directory_iterator(value.payload)) {
        require(
            !entry.is_symlink()
                && (entry.is_directory() || entry.is_regular_file()),
            "unsafe payload entry"
        );
        if (entry.is_regular_file())
            actual.insert(
                entry.path().lexically_relative(value.payload).generic_string()
            );
    }
    require(actual == seen, "payload has missing or unlisted files");
    for (const auto& dependency :
         scan(value, prepared, "prepared-runtime", value.payload / "usr/lib")) {
        const auto canonical = fs::canonical(dependency);
        require(
            release_detail::path_within(canonical, fs::canonical(value.payload))
                || release_detail::system_runtime_library(canonical),
            "prepared runtime resolves to a private non-payload dependency: "
                + dependency.string()
        );
    }
    return value.measurements;
}

} // namespace ecosystem::prepared_detail
