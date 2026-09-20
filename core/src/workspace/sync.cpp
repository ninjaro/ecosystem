#include "workspace/sync.hpp"

#include "packages/package_summary.hpp"
#include "packages/package_surface.hpp"
#include "workspace/project.hpp"
#include "workspace/source_dependencies.hpp"
#include "workspace/template_text.hpp"
#include "workspace/tooling.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace ecosystem {
namespace sync_support {

    std::string to_upper_identifier(std::string value) {
        std::replace(value.begin(), value.end(), '-', '_');
        std::replace(value.begin(), value.end(), '/', '_');
        for (char& character : value) {
            character = static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))
            );
        }
        return value;
    }

    std::string
    relative_path_string(const fs::path& project_root, const fs::path& path) {
        const fs::path relative = path.lexically_relative(project_root);
        if (!relative.empty()) {
            return relative.generic_string();
        }
        return path.generic_string();
    }

    void append_unique_paths(
        std::vector<fs::path>* destination, const std::vector<fs::path>& source
    ) {
        std::set<std::string> seen;
        for (const fs::path& path : *destination) {
            seen.insert(path.lexically_normal().generic_string());
        }
        for (const fs::path& path : source) {
            if (seen.insert(path.lexically_normal().generic_string()).second) {
                destination->push_back(path);
            }
        }
    }

    void emit_line(
        std::ostringstream& stream, const std::string& indent,
        const std::string& line
    ) {
        stream << indent << line << "\n";
    }

    void append_paths(
        std::ostringstream& stream, const std::string& variable_name,
        const std::vector<fs::path>& paths, const fs::path& project_root,
        const std::string& source_root_expression
    ) {
        std::ostringstream path_block;
        for (const fs::path& path : paths) {
            path_block << "        " << source_root_expression << "/"
                       << relative_path_string(project_root, path) << "\n";
        }

        std::string error_message;
        const std::string block = render_text_template(
            "cmake/path_set_block.tpl",
            {
                { "variable_name", variable_name },
                { "paths_block", path_block.str() },
            },
            &error_message
        );
        if (!error_message.empty()) {
            stream << "# " << error_message << "\n\n";
            return;
        }
        stream << block;
    }

    bool json_flag_enabled(const json& object, const std::string& key) {
        return object.contains(key) && object.at(key).is_boolean()
            && object.at(key).get<bool>();
    }

    bool is_library_kind(const std::string& kind) {
        return kind == "static_lib" || kind == "shared_lib"
            || kind == "interface_lib";
    }

    bool is_runnable_kind(const std::string& kind) {
        return kind == "exe" || kind == "qt_app";
    }

    bool artifact_is_runnable(const artifact& artifact_value) {
        return is_runnable_kind(artifact_value.kind);
    }

    bool
    install_surface_is_library_first(const artifact* entry_artifact_value) {
        return entry_artifact_value != nullptr
            && is_library_kind(entry_artifact_value->kind);
    }

    bool artifact_participates_in_install_surface(
        const artifact& artifact_value, const bool is_entry_artifact,
        const bool library_first_surface
    ) {
        if (is_entry_artifact) {
            return true;
        }
        if (artifact_value.kind == "shared_lib") {
            return true;
        }
        return library_first_surface && is_library_kind(artifact_value.kind);
    }

    bool artifact_installs_target_file(
        const artifact& artifact_value, const bool is_entry_artifact,
        const bool library_first_surface
    ) {
        return artifact_participates_in_install_surface(
                   artifact_value, is_entry_artifact, library_first_surface
               )
            && artifact_value.kind != "interface_lib";
    }

    bool artifact_installs_public_headers(
        const artifact& artifact_value, const bool is_entry_artifact,
        const bool library_first_surface
    ) {
        return artifact_participates_in_install_surface(
                   artifact_value, is_entry_artifact, library_first_surface
               )
            && is_library_kind(artifact_value.kind);
    }

    std::vector<fs::path> component_core_sources(
        const fs::path& project_root, const component& component_value
    ) {
        std::vector<fs::path> files
            = component_module_sources(project_root, component_value);
        append_unique_paths(
            &files,
            component_c_header_pair_sources(project_root, component_value)
        );
        append_unique_paths(
            &files,
            component_non_runtime_source_only_files(
                project_root, component_value
            )
        );
        return files;
    }

    std::vector<fs::path> component_runtime_sources(
        const fs::path& project_root, const component& component_value
    ) {
        std::vector<fs::path> files
            = component_core_sources(project_root, component_value);
        append_unique_paths(
            &files, component_runtime_only_files(project_root, component_value)
        );
        return files;
    }

    std::vector<fs::path> component_public_headers(
        const fs::path& project_root, const component& component_value
    ) {
        std::vector<fs::path> files
            = component_module_headers(project_root, component_value);
        append_unique_paths(
            &files, component_header_only_files(project_root, component_value)
        );
        append_unique_paths(
            &files, component_c_header_only_files(project_root, component_value)
        );
        append_unique_paths(
            &files, component_template_impl_files(project_root, component_value)
        );
        append_unique_paths(
            &files,
            component_c_header_pair_headers(project_root, component_value)
        );
        return files;
    }

    std::vector<fs::path> component_qml_files(
        const fs::path& project_root, const component& component_value
    ) {
        std::vector<fs::path> files;
        if (!component_value.ownership
            || !component_value.ownership->qml.has_value()) {
            return files;
        }
        for (const std::string& file : component_value.ownership->qml->files) {
            files.push_back(
                (project_root / component_value.root / file).lexically_normal()
            );
        }
        return files;
    }

    std::string link_scope_for_artifact(const artifact& artifact_value) {
        if (artifact_value.kind == "interface_lib") {
            return "INTERFACE";
        }
        if (is_library_kind(artifact_value.kind)) {
            return "PUBLIC";
        }
        return "PRIVATE";
    }

    std::vector<std::string> component_link_targets(
        const component& component_value, const artifact& artifact_value
    ) {
        std::vector<std::string> targets
            = component_package_link_targets(component_value);
        std::set<std::string> seen;
        for (const std::string& target : targets) {
            seen.insert(target);
        }

        for (const std::string& link : artifact_value.link) {
            const std::optional<artifact_ref> ref = parse_artifact_ref(link);
            const std::string target
                = ref.has_value() ? cmake_target_name(*ref) : link;
            if (!target.empty() && seen.insert(target).second) {
                targets.push_back(target);
            }
        }

        return targets;
    }

    bool project_has_assets_dir(const fs::path& project_root) {
        std::error_code error;
        return fs::exists(project_root / "assets", error) && !error;
    }

    struct github_actions_vars {
        std::string manifesto_bootstrap = "repository";
        std::string manifesto_repository;
        std::string manifesto_ref;
        std::string manifesto_source_path = ".";
        std::string manifesto_setup_action
            = "./.github/actions/setup-manifesto";
        std::string manifesto_build_parallelism = "2";
        std::string sphinx_theme = "sphinx_rtd_theme";
        std::string sphinx_theme_package = "sphinx-rtd-theme";
        std::string checkout_action = "actions/checkout@v4";
        std::string install_qt_action = "jurplel/install-qt-action@v4";
        std::string github_script_action = "actions/github-script@v7";
        std::string codeql_action_ref = "v3";
        std::string upload_artifact_action = "actions/upload-artifact@v4";
        std::string download_artifact_action = "actions/download-artifact@v4";
        std::string configure_pages_action = "actions/configure-pages@v5";
        std::string upload_pages_artifact_action
            = "actions/upload-pages-artifact@v3";
        std::string deploy_pages_action = "actions/deploy-pages@v4";
    };

    fs::path github_actions_vars_path(const fs::path& project_root) {
        return project_root / "manifesto.github.vars.json";
    }

    void assign_string_field(
        const json& object, const std::string& key, std::string* value,
        string_list* errors
    ) {
        if (!object.contains(key)) {
            return;
        }
        if (!object.at(key).is_string()) {
            errors->push_back(
                key + " must be a string in manifesto.github.vars.json"
            );
            return;
        }
        *value = object.at(key).get<std::string>();
    }

    void require_non_empty_field(
        const std::string& key, const std::string& value, string_list* errors
    ) {
        if (value.empty()) {
            errors->push_back(
                key + " must not be empty in manifesto.github.vars.json"
            );
        }
    }

    bool
    github_actions_use_local_setup_action(const github_actions_vars& vars) {
        return vars.manifesto_setup_action
            == "./.github/actions/setup-manifesto";
    }

    std::vector<fs::path> tracked_surface_owned_paths() {
        return {
            "CMakeLists.txt",
            ".gitignore",
            ".clang-format",
            ".clang-tidy",
            ".github/SECURITY.md",
            ".github/renovate.json",
            ".github/actions/run-manifesto-stage/action.yml",
            ".github/actions/publish-manifesto-report/action.yml",
            ".github/actions/setup-manifesto/action.yml",
            ".github/actions/setup-ecosystem/action.yml",
            ".github/actions/run-ecosystem-stage/action.yml",
            ".github/actions/publish-ecosystem-report/action.yml",
            ".github/workflows/tests.yml",
            ".github/workflows/codeql.yml",
            ".github/workflows/html.yml",
        };
    }

    std::set<std::string> generated_tracked_surface_keys(
        const std::vector<tracked_surface_file>& files
    ) {
        std::set<std::string> keys;
        for (const tracked_surface_file& file_value : files) {
            keys.insert(
                file_value.relative_path.lexically_normal().generic_string()
            );
        }
        return keys;
    }

    bool sync_owned_path_is_generated(
        const fs::path& relative_path,
        const std::set<std::string>& generated_keys
    ) {
        return generated_keys.contains(
            relative_path.lexically_normal().generic_string()
        );
    }

    bool path_exists(const fs::path& path) {
        std::error_code error;
        return fs::exists(path, error) && !error;
    }

    void remove_empty_parent_dirs(
        const fs::path& path, const fs::path& project_root
    ) {
        fs::path current = path.parent_path();
        while (!current.empty() && current != project_root
               && current != current.root_path()) {
            std::error_code error;
            if (!fs::is_directory(current, error) || error
                || !fs::is_empty(current, error) || error) {
                break;
            }
            fs::remove(current, error);
            if (error) {
                break;
            }
            current = current.parent_path();
        }
    }

    void append_obsolete_tracked_surface_drift(
        string_list* drift, const fs::path& project_root,
        const std::vector<tracked_surface_file>& files
    ) {
        const std::set<std::string> generated_keys
            = generated_tracked_surface_keys(files);
        for (const fs::path& relative_path : tracked_surface_owned_paths()) {
            if (sync_owned_path_is_generated(relative_path, generated_keys)) {
                continue;
            }
            const fs::path path = project_root / relative_path;
            if (!path_exists(path)) {
                continue;
            }
            drift->push_back(
                relative_path.generic_string()
                + " is obsolete for current ecosystem defaults; run `marx sync`"
            );
        }
    }

    void remove_obsolete_tracked_surface_files(
        sync_report* report, const fs::path& project_root,
        const std::vector<tracked_surface_file>& files
    ) {
        const std::set<std::string> generated_keys
            = generated_tracked_surface_keys(files);
        for (const fs::path& relative_path : tracked_surface_owned_paths()) {
            if (sync_owned_path_is_generated(relative_path, generated_keys)) {
                continue;
            }
            const fs::path path = project_root / relative_path;
            if (!path_exists(path)) {
                continue;
            }

            std::error_code error;
            if (!fs::remove(path, error)) {
                if (error) {
                    report->errors.push_back(
                        "unable to remove obsolete tracked surface "
                        + path.generic_string() + ": " + error.message()
                    );
                }
                continue;
            }
            report->removed_files.push_back(path);
            remove_empty_parent_dirs(path, project_root);
        }
    }

    std::optional<github_actions_vars> load_github_actions_vars(
        const fs::path& project_root, string_list* errors
    ) {
        github_actions_vars vars;
        std::error_code fs_error;
        const fs::path path = github_actions_vars_path(project_root);
        if (!fs::exists(path, fs_error) || fs_error) {
            return vars;
        }

        std::string read_error;
        const std::string contents = read_text_file(path, &read_error);
        if (!read_error.empty()) {
            errors->push_back(read_error);
            return std::nullopt;
        }

        json root;
        try {
            root = json::parse(contents);
        } catch (const std::exception& error) {
            errors->push_back(
                "invalid manifesto.github.vars.json: "
                + std::string(error.what())
            );
            return std::nullopt;
        }
        if (!root.is_object()) {
            errors->push_back(
                "manifesto.github.vars.json must contain a JSON object"
            );
            return std::nullopt;
        }

        assign_string_field(
            root, "manifesto_bootstrap", &vars.manifesto_bootstrap, errors
        );
        assign_string_field(
            root, "manifesto_repository", &vars.manifesto_repository, errors
        );
        assign_string_field(
            root, "manifesto_source_path", &vars.manifesto_source_path, errors
        );
        assign_string_field(root, "manifesto_ref", &vars.manifesto_ref, errors);
        assign_string_field(
            root, "manifesto_setup_action", &vars.manifesto_setup_action, errors
        );
        assign_string_field(
            root, "manifesto_build_parallelism",
            &vars.manifesto_build_parallelism, errors
        );
        assign_string_field(root, "sphinx_theme", &vars.sphinx_theme, errors);
        assign_string_field(
            root, "sphinx_theme_package", &vars.sphinx_theme_package, errors
        );
        assign_string_field(
            root, "checkout_action", &vars.checkout_action, errors
        );
        assign_string_field(
            root, "install_qt_action", &vars.install_qt_action, errors
        );
        assign_string_field(
            root, "github_script_action", &vars.github_script_action, errors
        );
        assign_string_field(
            root, "codeql_action_ref", &vars.codeql_action_ref, errors
        );
        assign_string_field(
            root, "upload_artifact_action", &vars.upload_artifact_action, errors
        );
        assign_string_field(
            root, "download_artifact_action", &vars.download_artifact_action,
            errors
        );
        assign_string_field(
            root, "configure_pages_action", &vars.configure_pages_action, errors
        );
        assign_string_field(
            root, "upload_pages_artifact_action",
            &vars.upload_pages_artifact_action, errors
        );
        assign_string_field(
            root, "deploy_pages_action", &vars.deploy_pages_action, errors
        );
        if (vars.manifesto_bootstrap != "checkout"
            && vars.manifesto_bootstrap != "repository") {
            errors->push_back(
                "manifesto_bootstrap must be checkout or repository in "
                "manifesto.github.vars.json"
            );
        }
        if (vars.manifesto_repository.empty() != vars.manifesto_ref.empty()) {
            errors->push_back(
                "manifesto_repository and manifesto_ref must be configured "
                "together in manifesto.github.vars.json"
            );
        }
        if (vars.manifesto_bootstrap == "checkout"
            && (!vars.manifesto_repository.empty()
                || !vars.manifesto_ref.empty())) {
            errors->push_back(
                "checkout bootstrap uses the revision under review; omit "
                "manifesto_repository and manifesto_ref"
            );
        }
        const fs::path source_path(vars.manifesto_source_path);
        if (source_path.empty() || source_path.is_absolute()
            || std::find(source_path.begin(), source_path.end(), fs::path(".."))
                != source_path.end()) {
            errors->push_back(
                "manifesto_source_path must stay within the checkout in "
                "manifesto.github.vars.json"
            );
        }
        for (const auto* field :
             { &vars.manifesto_repository, &vars.manifesto_ref,
               &vars.manifesto_source_path }) {
            if (std::any_of(
                    field->begin(), field->end(),
                    [](const unsigned char character) {
                        return std::iscntrl(character);
                    }
                )
                || field->find("${{") != std::string::npos) {
                errors->push_back(
                    "bootstrap repository, ref and source path must be literal "
                    "single-line values in manifesto.github.vars.json"
                );
            }
        }
        if (vars.manifesto_build_parallelism.empty()
            || vars.manifesto_build_parallelism.front() == '0'
            || !std::all_of(
                vars.manifesto_build_parallelism.begin(),
                vars.manifesto_build_parallelism.end(),
                [](const unsigned char character) {
                    return std::isdigit(character);
                }
            )) {
            errors->push_back(
                "manifesto_build_parallelism must be a positive integer string "
                "in manifesto.github.vars.json"
            );
        }
        require_non_empty_field(
            "manifesto_setup_action", vars.manifesto_setup_action, errors
        );
        require_non_empty_field(
            "manifesto_build_parallelism", vars.manifesto_build_parallelism,
            errors
        );
        require_non_empty_field("sphinx_theme", vars.sphinx_theme, errors);
        require_non_empty_field(
            "sphinx_theme_package", vars.sphinx_theme_package, errors
        );
        require_non_empty_field(
            "checkout_action", vars.checkout_action, errors
        );
        require_non_empty_field(
            "install_qt_action", vars.install_qt_action, errors
        );
        require_non_empty_field(
            "github_script_action", vars.github_script_action, errors
        );
        require_non_empty_field(
            "codeql_action_ref", vars.codeql_action_ref, errors
        );
        require_non_empty_field(
            "upload_artifact_action", vars.upload_artifact_action, errors
        );
        require_non_empty_field(
            "download_artifact_action", vars.download_artifact_action, errors
        );
        require_non_empty_field(
            "configure_pages_action", vars.configure_pages_action, errors
        );
        require_non_empty_field(
            "upload_pages_artifact_action", vars.upload_pages_artifact_action,
            errors
        );
        require_non_empty_field(
            "deploy_pages_action", vars.deploy_pages_action, errors
        );
        if (!errors->empty()) {
            return std::nullopt;
        }
        return vars;
    }

    std::optional<std::string> render_sync_template_candidates(
        const std::vector<fs::path>& relative_paths,
        const template_bindings& bindings, string_list* errors
    ) {
        std::string error_message;
        const std::string contents = render_text_template_candidates(
            relative_paths, bindings, &error_message
        );
        if (!error_message.empty()) {
            errors->push_back(error_message);
            return std::nullopt;
        }
        return contents;
    }

    std::string render_required_sync_template(
        const fs::path& relative_path, const template_bindings& bindings
    ) {
        return render_required_text_template(relative_path, bindings);
    }

    std::string
    newline_terminated_lines(const std::vector<std::string>& lines) {
        std::ostringstream stream;
        for (const std::string& line : lines) {
            stream << line << "\n";
        }
        return stream.str();
    }

    std::string
    indent_block(const std::string& block, const std::string& indent) {
        if (block.empty() || indent.empty()) {
            return block;
        }

        std::istringstream input(block);
        std::ostringstream output;
        std::string line;
        bool first_line = true;
        while (std::getline(input, line)) {
            if (!first_line) {
                output << "\n";
            }
            output << indent << line;
            first_line = false;
        }
        return output.str();
    }

    std::string indented_lines_block(
        const std::vector<std::string>& lines, const std::string& indent
    ) {
        std::vector<std::string> indented_lines;
        indented_lines.reserve(lines.size());
        for (const std::string& line : lines) {
            indented_lines.push_back(indent + line);
        }
        return newline_terminated_lines(indented_lines);
    }

    std::string render_indented_sync_template(
        const fs::path& relative_path, const template_bindings& bindings,
        const std::string& indent
    ) {
        return indent_block(
            render_required_sync_template(relative_path, bindings), indent
        );
    }

    std::string render_interface_artifact_block(
        const std::string& target_name, const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/artifact/interface_lib.tpl",
            { { "target_name", target_name } }, indent
        );
    }

    std::string render_library_artifact_block(
        const std::string& target_name, const std::string& library_kind,
        const std::vector<std::string>& header_lines,
        const std::vector<std::string>& source_lines, const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/artifact/library.tpl",
            {
                { "target_name", target_name },
                { "library_kind", library_kind },
                { "headers_block", indented_lines_block(header_lines, "    ") },
                { "sources_block", indented_lines_block(source_lines, "    ") },
            },
            indent
        );
    }

    std::string render_runnable_artifact_block(
        const fs::path& template_path, const std::string& target_name,
        const std::vector<std::string>& header_lines,
        const std::vector<std::string>& source_lines, const std::string& indent
    ) {
        return render_indented_sync_template(
            template_path,
            {
                { "target_name", target_name },
                { "headers_block", indented_lines_block(header_lines, "    ") },
                { "sources_block", indented_lines_block(source_lines, "    ") },
            },
            indent
        );
    }

    std::string render_executable_artifact_block(
        const std::string& target_name,
        const std::vector<std::string>& header_lines,
        const std::vector<std::string>& source_lines, const std::string& indent
    ) {
        return render_runnable_artifact_block(
            "cmake/artifact/executable.tpl", target_name, header_lines,
            source_lines, indent
        );
    }

    std::string render_qt_app_artifact_block(
        const std::string& target_name,
        const std::vector<std::string>& header_lines,
        const std::vector<std::string>& source_lines, const std::string& indent
    ) {
        return render_runnable_artifact_block(
            "cmake/artifact/qt_app.tpl", target_name, header_lines,
            source_lines, indent
        );
    }

    std::string source_path_expression(
        const std::string& source_root_expression, const fs::path& project_root,
        const fs::path& path
    );

    std::string render_qml_module_block(
        const std::string& target_name, const qml_module& module,
        const std::vector<fs::path>& files, const fs::path& project_root,
        const std::string& source_root_expression, const std::string& indent
    ) {
        std::vector<std::string> qml_file_lines;
        std::vector<std::string> resource_alias_lines;
        auto uri_path = module.uri;
        std::replace(uri_path.begin(), uri_path.end(), '.', '/');
        for (std::size_t index = 0; index < files.size(); ++index) {
            const fs::path& file = files.at(index);
            qml_file_lines.push_back(source_path_expression(
                source_root_expression, project_root, file
            ));
            const fs::path alias = fs::path(module.files.at(index))
                                       .lexically_relative(fs::path("qml"));
            resource_alias_lines.push_back(
                "set_source_files_properties(" + qml_file_lines.back()
                + " PROPERTIES QT_RESOURCE_ALIAS " + alias.generic_string()
                + ")"
            );
        }
        return render_indented_sync_template(
            "cmake/artifact/qt_qml_module.tpl",
            {
                { "target_name", target_name },
                { "qml_uri", module.uri },
                { "qml_uri_path", uri_path },
                { "qml_version", module.version },
                { "resource_alias_block",
                  newline_terminated_lines(resource_alias_lines) },
                { "qml_files_block",
                  indented_lines_block(qml_file_lines, "    ") },
            },
            indent
        );
    }

    std::string render_include_dirs_block(
        const std::string& target_name, const std::string& visibility,
        const std::vector<std::string>& include_dir_lines,
        const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/artifact/include_dirs.tpl",
            {
                { "target_name", target_name },
                { "visibility", visibility },
                { "include_dirs_block",
                  indented_lines_block(include_dir_lines, "    ") },
            },
            indent
        );
    }

    std::string render_link_libraries_block(
        const std::string& target_name, const std::string& link_scope,
        const std::vector<std::string>& link_target_lines,
        const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/artifact/link_libraries.tpl",
            {
                { "target_name", target_name },
                { "link_scope", link_scope },
                { "link_targets_block",
                  indented_lines_block(link_target_lines, "    ") },
            },
            indent
        );
    }

    std::string render_qt_android_package_block(
        const std::string& target_name, const std::string& android_package_name,
        const std::string& android_package_source_dir,
        const std::string& source_root_expression, const std::string& indent
    ) {
        const std::string android_package_source_block
            = android_package_source_dir.empty() ? std::string()
                                                 : "    set_property(TARGET "
                + target_name + " PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR \""
                + source_root_expression + "/" + android_package_source_dir
                + "\")";
        return render_indented_sync_template(
            "cmake/artifact/qt_android_package.tpl",
            {
                { "target_name", target_name },
                { "android_package_name", android_package_name },
                { "android_package_source_block",
                  android_package_source_block },
            },
            indent
        );
    }

    std::string render_tests_guard_open_block(const std::string& indent) {
        return render_indented_sync_template(
            "cmake/component/tests_guard_open.tpl", {}, indent
        );
    }

    std::string render_gtest_guard_open_block(const std::string& indent) {
        return render_indented_sync_template(
            "cmake/component/gtest_guard_open.tpl", {}, indent
        );
    }

    std::string render_benchmarks_guard_open_block(const std::string& indent) {
        return render_indented_sync_template(
            "cmake/component/benchmarks_guard_open.tpl", {}, indent
        );
    }

    std::string
    render_benchmark_package_guard_open_block(const std::string& indent) {
        return render_indented_sync_template(
            "cmake/component/benchmark_package_guard_open.tpl", {}, indent
        );
    }

    std::string render_gtest_guard_else_block(
        const std::string& component_id, const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/component/gtest_guard_else.tpl",
            { { "component_id", component_id } }, indent
        );
    }

    std::string render_benchmark_package_guard_else_block(
        const std::string& component_id, const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/component/benchmark_package_guard_else.tpl",
            { { "component_id", component_id } }, indent
        );
    }

    std::string render_opencv_compile_definitions_block(
        const std::string& target_name, const std::string& link_scope,
        const std::string& project_prefix, const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/artifact/opencv_compile_definitions.tpl",
            {
                { "target_name", target_name },
                { "link_scope", link_scope },
                { "project_prefix", project_prefix },
            },
            indent
        );
    }

    std::string render_kde_compile_definitions_line(
        const std::string& target_name, const std::string& link_scope,
        const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/artifact/kde_compile_definitions.tpl",
            {
                { "target_name", target_name },
                { "link_scope", link_scope },
            },
            indent
        );
    }

    std::string render_output_name_line(
        const std::string& target_name, const std::string& output_name,
        const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/artifact/output_name.tpl",
            {
                { "target_name", target_name },
                { "output_name", output_name },
            },
            indent
        );
    }

    std::string render_apply_defaults_line(
        const std::string& target_name, const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/artifact/apply_defaults_line.tpl",
            { { "target_name", target_name } }, indent
        );
    }

    std::string render_project_version_definition(
        const std::string& target_name, const std::string& link_scope,
        const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/artifact/project_version_definition.tpl",
            {
                { "target_name", target_name },
                { "link_scope", link_scope },
            },
            indent
        );
    }

    std::string render_qt_finalize_line(
        const std::string& target_name, const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/artifact/qt_finalize.tpl",
            { { "target_name", target_name } }, indent
        );
    }

    std::string render_stage_assets_line(
        const std::string& target_name, const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/artifact/stage_assets.tpl",
            { { "target_name", target_name } }, indent
        );
    }

    std::string render_embed_assets_line(
        const std::string& target_name, const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/artifact/embed_assets.tpl",
            { { "target_name", target_name } }, indent
        );
    }

    std::string render_add_test_line(
        const std::string& test_name, const std::string& target_name,
        const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/artifact/add_test.tpl",
            {
                { "test_name", test_name },
                { "target_name", target_name },
            },
            indent
        );
    }

    std::string render_qttest_offscreen_line(
        const std::string& test_name, const std::string& indent
    ) {
        return render_indented_sync_template(
            "cmake/artifact/qttest_offscreen.tpl",
            { { "test_name", test_name } }, indent
        );
    }

    std::string render_install_target_block(const std::string& target_name) {
        return render_required_sync_template(
            "cmake/install_target.tpl", { { "target_name", target_name } }
        );
    }

    std::string render_install_include_directory_block(
        const std::string& include_dir_expression
    ) {
        return render_required_sync_template(
            "cmake/install_include_dir.tpl",
            { { "include_dir", include_dir_expression } }
        );
    }

    std::string render_generated_tests_link_block(
        const std::string& test_target,
        const std::vector<std::string>& link_targets
    ) {
        std::vector<std::string> indented_link_targets;
        for (const std::string& link_target : link_targets) {
            indented_link_targets.push_back("                " + link_target);
        }
        return render_required_sync_template(
            "cmake/generated_tests_link_block.tpl",
            {
                { "test_target", test_target },
                { "link_targets_block",
                  newline_terminated_lines(indented_link_targets) },
            }
        );
    }

    std::string
    render_generated_tests_stage_assets_block(const std::string& test_target) {
        return render_required_sync_template(
            "cmake/generated_tests_stage_assets.tpl",
            { { "test_target", test_target } }
        );
    }

    std::string render_generated_tests_dependencies_block(
        const std::string& test_target,
        const std::vector<std::string>& dependency_targets
    ) {
        std::vector<std::string> indented_dependency_targets;
        for (const std::string& dependency_target : dependency_targets) {
            indented_dependency_targets.push_back(
                "                " + dependency_target
            );
        }
        return render_required_sync_template(
            "cmake/generated_tests_dependencies.tpl",
            {
                { "test_target", test_target },
                { "dependency_targets_block",
                  newline_terminated_lines(indented_dependency_targets) },
            }
        );
    }

    std::string render_generated_tests_gtest_open_block() {
        return render_required_sync_template(
            "cmake/generated_tests_gtest_open.tpl", {}
        );
    }

    std::string
    render_generated_tests_gtest_else_block(const std::string& component_id) {
        return render_required_sync_template(
            "cmake/generated_tests_gtest_else.tpl",
            { { "component_id", component_id } }
        );
    }

    std::string render_generated_tests_block(
        const std::string& test_var, const std::string& tests_glob,
        const std::string& test_target, const std::string& headers_var,
        const std::string& sources_var, const std::string& test_var_expression,
        const std::vector<std::string>& include_dir_expressions,
        const std::vector<std::string>& link_targets,
        const std::string& source_root_expression,
        const std::string& runtime_definition_block,
        const std::vector<std::string>& runtime_dependency_targets,
        const bool stage_assets, const bool gtest_enabled,
        const std::string& component_id,
        const std::optional<std::string>& owned_inputs = std::nullopt
    ) {
        std::vector<std::string> indented_include_dirs;
        for (const std::string& include_dir_expression :
             include_dir_expressions) {
            indented_include_dirs.push_back(
                "                " + include_dir_expression
            );
        }
        const std::string link_block = link_targets.empty()
            ? std::string()
            : render_generated_tests_link_block(test_target, link_targets);
        const std::string stage_assets_block = stage_assets
            ? render_generated_tests_stage_assets_block(test_target) + "\n"
            : std::string();
        const std::string runtime_dependencies_block
            = runtime_dependency_targets.empty()
            ? std::string()
            : render_generated_tests_dependencies_block(
                  test_target, runtime_dependency_targets
              ) + "\n";
        const std::string gtest_guard_open = gtest_enabled
            ? render_generated_tests_gtest_open_block() + "\n"
            : std::string();
        const std::string gtest_guard_close = gtest_enabled
            ? render_generated_tests_gtest_else_block(component_id) + "\n"
            : std::string();

        return render_required_sync_template(
            "cmake/generated_tests_block.tpl",
            {
                { "gtest_guard_open", gtest_guard_open },
                { "test_var", test_var },
                { "test_source_block",
                  owned_inputs
                      ? "set(" + test_var + "\n" + *owned_inputs + "    )"
                      : "file(GLOB_RECURSE " + test_var
                          + " CONFIGURE_DEPENDS\n            " + tests_glob
                          + "\n    )" },
                { "test_target", test_target },
                { "headers_var", headers_var },
                { "sources_var", sources_var },
                { "test_var_expression", test_var_expression },
                { "include_dirs_block",
                  newline_terminated_lines(indented_include_dirs) },
                { "link_block", link_block },
                { "source_root_expression", source_root_expression },
                { "runtime_definition_block", runtime_definition_block },
                { "runtime_dependencies_block", runtime_dependencies_block },
                { "stage_assets_block", stage_assets_block },
                { "component_id", component_id },
                { "test_name",
                  owned_inputs ? test_target : component_id + "_tests" },
                { "gtest_guard_close", gtest_guard_close },
            }
        );
    }

    void append_tracked_template_file(
        std::vector<tracked_surface_file>* files, const fs::path& output_path,
        const std::vector<fs::path>& template_paths,
        const template_bindings& bindings, string_list* errors
    ) {
        const std::optional<std::string> contents
            = render_sync_template_candidates(template_paths, bindings, errors);
        if (!contents.has_value()) {
            return;
        }
        files->push_back(tracked_surface_file { output_path, *contents });
    }

    template_bindings github_actions_bindings(
        const manifest& value, const github_actions_vars& vars
    ) {
        return {
            { "project_id", value.id },
            { "manifesto_bootstrap", json(vars.manifesto_bootstrap).dump() },
            { "manifesto_repository", json(vars.manifesto_repository).dump() },
            { "manifesto_ref", json(vars.manifesto_ref).dump() },
            { "manifesto_source_path",
              json(vars.manifesto_source_path).dump() },
            { "manifesto_setup_action", vars.manifesto_setup_action },
            { "manifesto_build_parallelism", vars.manifesto_build_parallelism },
            { "sphinx_theme", vars.sphinx_theme },
            { "sphinx_theme_package", vars.sphinx_theme_package },
            { "checkout_action", vars.checkout_action },
            { "install_qt_action", vars.install_qt_action },
            { "github_script_action", vars.github_script_action },
            { "codeql_action_ref", vars.codeql_action_ref },
            { "upload_artifact_action", vars.upload_artifact_action },
            { "download_artifact_action", vars.download_artifact_action },
            { "configure_pages_action", vars.configure_pages_action },
            { "upload_pages_artifact_action",
              vars.upload_pages_artifact_action },
            { "deploy_pages_action", vars.deploy_pages_action },
            { "github_matrix_language", "${{ matrix.language }}" },
            { "github_coverage_enabled",
              "${{ steps.coverage.outputs.status == 'passed' }}" },
            { "github_page_url", "${{ steps.deployment.outputs.page_url }}" },
        };
    }

    std::string source_path_expression(
        const std::string& source_root_expression, const fs::path& project_root,
        const fs::path& path
    ) {
        return source_root_expression + "/"
            + relative_path_string(project_root, path);
    }

    std::vector<std::string> runnable_source_lines(
        const fs::path& project_root, const component& component_value,
        const std::string& prefix, const std::string& source_root_expression,
        const bool reuse_component_library
    ) {
        if (!reuse_component_library) {
            return { "${" + prefix + "_RUNTIME_SOURCES}" };
        }

        std::vector<std::string> lines;
        for (const fs::path& source_file :
             component_runtime_only_files(project_root, component_value)) {
            lines.push_back(source_path_expression(
                source_root_expression, project_root, source_file
            ));
        }
        return lines;
    }

    std::optional<artifact_ref>
    first_library_artifact_ref(const component& component_value) {
        for (const artifact& artifact_value : component_value.artifacts) {
            if (is_library_kind(artifact_value.kind)) {
                return artifact_ref { component_value.id, artifact_value.id };
            }
        }
        return std::nullopt;
    }

    bool same_artifact_ref(const artifact_ref& lhs, const artifact_ref& rhs) {
        return lhs.component_id == rhs.component_id
            && lhs.artifact_id == rhs.artifact_id;
    }

    void collect_surface_artifacts(
        const manifest& value, const artifact_ref& ref,
        std::vector<artifact_ref>* artifacts, std::set<std::string>* seen
    ) {
        const std::string key = format_artifact_ref(ref);
        if (!seen->insert(key).second) {
            return;
        }
        artifacts->push_back(ref);

        const component* component_value
            = find_component(value, format_artifact_ref(ref));
        if (component_value == nullptr) {
            return;
        }
        const artifact* artifact_value
            = find_artifact(*component_value, ref.artifact_id);
        if (artifact_value == nullptr) {
            return;
        }

        if (!component_value->ownership
            && artifact_is_runnable(*artifact_value)) {
            const std::optional<artifact_ref> library_ref
                = first_library_artifact_ref(*component_value);
            if (library_ref.has_value()
                && format_artifact_ref(*library_ref) != key) {
                collect_surface_artifacts(value, *library_ref, artifacts, seen);
            }
        }

        for (const std::string& link : artifact_value->link) {
            const std::optional<artifact_ref> linked_ref
                = parse_artifact_ref(link);
            if (linked_ref.has_value()) {
                collect_surface_artifacts(value, *linked_ref, artifacts, seen);
            }
        }
    }

    std::set<std::string> all_artifact_keys(const manifest& value) {
        std::set<std::string> keys;
        for (const component& component_value : value.components) {
            for (const artifact& artifact_value : component_value.artifacts) {
                keys.insert(format_artifact_ref(
                    artifact_ref { component_value.id, artifact_value.id }
                ));
            }
        }
        return keys;
    }

    std::set<std::string> tracked_facade_artifact_keys(const manifest& value) {
        const std::optional<artifact_ref> entry_ref
            = parse_artifact_ref(value.facade_entry_artifact);
        if (!entry_ref.has_value()) {
            return all_artifact_keys(value);
        }

        std::vector<artifact_ref> artifacts;
        std::set<std::string> seen;
        collect_surface_artifacts(value, *entry_ref, &artifacts, &seen);
        for (const std::string& installed : value.install_artifacts) {
            if (const auto ref = parse_artifact_ref(installed);
                ref.has_value()) {
                collect_surface_artifacts(value, *ref, &artifacts, &seen);
            }
        }
        return seen;
    }

    bool component_has_selected_artifacts(
        const component& component_value,
        const std::set<std::string>& selected_artifact_keys
    ) {
        for (const artifact& artifact_value : component_value.artifacts) {
            if (selected_artifact_keys.contains(format_artifact_ref(
                    artifact_ref { component_value.id, artifact_value.id }
                ))) {
                return true;
            }
        }
        return false;
    }

    std::vector<const component*> selected_components(
        const manifest& value,
        const std::set<std::string>& selected_artifact_keys
    ) {
        std::vector<const component*> components;
        for (const component& component_value : value.components) {
            if (component_has_selected_artifacts(
                    component_value, selected_artifact_keys
                )) {
                components.push_back(&component_value);
            }
        }
        return components;
    }

    std::vector<const artifact*> selected_artifacts_for_component(
        const component& component_value,
        const std::set<std::string>& selected_artifact_keys
    ) {
        std::vector<const artifact*> artifacts;
        for (const artifact& artifact_value : component_value.artifacts) {
            if (selected_artifact_keys.contains(format_artifact_ref(
                    artifact_ref { component_value.id, artifact_value.id }
                ))) {
                artifacts.push_back(&artifact_value);
            }
        }
        return artifacts;
    }

    std::optional<artifact_ref> selected_library_ref(
        const component& component_value,
        const std::set<std::string>& selected_artifact_keys
    ) {
        for (const artifact& artifact_value : component_value.artifacts) {
            const artifact_ref ref { component_value.id, artifact_value.id };
            if (!selected_artifact_keys.contains(format_artifact_ref(ref))) {
                continue;
            }
            if (is_library_kind(artifact_value.kind)) {
                return ref;
            }
        }
        return std::nullopt;
    }

    std::string render_external_project_component(
        const component& component_value, const source_dependency& external,
        const std::vector<const artifact*>& artifacts
    ) {
        std::ostringstream imported_targets;
        for (const artifact* artifact_value : artifacts) {
            const auto target
                = cmake_target_name({ component_value.id, artifact_value->id });
            imported_targets
                << "add_library(" << target << " ALIAS " << external.package
                << "::" << cmake_target_name(external.artifact) << ")\n";
        }
        return render_required_sync_template(
            "cmake/component/external_project.tpl",
            { { "package", external.package },
              { "provider_target",
                external.package
                    + "::" + cmake_target_name(external.artifact) },
              { "imported_targets_block", imported_targets.str() } }
        );
    }

    std::set<std::string> exported_library_keys(const manifest& value) {
        std::set<std::string> keys;
        auto roots = value.install_artifacts;
        roots.push_back(value.facade_entry_artifact);
        std::vector<artifact_ref> refs;
        for (const auto& root : roots) {
            const auto ref = parse_artifact_ref(root);
            const auto resolved = resolve_artifact(value, ref);
            if (!resolved || !is_library_kind(resolved->artifact_value->kind)
                || source_dependency_for(*resolved->component_value))
                continue;
            std::set<std::string> closure;
            refs.clear();
            collect_surface_artifacts(value, *ref, &refs, &closure);
            keys.insert(closure.begin(), closure.end());
        }
        return keys;
    }

    manifest package_manifest(
        const manifest& value, const std::set<std::string>& keys, bool installed
    ) {
        auto result = value;
        result.components.clear();
        for (const auto* owner : selected_components(value, keys)) {
            if (installed && source_dependency_for(*owner))
                continue;
            result.components.push_back(*owner);
            // Preserve every requirement used by component_link_targets,
            // including support targets contributed by tests/benchmarks.
        }
        return result;
    }

    std::string generate_surface_cmakelists(
        const manifest& value, const fs::path& project_root,
        const bool developer_surface
    ) {
        const std::string project_prefix = to_upper_identifier(value.id);
        const bool has_assets_dir = project_has_assets_dir(project_root);
        const std::string source_root_expression = developer_surface
            ? "${ECOSYSTEM_PROJECT_ROOT}"
            : "${CMAKE_CURRENT_SOURCE_DIR}";
        const std::set<std::string> selected_artifact_keys = developer_surface
            ? all_artifact_keys(value)
            : tracked_facade_artifact_keys(value);
        const std::vector<const component*> components
            = selected_components(value, selected_artifact_keys);
        const auto export_keys = exported_library_keys(value);
        const dependency_summary dependencies = summarize_dependencies(
            package_manifest(value, selected_artifact_keys, false), std::nullopt
        );

        package_surface_options package_options;
        package_options.support_targets = true;
        package_options.imported_support_targets = !export_keys.empty();
        package_options.qt_automation = true;
        package_options.enable_testing
            = developer_surface && has_tests_enabled(value);
        const std::string developer_options_block = developer_surface
            ? render_required_sync_template("cmake/developer_options.tpl", {})
            : std::string();
        const std::string apply_defaults_block
            = render_required_sync_template("cmake/apply_defaults.tpl", {});
        const std::string assets_install_block
            = has_assets_dir && value.install_assets
            ? render_required_sync_template(
                  "cmake/assets_install.tpl",
                  {
                      { "source_root_expression", source_root_expression },
                      { "project_id", value.id },
                  }
              )
            : std::string();
        const std::string assets_embed_function_block
            = has_assets_dir && value.install_assets
            ? render_required_sync_template(
                  "cmake/assets_embed_function.tpl",
                  {
                      { "assets_embed_block",
                        render_required_sync_template(
                            "cmake/assets_embed.tpl",
                            {
                                { "source_root_expression",
                                  source_root_expression },
                                { "project_id", value.id },
                            }
                        ) },
                  }
              )
            : std::string();
        const std::string assets_block = has_assets_dir
            ? render_required_sync_template(
                  "cmake/assets_function.tpl",
                  {
                      { "source_root_expression", source_root_expression },
                      { "project_id", value.id },
                      { "assets_embed_function_block",
                        assets_embed_function_block },
                      { "assets_install_block", assets_install_block },
                  }
              )
            : std::string();

        std::ostringstream stream;
        stream << render_required_sync_template(
            "cmake/surface_prefix.tpl",
            {
                { "generated_notice",
                  developer_surface
                      ? "# This file is generated by ecosystem local tooling. "
                        "Do not edit by hand."
                      : "# This file is generated by `marx sync`. Do not edit "
                        "by hand." },
                { "project_id", value.id },
                { "project_version",
                  value.version.empty() ? "0.1.0" : value.version },
                { "developer_options_block", developer_options_block },
                { "cpp_standard", std::to_string(value.cpp_standard) },
                { "package_surface",
                  render_package_surface(dependencies, package_options) },
                { "apply_defaults_block", apply_defaults_block },
                { "assets_block", assets_block },
            }
        );

        if (value.id == "manifesto") {
            const auto runtime_ref
                = parse_artifact_ref(value.facade_entry_artifact);
            if (!runtime_ref)
                throw template_render_error(
                    "tooling runtime needs a valid facade identity"
                );
            stream << render_required_sync_template(
                "cmake/manifesto_runtime.tpl",
                { { "source_root_expression", source_root_expression },
                  { "runtime_target", cmake_target_name(*runtime_ref) } }
            ) << "\n";
        }

        for (const component* component_value : components) {
            if (source_dependency_for(*component_value).has_value()) {
                continue;
            }
            const std::string prefix = to_upper_identifier(
                value.id + "_" + component_value->id
                + (component_value->ownership
                       ? "_" + component_value->artifacts.front().id
                       : "")
            );
            append_paths(
                stream, prefix + "_HEADERS",
                component_public_headers(project_root, *component_value),
                project_root, source_root_expression
            );
            append_paths(
                stream, prefix + "_SOURCES",
                component_core_sources(project_root, *component_value),
                project_root, source_root_expression
            );
            append_paths(
                stream, prefix + "_RUNTIME_SOURCES",
                component_runtime_sources(project_root, *component_value),
                project_root, source_root_expression
            );
        }

        for (const component* component_value : components) {
            const std::vector<const artifact*> artifacts
                = selected_artifacts_for_component(
                    *component_value, selected_artifact_keys
                );
            if (artifacts.empty()) {
                continue;
            }

            if (const std::optional<source_dependency> external
                = source_dependency_for(*component_value);
                external.has_value()) {
                stream << render_external_project_component(
                    *component_value, *external, artifacts
                );
                continue;
            }

            std::string indent;
            bool closes_tests_guard = false;
            bool closes_gtest_guard = false;
            bool closes_bench_guard = false;
            bool closes_benchmark_pkg_guard = false;

            if (developer_surface && component_is_test_only(*component_value)) {
                stream << render_tests_guard_open_block(indent) << "\n";
                indent += "    ";
                closes_tests_guard = true;
                if (json_flag_enabled(component_value->tests, "gtest")) {
                    stream << render_gtest_guard_open_block(indent) << "\n";
                    indent += "    ";
                    closes_gtest_guard = true;
                }
            }
            if (developer_surface
                && component_is_benchmark_only(*component_value)) {
                stream << render_benchmarks_guard_open_block(indent) << "\n";
                indent += "    ";
                closes_bench_guard = true;
                if (json_flag_enabled(
                        component_value->benchmarks, "google_benchmark"
                    )) {
                    stream << render_benchmark_package_guard_open_block(indent)
                           << "\n";
                    indent += "    ";
                    closes_benchmark_pkg_guard = true;
                }
            }

            const std::string prefix = to_upper_identifier(
                value.id + "_" + component_value->id
                + (component_value->ownership
                       ? "_" + component_value->artifacts.front().id
                       : "")
            );
            const std::vector<fs::path> include_dirs
                = component_include_dirs(project_root, *component_value);
            const std::optional<artifact_ref> library_ref
                = selected_library_ref(
                    *component_value, selected_artifact_keys
                );
            const bool has_library = library_ref.has_value();

            for (const artifact* artifact_value : artifacts) {
                const artifact_ref ref { component_value->id,
                                         artifact_value->id };
                const std::string target_name = cmake_target_name(ref);
                const std::string output_name = artifact_output_name(
                    value, *component_value, *artifact_value
                );
                const std::vector<std::string> link_targets
                    = component_link_targets(*component_value, *artifact_value);
                const std::string link_scope
                    = link_scope_for_artifact(*artifact_value);
                const std::vector<std::string> header_lines
                    = { "${" + prefix + "_HEADERS}" };
                const bool reuse_component_library
                    = has_library && !same_artifact_ref(*library_ref, ref);

                if (artifact_value->kind == "interface_lib") {
                    stream << render_interface_artifact_block(
                        target_name, indent
                    ) << "\n";
                } else if (
                    artifact_value->kind == "static_lib"
                    || artifact_value->kind == "shared_lib"
                ) {
                    stream << render_library_artifact_block(
                        target_name,
                        artifact_value->kind == "static_lib" ? "STATIC"
                                                             : "SHARED",
                        header_lines, { "${" + prefix + "_SOURCES}" }, indent
                    ) << "\n";
                } else if (artifact_value->kind == "qt_app") {
                    const std::vector<std::string> source_lines
                        = runnable_source_lines(
                            project_root, *component_value, prefix,
                            source_root_expression, reuse_component_library
                        );
                    stream << render_qt_app_artifact_block(
                        target_name, header_lines, source_lines, indent
                    ) << "\n";
                    if (component_value->ownership
                        && component_value->ownership->qml.has_value()) {
                        stream << render_qml_module_block(
                            target_name, *component_value->ownership->qml,
                            component_qml_files(project_root, *component_value),
                            project_root, source_root_expression, indent
                        ) << "\n";
                    }
                } else {
                    const std::vector<std::string> source_lines
                        = runnable_source_lines(
                            project_root, *component_value, prefix,
                            source_root_expression, reuse_component_library
                        );
                    stream << render_executable_artifact_block(
                        target_name, header_lines, source_lines, indent
                    ) << "\n";
                }

                std::vector<std::string> include_dir_lines;
                for (const fs::path& include_dir : include_dirs) {
                    const auto path = source_path_expression(
                        source_root_expression, project_root, include_dir
                    );
                    include_dir_lines.push_back(
                        export_keys.contains(format_artifact_ref(ref))
                            ? "\"$<BUILD_INTERFACE:" + path + ">\""
                            : path
                    );
                }
                if (export_keys.contains(format_artifact_ref(ref)))
                    include_dir_lines.push_back(
                        "\"$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>\""
                    );
                stream << render_required_sync_template(
                    "cmake/artifact/compile_features.tpl",
                    { { "target_name", target_name },
                      { "visibility", link_scope },
                      { "cpp_standard", std::to_string(value.cpp_standard) } }
                ) << "\n";
                stream << render_include_dirs_block(
                    target_name,
                    artifact_value->kind == "interface_lib" ? "INTERFACE"
                                                            : "PUBLIC",
                    include_dir_lines, indent
                ) << "\n";

                std::vector<std::string> effective_link_targets;
                if (has_library && is_runnable_kind(artifact_value->kind)
                    && reuse_component_library) {
                    effective_link_targets.push_back(
                        cmake_target_name(*library_ref)
                    );
                }
                for (const std::string& link_target : link_targets) {
                    if (reuse_component_library
                        && is_runnable_kind(artifact_value->kind)
                        && link_target == cmake_target_name(*library_ref)) {
                        continue;
                    }
                    effective_link_targets.push_back(link_target);
                }
                if (!effective_link_targets.empty()) {
                    stream << render_link_libraries_block(
                        target_name, link_scope, effective_link_targets, indent
                    ) << "\n";
                }

                if (component_has_stack_key(*component_value, "opencv")) {
                    stream << render_opencv_compile_definitions_block(
                        target_name, link_scope, project_prefix, indent
                    ) << "\n";
                }

                if (component_has_stack_key(*component_value, "kde")) {
                    stream << render_kde_compile_definitions_line(
                        target_name, link_scope, indent
                    ) << "\n";
                }

                if (artifact_value->kind != "interface_lib") {
                    stream << render_output_name_line(
                        target_name, output_name, indent
                    ) << "\n";
                }
                stream << render_apply_defaults_line(target_name, indent)
                       << "\n";
                if (!value.version.empty()) {
                    stream << render_project_version_definition(
                        target_name, link_scope, indent
                    ) << "\n";
                }

                if (artifact_value->kind == "qt_app") {
                    if (component_has_stack_key(*component_value, "android")) {
                        stream << render_qt_android_package_block(
                            target_name,
                            value.android_application_id.empty()
                                ? "org.example." + value.id
                                : value.android_application_id,
                            value.android_package_source_dir,
                            source_root_expression, indent
                        ) << "\n";
                    }
                    if (has_assets_dir && value.install_assets) {
                        stream << render_embed_assets_line(target_name, indent)
                               << "\n";
                    }
                    stream << render_qt_finalize_line(target_name, indent)
                           << "\n";
                }
                if (has_assets_dir && is_runnable_kind(artifact_value->kind)) {
                    stream << render_stage_assets_line(target_name, indent)
                           << "\n";
                }

                if (developer_surface
                    && component_is_test_only(*component_value)
                    && is_runnable_kind(artifact_value->kind)) {
                    const std::string test_name = component_value->ownership
                        ? target_name
                        : component_value->id + "_" + artifact_value->id;
                    stream << render_add_test_line(
                        test_name, target_name, indent
                    ) << "\n";
                    if (json_flag_enabled(component_value->tests, "qttest")) {
                        stream
                            << render_qttest_offscreen_line(test_name, indent)
                            << "\n";
                    }
                }

                stream << "\n";
            }

            if (closes_benchmark_pkg_guard) {
                indent.erase(indent.size() - 4U);
                stream << render_benchmark_package_guard_else_block(
                    component_value->id, indent
                ) << "\n";
            }
            if (closes_bench_guard) {
                indent.erase(indent.size() - 4U);
                emit_line(stream, indent, "endif ()");
            }
            if (closes_gtest_guard) {
                indent.erase(indent.size() - 4U);
                stream << render_gtest_guard_else_block(
                    component_value->id, indent
                ) << "\n";
            }
            if (closes_tests_guard) {
                indent.erase(indent.size() - 4U);
                emit_line(stream, indent, "endif ()");
            }
            if (closes_tests_guard || closes_bench_guard) {
                stream << "\n";
            }
        }

        const std::set<std::string> install_artifact_keys
            = tracked_facade_artifact_keys(value);
        const std::optional<artifact_ref> install_entry_ref
            = parse_artifact_ref(value.facade_entry_artifact);
        const component* install_entry_component = install_entry_ref.has_value()
            ? find_component(value, format_artifact_ref(*install_entry_ref))
            : nullptr;
        const artifact* install_entry_artifact
            = install_entry_component == nullptr
                || !install_entry_ref.has_value()
            ? nullptr
            : find_artifact(
                  *install_entry_component, install_entry_ref->artifact_id
              );
        const bool library_first_install_surface
            = install_surface_is_library_first(install_entry_artifact);
        if (install_entry_ref && install_entry_artifact) {
            stream << render_required_sync_template(
                "cmake/visitor_facade.tpl",
                { { "target_name", cmake_target_name(*install_entry_ref) },
                  { "project_id", value.id } }
            ) << "\n";
        }
        std::set<std::string> installed_header_components;
        for (const component* component_value :
             selected_components(value, install_artifact_keys)) {
            if (source_dependency_for(*component_value).has_value()) {
                continue;
            }
            for (const artifact* artifact_value :
                 selected_artifacts_for_component(
                     *component_value, install_artifact_keys
                 )) {
                const artifact_ref ref {
                    component_value->id,
                    artifact_value->id,
                };
                const bool is_entry_artifact
                    = (install_entry_ref.has_value()
                       && same_artifact_ref(*install_entry_ref, ref))
                    || std::find(
                           value.install_artifacts.begin(),
                           value.install_artifacts.end(),
                           format_artifact_ref(ref)
                       ) != value.install_artifacts.end();
                const bool exported
                    = export_keys.contains(format_artifact_ref(ref));
                if (exported) {
                    stream << render_required_sync_template(
                        "cmake/install_export_target.tpl",
                        { { "target_name", cmake_target_name(ref) },
                          { "project_id", value.id } }
                    ) << "\n";
                } else if (
                    artifact_installs_target_file(
                        *artifact_value, is_entry_artifact,
                        library_first_install_surface
                    )
                ) {
                    stream << render_install_target_block(
                        cmake_target_name(ref)
                    ) << "\n";
                }
                if (!exported
                    && !artifact_installs_public_headers(
                        *artifact_value, is_entry_artifact,
                        library_first_install_surface
                    )) {
                    continue;
                }
                if (!installed_header_components
                         .insert(
                             component_value->ownership
                                 ? format_artifact_ref(ref)
                                 : component_value->id
                         )
                         .second) {
                    continue;
                }
                if (component_public_headers(project_root, *component_value)
                        .empty()) {
                    continue;
                }
                const fs::path include_dir
                    = component_root_path(project_root, *component_value)
                    / "include";
                if (component_value->ownership) {
                    for (const auto& header : component_public_headers(
                             project_root, *component_value
                         )) {
                        const auto relative
                            = header.lexically_relative(include_dir);
                        if (relative.empty() || *relative.begin() == "..")
                            continue;
                        stream << render_required_sync_template(
                            "cmake/artifact/install_header.tpl",
                            { { "header",
                                source_path_expression(
                                    source_root_expression, project_root, header
                                ) },
                              { "destination",
                                (fs::path("${CMAKE_INSTALL_INCLUDEDIR}")
                                 / relative.parent_path())
                                    .generic_string() } }
                        ) << "\n";
                    }
                    continue;
                }
                stream << render_install_include_directory_block(
                    source_path_expression(
                        source_root_expression, project_root, include_dir
                    )
                ) << "\n";
            }
        }

        if (!export_keys.empty()) {
            package_surface_options installed_options;
            installed_options.support_targets = true;
            installed_options.imported_support_targets = true;
            auto installed_packages = render_package_surface(
                summarize_dependencies(
                    package_manifest(value, export_keys, true), std::nullopt
                ),
                installed_options
            );
            std::set<std::string> external_packages;
            std::string first_exported_target;
            for (const auto& key : export_keys) {
                const auto resolved
                    = resolve_artifact(value, parse_artifact_ref(key));
                const auto external
                    = source_dependency_for(*resolved->component_value);
                if (external)
                    external_packages.insert(external->package);
                else if (first_exported_target.empty())
                    first_exported_target
                        = value.id + "::" + cmake_target_name(resolved->ref);
            }
            if (!external_packages.empty())
                installed_packages += "include(CMakeFindDependencyMacro)\n";
            for (const auto& package : external_packages)
                installed_packages
                    += "find_dependency(" + package + " CONFIG)\n";
            // Restore the consumer's profile variable after finding the
            // provider's dependencies.
            installed_packages = "set(_" + value.id
                + "_saved_kde \"${ECOSYSTEM_PROFILE_KDE}\")\n"
                  "set(ECOSYSTEM_PROFILE_KDE \"@ECOSYSTEM_PROFILE_KDE@\")\n"
                + installed_packages + "set(ECOSYSTEM_PROFILE_KDE \"${_"
                + value.id
                + "_saved_kde}\")\n"
                  "unset(_"
                + value.id + "_saved_kde)\n";
            stream << render_required_sync_template(
                "cmake/install_export_package.tpl",
                { { "project_id", value.id },
                  { "first_target", first_exported_target },
                  { "package_surface", installed_packages } }
            ) << "\n";
        }

        if (developer_surface) {
            for (const component* component_value : components) {
                const auto test_target
                    = component_generated_test_target(*component_value);
                if (!test_target.has_value()) {
                    continue;
                }

                const std::string prefix = to_upper_identifier(
                    value.id + "_" + component_value->id
                    + (component_value->ownership
                           ? "_" + component_value->artifacts.front().id
                           : "")
                );
                const std::string test_var = prefix + "_TEST_SOURCES";
                const std::vector<std::string> link_targets
                    = component_value->artifacts.empty()
                    ? std::vector<std::string>()
                    : component_link_targets(
                          *component_value, component_value->artifacts.front()
                      );
                std::vector<std::string> include_dir_expressions;
                for (const fs::path& include_dir :
                     component_include_dirs(project_root, *component_value)) {
                    include_dir_expressions.push_back(source_path_expression(
                        source_root_expression, project_root, include_dir
                    ));
                }
                std::string runtime_definition_block;
                if (const std::optional<artifact_ref> entry_ref
                    = parse_artifact_ref(value.facade_entry_artifact);
                    entry_ref.has_value()) {
                    runtime_definition_block
                        = "                ECOS_TEST_BUILD_DIR="
                          "\"$<TARGET_FILE_DIR:"
                        + cmake_target_name(*entry_ref) + ">\"\n";
                }
                std::vector<std::string> runtime_dependency_targets;
                std::set<std::string> seen_runtime_dependency_targets;
                for (const component* runtime_component : components) {
                    if (component_is_test_only(*runtime_component)
                        || component_is_benchmark_only(*runtime_component)) {
                        continue;
                    }
                    for (const artifact& artifact_value :
                         runtime_component->artifacts) {
                        if (!artifact_is_runnable(artifact_value)) {
                            continue;
                        }
                        const std::string dependency_target = cmake_target_name(
                            artifact_ref {
                                runtime_component->id,
                                artifact_value.id,
                            }
                        );
                        if (seen_runtime_dependency_targets
                                .insert(dependency_target)
                                .second) {
                            runtime_dependency_targets.push_back(
                                dependency_target
                            );
                        }
                    }
                }

                std::string test_inputs;
                if (component_value->ownership) {
                    for (const auto& path : component_test_sources(
                             project_root, *component_value
                         )) {
                        test_inputs
                            += "            "
                            + source_path_expression(
                                   source_root_expression, project_root, path
                            )
                            + "\n";
                    }
                }
                stream << render_generated_tests_block(
                    test_var,
                    component_value->ownership
                        ? ""
                        : source_path_expression(
                              source_root_expression, project_root,
                              component_root_path(
                                  project_root, *component_value
                              ) / "tests"
                          ) + "/*.cpp",
                    *test_target, "${" + prefix + "_HEADERS}",
                    "${" + prefix + "_SOURCES}", "${" + test_var + "}",
                    include_dir_expressions, link_targets,
                    source_root_expression, runtime_definition_block,
                    runtime_dependency_targets, has_assets_dir,
                    json_flag_enabled(component_value->tests, "gtest"),
                    component_value->id,
                    component_value->ownership
                        ? std::optional<std::string>(test_inputs)
                        : std::nullopt
                );
            }
        }

        return stream.str();
    }

} // namespace sync_support

