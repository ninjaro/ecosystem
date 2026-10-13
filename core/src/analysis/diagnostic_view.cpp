#include "analysis/diagnostic_view.hpp"

#include <algorithm>
#include <map>
#include <tuple>

namespace ecosystem {

std::vector<diagnostic_group>
group_diagnostics(const json& report, const diagnostic_view& view) {
    std::map<std::pair<std::string, std::string>, diagnostic_group> groups;
    for (const auto& finding : report.at("findings")) {
        const auto category = finding.at("category").get<std::string>();
        const auto severity = finding.at("severity").get<std::string>();
        if ((view.category && *view.category != category)
            || (view.severity && *view.severity != severity))
            continue;
        auto& group = groups[{ category, severity }];
        group.category = category;
        group.severity = severity;
        group.findings.push_back(&finding);
    }
    std::vector<diagnostic_group> result;
    for (auto& [key, group] : groups) {
        (void)key;
        result.push_back(std::move(group));
    }
    const auto rank = [](const std::string& severity) {
        if (severity == "error" || severity == "fatal")
            return 0;
        if (severity == "warning")
            return 1;
        if (severity == "note")
            return 2;
        return 3;
    };
    std::stable_sort(
        result.begin(), result.end(),
        [&](const auto& a, const auto& b) {
            return std::tuple(a.category, rank(a.severity), a.severity)
                < std::tuple(b.category, rank(b.severity), b.severity);
        }
    );
    return result;
}

} // namespace ecosystem
