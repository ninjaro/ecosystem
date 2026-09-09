#include "packages/package_surface_render.hpp"

#include "packages/package_dependency.hpp"
#include "packages/package_surface.hpp"
#include "workspace/template_text.hpp"

#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ecosystem {
namespace package_surface_render_support {

    std::string render_package_surface_template(
        const std::filesystem::path& relative_path,
        const template_bindings& bindings, std::string*
    ) {
        return render_required_text_template(relative_path, bindings);
    }

    std::string
    newline_terminated_lines(const std::vector<std::string_view>& lines) {
        std::ostringstream stream;
        for (const std::string_view line : lines) {
            stream << line << "\n";
        }
        return stream.str();
    }

    std::string
    newline_terminated_lines(const std::vector<std::string>& lines) {
        std::ostringstream stream;
        for (const std::string& line : lines) {
            stream << line << "\n";
        }
        return stream.str();
    }

    std::string prefixed_values_block(
        const std::set<std::string>& values, const std::string& prefix
    ) {
        std::ostringstream stream;
        for (const std::string& value : values) {
            stream << prefix << value << "\n";
        }
        return stream.str();
    }

    std::string indented_values_block(
        const std::vector<std::string_view>& values, const std::string& indent
    ) {
        std::ostringstream stream;
        for (const std::string_view value : values) {
            stream << indent << "        " << value << "\n";
        }
        return stream.str();
    }

    bool dependency_id_enabled(
        const dependency_summary& dependencies,
        const std::optional<dependency_id>& dependency
    );

    std::string conditional_surface_lines_block(
        const std::vector<package_surface_conditional_line_rule>& lines,
        const dependency_summary& dependencies
    ) {
        std::ostringstream stream;
        for (const package_surface_conditional_line_rule& line : lines) {
            if (!dependency_id_enabled(dependencies, line.dependency)) {
                continue;
            }
            stream << line.line << "\n";
        }
        return stream.str();
    }

    std::string render_if_block(
        const std::string& condition, const std::string& body,
        std::string* error_message
    ) {
        return render_package_surface_template(
            "cmake/package_surface/if_block.tpl",
            {
                { "condition", condition },
                { "body", body },
            },
            error_message
        );
    }

    std::string render_add_interface_library(
        const std::string& target_name, bool imported,
        std::string* error_message
    ) {
        return render_package_surface_template(
            imported
                ? "cmake/package_surface/add_imported_interface_library.tpl"
                : "cmake/package_surface/add_interface_library.tpl",
            { { "target_name", target_name } }, error_message
        );
    }

    std::string render_static_link_libraries(
        const package_surface_target_rule& target, const std::string& indent,
        std::string* error_message
    ) {
        if (target.link_libraries.empty()) {
            return {};
        }

        if (target.link_libraries.size() == 1) {
            return render_package_surface_template(
                "cmake/package_surface/target_link_libraries_single.tpl",
                {
                    { "indent", indent },
                    { "target_name", std::string(target.name) },
                    { "library", std::string(target.link_libraries.front()) },
                },
                error_message
            );
        }

        return render_package_surface_template(
            "cmake/package_surface/target_link_libraries_multi.tpl",
            {
                { "indent", indent },
                { "target_name", std::string(target.name) },
                { "libraries_block",
                  indented_values_block(target.link_libraries, indent) },
            },
            error_message
        );
    }

    std::string render_static_include_directories(
        const package_surface_target_rule& target, const std::string& indent,
        std::string* error_message
    ) {
        if (target.include_directories.empty()) {
            return {};
        }

        if (target.system_include_directories) {
            return render_package_surface_template(
                "cmake/package_surface/"
                "target_include_directories_system_multi.tpl",
                {
                    { "indent", indent },
                    { "target_name", std::string(target.name) },
                    { "include_directories_block",
                      indented_values_block(
                          target.include_directories, indent
                      ) },
                },
                error_message
            );
        }

        if (target.include_directories.size() == 1) {
            return render_package_surface_template(
                "cmake/package_surface/target_include_directories_single.tpl",
                {
                    { "indent", indent },
                    { "target_name", std::string(target.name) },
                    { "include_directory",
                      std::string(target.include_directories.front()) },
                },
                error_message
            );
        }

        return render_package_surface_template(
            "cmake/package_surface/target_include_directories_multi.tpl",
            {
                { "indent", indent },
                { "target_name", std::string(target.name) },
                { "include_directories_block",
                  indented_values_block(target.include_directories, indent) },
            },
            error_message
        );
    }

