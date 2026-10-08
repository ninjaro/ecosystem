#include "analysis/diagnostic_view.hpp"

namespace ecosystem {
namespace {

    // Encode Markdown punctuation as well as HTML so authored diagnostics
    // cannot create links, headings, fences or active markup in a CI summary.
    std::string markdown_text(const std::string& text) {
        std::string result;
        for (const char value : text) {
            const auto character = static_cast<unsigned char>(value);
            if (character < 128 && !(character >= 'a' && character <= 'z')
                && !(character >= 'A' && character <= 'Z')
                && !(character >= '0' && character <= '9') && character != ' ')
                result += "&#" + std::to_string(character) + ";";
            else
                result += static_cast<char>(character);
        }
        return result;
    }

    void field(std::ostream& out, const std::string& label, const json& value) {
        out << "- " << label << ": <code>"
            << markdown_text(
                   value.is_string() ? value.get<std::string>() : value.dump()
               )
            << "</code>\n";
    }

} // namespace

void render_diagnostic_markdown(
    const json& report, std::ostream& out, const diagnostic_view& view
) {
    if (report.contains("projects")) {
        out << "## Workspace "
            << markdown_text(report.at("workspace_root").get<std::string>())
            << "\n\n";
        field(out, "Status", report.at("status"));
        if (report.contains("selection"))
            field(out, "Selection", report.at("selection"));
        out << '\n';
        for (const auto& project : report.at("projects"))
            render_diagnostic_markdown(project, out, view);
        return;
    }
    out << "## " << markdown_text(report.at("project").get<std::string>())
        << " — " << markdown_text(report.at("profile").get<std::string>())
        << "\n\n";
    field(out, "Status", report.at("status"));
    for (const auto* key : { "root", "artifact" })
        if (report.contains(key))
            field(out, key, report.at(key));
    out << "- Findings: " << report.at("findings").size() << "\n";
    const auto groups = group_diagnostics(report, view);
    if (view.category)
        field(out, "Category filter", *view.category);
    if (view.severity)
        field(out, "Severity filter", *view.severity);
    std::size_t visible = 0;
    for (const auto& group : groups)
        visible += group.findings.size();
    out << "- Displayed: " << visible << "\n\n";
    for (const auto& error : report.at("errors"))
        out << "**Analysis failure:** "
            << markdown_text(error.get<std::string>()) << "\n\n";
    for (const auto& group : groups) {
        out << "### " << markdown_text(group.category) << " / "
            << markdown_text(group.severity) << " (" << group.findings.size()
            << ")\n\n";
        for (const auto* finding : group.findings) {
            out << "#### "
                << markdown_text(finding->at("rule").get<std::string>())
                << "\n\n"
                << markdown_text(finding->at("message").get<std::string>())
                << "\n\n";
            for (const auto* key :
                 { "file", "line", "column", "artifact", "entity_kind",
                   "entity", "referent", "authority", "enforcement",
                   "measurements", "thresholds" })
                field(out, key, finding->at(key));
            out << '\n';
        }
    }
    auto measurements = report;
    measurements.erase("findings");
    out << "<details><summary>Raw scope and measurements</summary>\n\n<pre>"
        << markdown_text(measurements.dump(2)) << "</pre>\n\n</details>\n\n";
}

} // namespace ecosystem