using namespace sync_support;

bool artifact_has_cmake_export(const manifest& value, const artifact_ref& ref) {
    const auto resolved = resolve_artifact(value, ref);
    return resolved && !source_dependency_for(*resolved->component_value)
        && exported_library_keys(value).contains(format_artifact_ref(ref));
}

std::string
generate_cmakelists(const manifest& value, const fs::path& project_root) {
    return generate_surface_cmakelists(value, project_root, false);
}

std::string generate_developer_cmakelists(
    const manifest& value, const fs::path& project_root
) {
    return generate_surface_cmakelists(value, project_root, true);
}

std::string generate_gitignore() {
    return render_required_text_template("tracked/.gitignore.tpl", {});
}

std::vector<tracked_surface_file> generate_tracked_surface_files(
    const manifest& value, const fs::path& project_root, string_list* errors
) try {
    string_list local_errors;
    string_list* error_sink = errors == nullptr ? &local_errors : errors;
    std::vector<tracked_surface_file> files;
    const std::optional<github_actions_vars> vars
        = load_github_actions_vars(project_root, error_sink);
    if (!vars.has_value()) {
        return files;
    }

    files.push_back(
        tracked_surface_file {
            "CMakeLists.txt",
            generate_cmakelists(value, project_root),
        }
    );
    files.push_back(
        tracked_surface_file {
            ".gitignore",
            generate_gitignore(),
        }
    );

    append_tracked_template_file(
        &files, ".clang-format",
        { ".clang-format", "tooling/clang-format.tpl" }, {}, error_sink
    );
    append_tracked_template_file(
        &files, ".clang-tidy", { ".clang-tidy", "tooling/clang-tidy.tpl" }, {},
        error_sink
    );
    append_tracked_template_file(
        &files, ".github/SECURITY.md", { ".github/SECURITY.md" }, {}, error_sink
    );
    append_tracked_template_file(
        &files, ".github/renovate.json", { ".github/renovate.json" }, {},
        error_sink
    );

    const template_bindings workflow_bindings
        = github_actions_bindings(value, *vars);
    append_tracked_template_file(
        &files, ".github/actions/run-manifesto-stage/action.yml",
        { ".github/actions/run-manifesto-stage/action.yml",
          "tracked/.github/actions/run-manifesto-stage/action.yml.tpl" },
        workflow_bindings, error_sink
    );
    append_tracked_template_file(
        &files, ".github/actions/publish-manifesto-report/action.yml",
        { ".github/actions/publish-manifesto-report/action.yml",
          "tracked/.github/actions/publish-manifesto-report/action.yml.tpl" },
        workflow_bindings, error_sink
    );
    if (github_actions_use_local_setup_action(*vars)) {
        append_tracked_template_file(
            &files, ".github/actions/setup-manifesto/action.yml",
            { ".github/actions/setup-manifesto/action.yml",
              "tracked/.github/actions/setup-manifesto/action.yml.tpl" },
            workflow_bindings, error_sink
        );
    }
    append_tracked_template_file(
        &files, ".github/workflows/tests.yml",
        { ".github/workflows/tests.yml",
          "tracked/.github/workflows/tests.yml.tpl" },
        workflow_bindings, error_sink
    );
    append_tracked_template_file(
        &files, ".github/workflows/codeql.yml",
        { ".github/workflows/codeql.yml",
          "tracked/.github/workflows/codeql.yml.tpl" },
        workflow_bindings, error_sink
    );
    append_tracked_template_file(
        &files, ".github/workflows/html.yml",
        { ".github/workflows/html.yml",
          "tracked/.github/workflows/html.yml.tpl" },
        workflow_bindings, error_sink
    );

    if (!error_sink->empty()) {
        if (errors == nullptr) {
            throw template_render_error(error_sink->front());
        }
        return {};
    }
    return files;
} catch (const template_render_error& error) {

    if (errors == nullptr) {
        throw;
    }
    errors->push_back(error.what());
    return {};
}