    std::string render_static_support_target_body(
        const package_surface_target_rule& target, const std::string& indent,
        std::string* error_message
    ) {
        std::string body;
        body += render_static_link_libraries(target, indent, error_message);
        if (error_message != nullptr && !error_message->empty()) {
            return {};
        }
        body
            += render_static_include_directories(target, indent, error_message);
        if (error_message != nullptr && !error_message->empty()) {
            return {};
        }
        return body;
    }

    std::string render_static_support_target(
        const package_surface_target_rule& target, bool imported,
        std::string* error_message
    ) {
        std::string rendered = render_add_interface_library(
            std::string(target.name), imported, error_message
        );
        if (error_message != nullptr && !error_message->empty()) {
            return {};
        }

        if (target.condition.empty()) {
            rendered
                += render_static_support_target_body(target, {}, error_message);
            return rendered;
        }

        const std::string body
            = render_static_support_target_body(target, "    ", error_message);
        if (error_message != nullptr && !error_message->empty()) {
            return {};
        }
        rendered += render_if_block(
            std::string(target.condition), body, error_message
        );
        return rendered;
    }

    bool dependency_id_enabled(
        const dependency_summary& dependencies,
        const std::optional<dependency_id>& dependency
    ) {
        if (!dependency.has_value()) {
            return true;
        }
        return dependency_entry_for_id(dependencies, *dependency).enabled;
    }

    std::string render_component_find_package_block(
        const std::string& pre_find_package_lines,
        const std::string& find_package_open_line,
        const std::string& component_lines,
        const std::string& find_package_close_line,
        const std::string& post_find_package_lines,
        const std::string& qt_automation_block, std::string* error_message
    ) {
        return render_package_surface_template(
            "cmake/package_surface/component_find_package.tpl",
            {
                { "pre_find_package_lines", pre_find_package_lines },
                { "find_package_open_line", find_package_open_line },
                { "component_lines", component_lines },
                { "find_package_close_line", find_package_close_line },
                { "post_find_package_lines", post_find_package_lines },
                { "qt_automation_block", qt_automation_block },
            },
            error_message
        );
    }

    std::string render_profile_support_target_link_block(
        const package_surface_profile_rule& rule,
        const dependency_summary& dependencies, std::string* error_message
    ) {
        std::string link_lines;
        if (dependency_id_enabled(
                dependencies, rule.support_component_dependency
            )) {
            const dependency_entry& entry = dependency_entry_for_id(
                dependencies, *rule.support_component_dependency
            );
            for (const std::string& value : entry.values) {
                link_lines += rule.support_component_line_prefix;
                link_lines += rule.support_component_value_prefix;
                link_lines += value;
                link_lines += "\n";
            }
        }
        link_lines += conditional_surface_lines_block(
            rule.fixed_support_link_lines, dependencies
        );

        return render_package_surface_template(
            "cmake/package_surface/profile_support_target_link_block.tpl",
            {
                { "target_name", std::string(rule.support_target_name) },
                { "link_lines", link_lines },
            },
            error_message
        );
    }

    std::string profile_component_package_value(
        const package_surface_profile_rule& rule, const std::string& value
    ) {
        for (const auto& [component, package] :
             rule.component_find_package_value_aliases) {
            if (component == value) {
                return std::string(package);
            }
        }
        return value;
    }

    std::string render_static_surface_block(
        const package_surface_rule& rule,
        const package_surface_options& options, std::string* error_message
    ) {
        std::string stream = newline_terminated_lines(rule.find_package_lines);
        if (!options.support_targets || rule.support_target.name.empty()) {
            return stream;
        }
        stream += newline_terminated_lines(rule.pre_target_lines);
        if (rule.blank_line_before_target) {
            stream += "\n";
        }
        stream += render_static_support_target(
            rule.support_target, options.imported_support_targets, error_message
        );
        return stream;
    }

