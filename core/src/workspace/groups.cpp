#include "workspace/groups.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace ecosystem {
namespace workspace_groups_support {

    const std::regex group_id_pattern("^[a-z][a-z0-9_]*$");

    std::string trim_copy(const std::string& value) {
        const std::size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }
        const std::size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1U);
    }

    void append_unique_value(string_list* values, const std::string& value) {
        if (std::find(values->begin(), values->end(), value) == values->end()) {
            values->push_back(value);
        }
    }

    string_list parse_project_selectors(
        const json& value, const std::string& context, string_list* errors
    ) {
        string_list selectors;
        if (value.is_string()) {
            const std::string selector = trim_copy(value.get<std::string>());
            if (selector.empty()) {
                errors->push_back(context + " must not be empty");
                return selectors;
            }
            selectors.push_back(selector);
            return selectors;
        }
        if (!value.is_array()) {
            errors->push_back(
                context + " must be a string or an array of project selectors"
            );
            return selectors;
        }

        for (std::size_t index = 0U; index < value.size(); ++index) {
            if (!value.at(index).is_string()) {
                errors->push_back(
                    context + "[" + std::to_string(index)
                    + "] must be a project selector string"
                );
                continue;
            }
            const std::string selector
                = trim_copy(value.at(index).get<std::string>());
            if (selector.empty()) {
                errors->push_back(
                    context + "[" + std::to_string(index)
                    + "] must not be empty"
                );
                continue;
            }
            append_unique_value(&selectors, selector);
        }
        return selectors;
    }

    workspace_group parse_workspace_group(
        const std::string& id, const json& value, string_list* errors
    ) {
        workspace_group group;
        group.id = trim_copy(id);
        if (!std::regex_match(group.id, group_id_pattern)) {
            errors->push_back(
                "manifesto.workspace.json.groups." + id
                + " must use snake_case ids"
            );
        }
        group.project_selectors = parse_project_selectors(
            value, "manifesto.workspace.json.groups." + id, errors
        );
        if (group.project_selectors.empty()) {
            errors->push_back(
                "manifesto.workspace.json.groups." + id
                + " must contain at least one project selector"
            );
        }
        return group;
    }

} // namespace workspace_groups_support

std::filesystem::path
workspace_config_path(const std::filesystem::path& workspace_root) {
    return workspace_root / "manifesto.workspace.json";
}

workspace_config_report
load_workspace_config(const std::filesystem::path& workspace_root) {
    workspace_config_report report;
    report.path = workspace_config_path(workspace_root);

    std::error_code error;
    if (!fs::exists(report.path, error)) {
        return report;
    }
    if (error) {
        report.errors.push_back(error.message());
        return report;
    }

    report.has_config = true;
    std::ifstream file(report.path);
    if (!file.is_open()) {
        report.errors.push_back("unable to open " + report.path.string());
        return report;
    }

    workspace_config value;
    try {
        const json root = json::parse(file);
        if (!root.is_object()) {
            report.errors.emplace_back(
                "manifesto.workspace.json must be a JSON object"
            );
            return report;
        }
        for (auto iterator = root.begin(); iterator != root.end(); ++iterator) {
            if (iterator.key() != "groups") {
                report.errors.push_back(
                    "manifesto.workspace.json." + iterator.key()
                    + " is not supported"
                );
            }
        }
        if (root.contains("groups")) {
            if (!root.at("groups").is_object()) {
                report.errors.emplace_back(
                    "manifesto.workspace.json.groups must be a JSON object"
                );
            } else {
                for (auto iterator = root.at("groups").begin();
                     iterator != root.at("groups").end(); ++iterator) {
                    value.groups.push_back(
                        workspace_groups_support::parse_workspace_group(
                            iterator.key(), iterator.value(), &report.errors
                        )
                    );
                }
            }
        }
    } catch (const json::parse_error& error_value) {
        report.errors.push_back(
            "manifesto.workspace.json parse error: "
            + std::string(error_value.what())
        );
    } catch (const json::type_error& error_value) {
        report.errors.push_back(
            "manifesto.workspace.json type error: "
            + std::string(error_value.what())
        );
    }

    if (report.errors.empty()) {
        report.value = value;
    }
    return report;
}

const workspace_group*
find_workspace_group(const workspace_config& value, const std::string& id) {
    for (const workspace_group& group : value.groups) {
        if (group.id == id) {
            return &group;
        }
    }
    return nullptr;
}

json to_json(const workspace_config& value) {
    json root = json::object();
    json groups = json::object();
    for (const workspace_group& group : value.groups) {
        groups[group.id] = group.project_selectors;
    }
    root["groups"] = groups;
    return root;
}

} // namespace ecosystem
