#include "workspace/doxygen.hpp"

#include "workspace/project.hpp"
#include "workspace/template_text.hpp"
#include "workspace/tooling.hpp"

#include <algorithm>
#include <fstream>

namespace ecosystem {

namespace doxygen_support {

    namespace fs = std::filesystem;

    void reject_output_symlinks(const fs::path& root, const fs::path& target) {
        const auto relative = target.lexically_relative(root);
        const auto errors = validate_project_paths(root, { relative });
        if (!errors.empty())
            throw template_render_error(errors.front());
        auto path = root;
        for (const auto& part : relative) {
            path /= part;
            std::error_code error;
            const auto status = fs::symlink_status(path, error);
            if (error && error != std::errc::no_such_file_or_directory)
                throw template_render_error(
                    path.string() + ": " + error.message()
                );
            if (fs::is_symlink(status))
                throw template_render_error(
                    "Doxygen service output must not use a symlink: "
                    + path.lexically_relative(root).generic_string()
                );
        }
        if (fs::is_directory(target))
            for (const auto& entry : fs::recursive_directory_iterator(target))
                if (entry.is_symlink())
                    throw template_render_error(
                        "Doxygen service output must not contain a symlink: "
                        + entry.path().lexically_relative(root).generic_string()
                    );
    }

    void validate_outputs(const fs::path& root, const fs::path& directory) {
        for (const std::string name :
             { "Doxyfile", "html", "warnings.log", "doxygen.log" }) {
            reject_output_symlinks(root, directory / name);
            const auto status = fs::status(directory / name);
            if (fs::exists(status)
                && (name == "html" ? !fs::is_directory(status)
                                   : !fs::is_regular_file(status)))
                throw template_render_error(
                    "invalid Doxygen output path type: "
                    + (directory / name)
                          .lexically_relative(root)
                          .generic_string()
                );
        }
    }

    std::string quote(const std::string& value) {
        // Doxygen expands environment references even inside quoted strings.
        if (value.find("$(") != std::string::npos
            || std::any_of(value.begin(), value.end(), [](unsigned char ch) {
                   return ch < 32 || ch == 127;
               }))
            throw template_render_error(
                "Doxygen value contains a control character or environment "
                "expansion: "
                + value
            );
        std::string result = "\"";
        for (const char ch : value) {
            if (ch == '\\' || ch == '"')
                result += '\\';
            result += ch;
        }
        return result + '"';
    }

    std::string input_list(const fs::path& root, std::vector<fs::path> files) {
        std::sort(files.begin(), files.end());
        files.erase(std::unique(files.begin(), files.end()), files.end());
        std::string result;
        for (const auto& file : files) {
            const auto relative = file.lexically_relative(root);
            const auto errors = validate_project_paths(root, { relative });
            if (!errors.empty())
                throw template_render_error(errors.front());
            std::error_code error;
            if (!fs::is_regular_file(file, error) || error
                || !std::ifstream(file).good())
                throw template_render_error(
                    "unable to read Doxygen input: " + relative.generic_string()
                );
            if (!result.empty())
                result += " ";
            result += quote(relative.generic_string());
        }
        return result;
    }

} // namespace doxygen_support

std::filesystem::path local_doxygen_dir(
    const std::filesystem::path& root,
    const std::optional<artifact_ref>& requested
) {
    auto path = local_state_dir(root) / "doxygen";
    if (requested)
        path /= std::filesystem::path("artifacts") / requested->component_id
            / requested->artifact_id;
    return path;
}

bool write_local_doxygen_config(
    const std::filesystem::path& root, const manifest& value,
    const std::optional<artifact_ref>& requested, std::string* error_message
) {
    namespace fs = std::filesystem;
    error_message->clear();
    using doxygen_support::quote;
    const auto directory = local_doxygen_dir(root, requested);
    const auto config = directory / "Doxyfile";
    try {
        if (requested && !resolve_artifact(value, requested))
            throw template_render_error(
                "unknown artifact request: " + format_artifact_ref(*requested)
            );
        auto files = declared_cpp_files(value, root, requested);
        if (files.empty())
            throw template_render_error("no owned C/C++ inputs for Doxygen");
        if (!requested)
            for (const auto& name : { "README.md", "readme.md" }) {
                std::error_code error;
                if (fs::exists(root / name, error))
                    files.push_back(root / name);
            }
        const auto inputs = doxygen_support::input_list(root, std::move(files));
        // Validate before creating service directories or replacing config.
        doxygen_support::validate_outputs(root, directory);
        const auto output = directory.lexically_relative(root).generic_string();
        const auto database = local_build_dir(root, "debug");
        std::error_code error;
        const bool has_database
            = fs::is_regular_file(database / "compile_commands.json", error);
        const std::string contents = render_text_template_candidates(
            { "tooling/Doxyfile.tpl" },
            { { "project_name", quote(value.id) },
              { "project_version", quote(value.version) },
              { "input_files", inputs },
              { "output_dir", quote(output) },
              { "warning_log", quote(output + "/warnings.log") },
              { "clang_database",
                quote(
                    has_database
                        ? database.lexically_relative(root).generic_string()
                        : ""
                ) } },
            error_message
        );
        if (!error_message->empty())
            throw template_render_error(*error_message);
        if (!write_text_file(config, contents, error_message))
            throw template_render_error(*error_message);
        return true;
    } catch (const std::exception& error) {
        *error_message = config.lexically_relative(root).generic_string() + ": "
            + error.what();
        return false;
    }
}

bool prepare_local_doxygen_output(
    const std::filesystem::path& root,
    const std::optional<artifact_ref>& requested, std::string* error_message
) {
    const auto directory = local_doxygen_dir(root, requested);
    try {
        doxygen_support::validate_outputs(root, directory);
        for (const auto& name : { "doxygen.log", "warnings.log" })
            if (!write_text_file(directory / name, "", error_message))
                return false;
        // Recreate only this scope's generated HTML; stale pages must not
        // survive ownership changes or stand in for output from an unexecuted
        // tool.
        std::filesystem::remove_all(directory / "html");
        return true;
    } catch (const std::exception& error) {
        *error_message = directory.lexically_relative(root).generic_string()
            + ": " + error.what();
        return false;
    }
}

} // namespace ecosystem