    std::string render_component_surface_block(
        const package_surface_rule& rule,
        const dependency_summary& dependencies,
        const package_surface_options& options, std::string* error_message
    ) {
        if (!rule.component_rule.dependency.has_value()) {
            return {};
        }

        const dependency_entry& entry = dependency_entry_for_id(
            dependencies, *rule.component_rule.dependency
        );
        if (!entry.enabled) {
            return {};
        }
        std::string qt_automation_block;
        if (options.qt_automation
            && !rule.component_rule.qt_automation_lines.empty()) {
            if (rule.component_rule.blank_line_before_qt_automation) {
                qt_automation_block += "\n";
            }
            qt_automation_block += newline_terminated_lines(
                rule.component_rule.qt_automation_lines
            );
        }

        return render_component_find_package_block(
            newline_terminated_lines(
                rule.component_rule.pre_find_package_lines
            ),
            std::string(rule.component_rule.find_package_open_line),
            prefixed_values_block(
                entry.values,
                std::string(rule.component_rule.component_line_prefix)
            ),
            std::string(rule.component_rule.find_package_close_line),
            newline_terminated_lines(
                rule.component_rule.post_find_package_lines
            ),
            qt_automation_block, error_message
        );
    }

    std::string render_profile_surface_block(
        const package_surface_rule& rule,
        const dependency_summary& dependencies,
        const package_surface_options& options, std::string* error_message
    ) {
        std::string stream;
        if (options.emit_profile_option_lines) {
            stream += newline_terminated_lines(rule.profile_rule.option_lines);
        }

        if (options.support_targets
            && !rule.profile_rule.support_target_name.empty()) {
            stream += render_add_interface_library(
                std::string(rule.profile_rule.support_target_name),
                options.imported_support_targets, error_message
            );
            if (error_message != nullptr && !error_message->empty()) {
                return {};
            }
        }

        std::string body;
        if (dependency_id_enabled(
                dependencies, rule.profile_rule.component_dependency
            )) {
            const dependency_entry& entry = dependency_entry_for_id(
                dependencies, *rule.profile_rule.component_dependency
            );
            if (!rule.profile_rule.component_find_package_line_prefix.empty()) {
                for (const std::string& value : entry.values) {
                    body
                        += rule.profile_rule.component_find_package_line_prefix;
                    body += profile_component_package_value(
                        rule.profile_rule, value
                    );
                    body
                        += rule.profile_rule.component_find_package_line_suffix;
                    body += "\n";
                }
            } else {
                body += render_component_find_package_block(
                    {},
                    std::string(
                        rule.profile_rule.component_find_package_open_line
                    ),
                    prefixed_values_block(
                        entry.values,
                        std::string(rule.profile_rule.component_line_prefix)
                    ),
                    std::string(
                        rule.profile_rule.component_find_package_close_line
                    ),
                    {}, {}, error_message
                );
            }
            if (error_message != nullptr && !error_message->empty()) {
                return {};
            }
        }

        body += conditional_surface_lines_block(
            rule.profile_rule.fixed_find_package_lines, dependencies
        );

        if (options.support_targets
            && !rule.profile_rule.support_target_name.empty()) {
            body += render_profile_support_target_link_block(
                rule.profile_rule, dependencies, error_message
            );
            if (error_message != nullptr && !error_message->empty()) {
                return {};
            }
        }

        stream += render_if_block(
            std::string(rule.profile_rule.condition), body, error_message
        );
        return stream;
    }

} // namespace package_surface_render_support

using namespace package_surface_render_support;

std::string render_package_surface_rule(
    const package_surface_rule& rule, const dependency_summary& dependencies,
    const package_surface_options& options
) {
    std::string error_message;
    switch (rule.source) {
    case package_surface_source::static_rule: {
        const std::string rendered
            = render_static_surface_block(rule, options, &error_message);
        return error_message.empty() ? rendered : std::string();
    }
    case package_surface_source::component_rule: {
        const std::string rendered = render_component_surface_block(
            rule, dependencies, options, &error_message
        );
        return error_message.empty() ? rendered : std::string();
    }
    case package_surface_source::profile_rule: {
        const std::string rendered = render_profile_surface_block(
            rule, dependencies, options, &error_message
        );
        return error_message.empty() ? rendered : std::string();
    }
    }

    return {};
}

std::string render_package_surface_block(
    const package_surface_block block, const dependency_summary& dependencies,
    const package_surface_options& options
) {
    const package_surface_rule* rule = find_package_surface_rule(block);
    if (rule == nullptr) {
        return {};
    }
    return render_package_surface_rule(*rule, dependencies, options);
}

} // namespace ecosystem
