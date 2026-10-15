#include "internal.hpp"
#include "workspace/tooling.hpp"

#include "workspace/project.hpp"
#include "workspace/template_text.hpp"

namespace fs = std::filesystem;

namespace ecosystem {
using namespace tooling_detail;

namespace {

    bool render_template(
        const fs::path& relative_path, const template_bindings& bindings,
        std::string* contents, std::string* error_message
    ) {
        *contents
            = render_text_template(relative_path, bindings, error_message);
        return error_message == nullptr || error_message->empty();
    }

    bool
    tree_has_extension(const fs::path& root, const std::string& extension) {
        std::error_code error;
        if (!fs::exists(root, error) || error) {
            return false;
        }

        fs::recursive_directory_iterator iterator(root, error);
        const fs::recursive_directory_iterator end;
        while (!error && iterator != end) {
            if (iterator->is_regular_file(error)
                && iterator->path().extension().generic_string() == extension) {
                return true;
            }

            error.clear();
            iterator.increment(error);
        }

        return false;
    }

    void validate_outputs(const fs::path& root) {
        const auto inspect = [&](const fs::path& path) {
            std::error_code error;
            const auto status = fs::symlink_status(path, error);
            if (error && error != std::errc::no_such_file_or_directory)
                throw template_render_error(
                    path.string() + ": " + error.message()
                );
            if (fs::is_symlink(status))
                throw template_render_error(
                    "Sphinx service output must not use a symlink: "
                    + path.lexically_relative(root).generic_string()
                );
            if (fs::is_regular_file(status) && fs::hard_link_count(path) > 1)
                throw template_render_error(
                    "Sphinx service output must not use a hard link: "
                    + path.lexically_relative(root).generic_string()
                );
            if (fs::exists(status) && !fs::is_directory(status)
                && !fs::is_regular_file(status))
                throw template_render_error(
                    "invalid Sphinx output path type: "
                    + path.lexically_relative(root).generic_string()
                );
            return status;
        };
        const auto directory = local_sphinx_dir(root);
        for (const auto& path :
             { local_state_dir(root), directory, directory / "conf.py",
               directory / "ecosystem_presentation.py",
               directory / "ecosystem_documentation_boundary.py",
               directory / "sphinx.log", directory / "html",
               directory / "html.pending", directory / "html.previous" }) {
            const auto status = inspect(path);
            const bool file = path.filename() == "conf.py"
                || path.filename() == "ecosystem_presentation.py"
                || path.filename() == "ecosystem_documentation_boundary.py"
                || path.filename() == "sphinx.log";
            if (fs::exists(status)
                && (file ? !fs::is_regular_file(status)
                         : !fs::is_directory(status)))
                throw template_render_error(
                    "invalid Sphinx output path type: "
                    + path.lexically_relative(root).generic_string()
                );
        }
        // Sphinx writes nested pages and cached doctrees as well as conf.py.
        // Reject aliases before any generated configuration is replaced.
        if (fs::is_directory(directory))
            for (const auto& entry :
                 fs::recursive_directory_iterator(directory))
                inspect(entry.path());
    }

} // namespace

fs::path sphinx_source_dir(const fs::path& project_root) {
    const auto stable = project_root / "assets/docs";
    const auto errors = validate_project_paths(
        project_root,
        { "assets/docs", "assets/docs/index.md", "assets/docs/index.rst" }
    );
    if (!errors.empty())
        throw template_render_error(errors.front());
    const bool markdown = fs::is_regular_file(stable / "index.md");
    const bool rst = fs::is_regular_file(stable / "index.rst");
    if (markdown == rst)
        throw template_render_error(
            "documentation-source error: assets/docs requires exactly one "
            "index.md or index.rst; root docs is not an authored source"
        );
    return stable;
}

bool write_local_sphinx_conf(
    const fs::path& project_root, const std::string& project_name,
    std::string* error_message
) {
    error_message->clear();
    fs::path docs_dir;
    try {
        validate_outputs(project_root);
        docs_dir = sphinx_source_dir(project_root);
    } catch (const std::exception& error) {
        if (error_message)
            *error_message = error.what();
        return false;
    }
    json excluded = json::array({ "_build" });
    const auto selection = docs_dir / "sphinx.json";
    try {
        if (fs::exists(fs::symlink_status(selection))) {
            const auto errors = validate_project_paths(
                project_root, { selection.lexically_relative(project_root) }
            );
            if (!errors.empty())
                throw template_render_error(errors.front());
            const auto contents = read_text_file(selection, error_message);
            if (!error_message->empty())
                return false;
            const auto options = json::parse(contents);
            if (!options.is_object())
                throw template_render_error("expected an object");
            for (const auto& [key, value] : options.items()) {
                if (key != "exclude_patterns" || !value.is_array())
                    throw template_render_error(
                        "only an exclude_patterns array is supported"
                    );
                for (const auto& pattern : value) {
                    if (!pattern.is_string()
                        || pattern.get<std::string>().empty())
                        throw template_render_error(
                            "exclude_patterns must contain nonempty strings"
                        );
                    excluded.push_back(pattern);
                }
            }
        }
    } catch (const std::exception& error) {
        *error_message = selection.lexically_relative(project_root).string()
            + ": " + error.what();
        return false;
    }
    // The same validator guards the actor and direct sphinx-build invocations.
    // It lives with generated configuration, never in authored documentation.
    std::string boundary;
    const auto boundary_path = local_sphinx_dir(project_root)
        / "ecosystem_documentation_boundary.py";
    if (!render_template(
            "tooling/documentation_boundary.py", {}, &boundary, error_message
        )
        || !write_text_file(boundary_path, boundary, error_message))
        return false;
    const auto validation = capture_command_result(
        { "python3", boundary_path.string(), docs_dir.string() }, project_root
    );
    if (validation.exit_code != 0) {
        *error_message = "documentation-source error: " + validation.output;
        return false;
    }
    const bool has_markdown_docs = tree_has_extension(docs_dir, ".md");
    std::string contents;
    if (!render_template(
            "tooling/sphinx_conf.py.tpl",
            {
                { "project_name", json(project_name).dump() },
                { "exclude_patterns", excluded.dump() },
                { "extensions",
                  has_markdown_docs ? "    'myst_parser',\n" : std::string() },
                { "source_suffix_markdown",
                  has_markdown_docs ? "    '.md': 'markdown',\n"
                                    : std::string() },
            },
            &contents, error_message
        )) {
        return false;
    }

    std::string presentation;
    if (!render_template(
            "tooling/sphinx_presentation.py.tpl", {}, &presentation,
            error_message
        ))
        return false;
    for (const auto* name :
         { "__init__.py", "inputs.py", "reports.py", "coverage.py",
           "benchmarks.py", "benchmark_data.py", "diagnostics.py",
           "diagnostics.js", "diagnostics.css", "assets.py", "readme.py",
           "previews.py", "carousel.css", "carousel.js" }) {
        const auto relative = fs::path("sphinx_presentation") / name;
        std::string module;
        if (!render_template(
                fs::path("tooling") / relative, {}, &module, error_message
            )
            || !write_text_file(
                local_sphinx_dir(project_root) / relative, module, error_message
            ))
            return false;
    }
    for (const auto* name : { "layout.html", "ecosystem-navigation.html" }) {
        std::string page;
        if (!render_template(
                fs::path("tooling/sphinx_templates") / name, {}, &page,
                error_message
            )
            || !write_text_file(
                local_sphinx_dir(project_root) / "templates" / name, page,
                error_message
            ))
            return false;
    }
    std::string coverage;
    if (!render_template(
            "tooling/coverage_site.py", {}, &coverage, error_message
        )
        || !write_text_file(
            local_sphinx_dir(project_root) / "coverage_site.py", coverage,
            error_message
        ))
        return false;
    return write_text_file(
               local_sphinx_dir(project_root) / "ecosystem_presentation.py",
               presentation, error_message
           )
        && write_text_file(
               local_sphinx_dir(project_root) / "conf.py", contents,
               error_message
        );
}

bool prepare_local_sphinx_output(
    const fs::path& root, std::string* error_message
) {
    const auto directory = local_sphinx_dir(root);
    try {
        validate_outputs(root);
        // A failed publication rollback is recovery state, never disposable
        // cache.
        if (fs::exists(directory / "html.previous"))
            throw template_render_error("Sphinx recovery output exists at "
                                        ".ecosystem/sphinx/html.previous; "
                                        "restore or move it before rebuilding");
        fs::remove_all(directory / "html.pending");
        fs::create_directories(directory / "html.pending");
        return write_text_file(directory / "sphinx.log", "", error_message);
    } catch (const std::exception& error) {
        *error_message = error.what();
        return false;
    }
}

bool publish_local_sphinx_output(
    const fs::path& root, std::string* error_message
) {
    const auto directory = local_sphinx_dir(root);
    const auto pending = directory / "html.pending";
    const auto published = directory / "html";
    const auto previous = directory / "html.previous";
    try {
        validate_outputs(root);
        for (const auto* name : { "index.html", "_ecosystem/index.html",
                                  "_ecosystem/doxygen/index.html",
                                  "_ecosystem/coverage/index.html" })
            if (!fs::is_regular_file(pending / name)
                || fs::file_size(pending / name) == 0)
                throw template_render_error(
                    "sphinx produced no fresh HTML index; expected "
                    ".ecosystem/sphinx/html.pending/"
                    + std::string(name)
                );
        if (fs::exists(previous))
            throw template_render_error("Sphinx recovery output exists at "
                                        ".ecosystem/sphinx/html.previous");
        const bool had_output = fs::exists(published);
        if (had_output)
            fs::rename(published, previous);
        std::error_code publish_error;
        fs::rename(pending, published, publish_error);
        if (publish_error) {
            std::error_code rollback_error;
            if (had_output)
                fs::rename(previous, published, rollback_error);
            throw template_render_error(
                "unable to publish Sphinx HTML: " + publish_error.message()
                + (rollback_error
                       ? "; restore .ecosystem/sphinx/html.previous: "
                           + rollback_error.message()
                       : "")
            );
        }
        fs::remove_all(previous);
        return true;
    } catch (const std::exception& error) {
        *error_message = error.what();
        return false;
    }
}

} // namespace ecosystem
