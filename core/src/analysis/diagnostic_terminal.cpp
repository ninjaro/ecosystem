#include "analysis/diagnostic_view.hpp"

#include <algorithm>

namespace ecosystem {

void render_diagnostic_report(
    const json& report, std::ostream& out, const diagnostic_view& view
) {
    if (view.format == diagnostic_format::markdown) {
        render_diagnostic_markdown(report, out, view);
        return;
    }
    if (report.contains("projects")) {
        out << "workspace " << report.at("workspace_root").get<std::string>()
            << ": " << report.at("status").get<std::string>() << "\n";
        for (const auto& project : report.at("projects"))
            render_diagnostic_report(project, out, view);
        return;
    }
    const auto profile = report.at("profile").get<std::string>();
    out << report.at("project").get<std::string>();
    if (report.contains("root"))
        out << " (" << report.at("root").get<std::string>() << ')';
    if (report.contains("artifact"))
        out << " [" << report.at("artifact").get<std::string>() << ']';
    out << " " << profile << ": " << report.at("status").get<std::string>()
        << ", " << report.at("findings").size() << " finding(s)\n";

    const auto groups = group_diagnostics(report, view);
    if (view.category || view.severity) {
        std::size_t visible = 0;
        for (const auto& group : groups)
            visible += group.findings.size();
        out << "display: " << visible << '/' << report.at("findings").size();
        if (view.category)
            out << " category=" << *view.category;
        if (view.severity)
            out << " severity=" << *view.severity;
        out << "\n";
    }
    for (const auto& group : groups) {
        out << "  " << group.category << " / " << group.severity << ": "
            << group.findings.size() << " finding(s)\n";
        for (const auto* entry : group.findings) {
            const auto& finding = *entry;
            out << finding.at("file").get<std::string>() << ':'
                << finding.at("line") << ':' << finding.at("column") << ": "
                << finding.at("rule").get<std::string>() << " ["
                << finding.at("authority").get<std::string>() << '/'
                << finding.at("enforcement").get<std::string>() << "] "
                << finding.at("severity").get<std::string>() << " "
                << finding.at("entity_kind").get<std::string>() << " '"
                << finding.at("entity").get<std::string>()
                << "': " << finding.at("message").get<std::string>();
            if (!finding.at("artifact").get<std::string>().empty())
                out << " artifact="
                    << finding.at("artifact").get<std::string>();
            if (!finding.at("referent").is_null())
                out << " referent="
                    << finding.at("referent").get<std::string>();
            if (!finding.at("measurements").empty())
                out << " " << finding.at("measurements").dump();
            if (!finding.at("thresholds").empty())
                out << " targets=" << finding.at("thresholds").dump();
            out << "\n";
        }
    }
    for (const auto& error : report.at("errors"))
        out << "analysis failure: " << error.get<std::string>() << "\n";
    if (profile == "cxx") {
        out << "translation units: " << report.at("files_analyzed")
            << ", functions: " << report.at("total_functions")
            << ", classes: " << report.at("total_classes")
            << ", namespaces: " << report.at("total_namespaces") << "\n";
        for (const auto& source : report.at("sources"))
            out << "  " << source.at("artifact").get<std::string>() << ' '
                << source.at("file").get<std::string>() << " ["
                << source.at("category").get<std::string>() << "]\n";
    } else {
        out << "owned files: " << report.at("files").size() << "\n";
    }
    if (profile == "style") {
        auto ranked = report.at("files");
        std::stable_sort(
            ranked.begin(), ranked.end(),
            [](const json& first, const json& second) {
                return first.value("effective_lines", 0U)
                    > second.value("effective_lines", 0U);
            }
        );
        out << "largest owned files (effective/physical lines; size thresholds "
               "uncalibrated):\n";
        for (std::size_t index = 0;
             index < std::min<std::size_t>(5, ranked.size()); ++index) {
            const auto& file = ranked[index];
            out << "  " << file.at("file").get<std::string>() << ": "
                << file.value("effective_lines", 0U) << '/'
                << file.value("physical_lines", 0U) << "\n";
        }
        out << "referents: " << report.at("referents").size()
            << ", modules: " << report.at("modules").size() << "\n";
    }
}

} // namespace ecosystem