string_list
tracked_surface_drift(const fs::path& project_root, const manifest& value) {
    string_list drift;
    const std::vector<tracked_surface_file> files
        = generate_tracked_surface_files(value, project_root, &drift);
    for (const tracked_surface_file& file_value : files) {
        std::string error_message;
        const fs::path path = project_root / file_value.relative_path;
        const std::string current_contents
            = read_text_file(path, &error_message);
        if (!error_message.empty()) {
            drift.push_back(
                file_value.relative_path.generic_string()
                + " is missing or unreadable; run `marx sync`"
            );
        } else if (current_contents != file_value.contents) {
            drift.push_back(
                file_value.relative_path.generic_string()
                + " is out of sync with ecosystem defaults; run `marx sync`"
            );
        }
    }
    append_obsolete_tracked_surface_drift(&drift, project_root, files);
    if (drift.size() >= 2U) {
        std::sort(drift.begin(), drift.end());
    }

    return drift;
}

sync_report sync_project(const fs::path& project_root, const manifest& value) {
    sync_report report;
    report.errors = validate_manifest_paths(value, project_root);
    if (!report.errors.empty()) {
        return report;
    }
    std::string error_message;
    const std::vector<tracked_surface_file> files
        = generate_tracked_surface_files(value, project_root, &report.errors);
    if (!report.errors.empty()) {
        return report;
    }
    std::vector<fs::path> destinations = tracked_surface_owned_paths();
    for (const tracked_surface_file& file_value : files) {
        destinations.push_back(file_value.relative_path);
    }
    report.errors = validate_project_paths(project_root, destinations);
    if (!report.errors.empty()) {
        return report;
    }
    for (const tracked_surface_file& file_value : files) {
        const fs::path path = project_root / file_value.relative_path;
        error_message.clear();
        if (!write_text_file(path, file_value.contents, &error_message)) {
            report.errors.push_back(error_message);
            return report;
        }
        report.written_files.push_back(path);
    }
    remove_obsolete_tracked_surface_files(&report, project_root, files);

    return report;
}

} // namespace ecosystem
