#pragma once

#include "analysis/diagnostic.hpp"

#include <filesystem>
#include <optional>
#include <ostream>
#include <vector>

namespace ecosystem {

struct personal_report {
    std::string project;
    std::string profile;
    std::optional<artifact_ref> requested_artifact;
    std::vector<diagnostic_finding> findings;
    string_list errors;
    json files = json::array();
    json scopes = json::array();
    json referents = json::array();
    json modules = json::array();
};

personal_report analyze_personal_checks(
    const manifest& value, const std::filesystem::path& project_root,
    const std::optional<artifact_ref>& requested_artifact,
    const std::string& profile = "naming"
);
json to_json(const personal_report& report);
void render_personal_report(const personal_report& report, std::ostream& out);

} // namespace ecosystem
