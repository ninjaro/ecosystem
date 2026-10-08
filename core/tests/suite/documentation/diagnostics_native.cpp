#include "../documentation.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_native_diagnostics(const fs::path& project) {
    auto result = run_engels_cli(project, "report style lib:first --json");
    require_true(result.exit_code == 0, result.output);
    const auto source = project / ".ecosystem/reports/style.json";
    const auto snapshot = read_text(source);
    const auto site = ecosystem::local_sphinx_dir(project) / "html/_manifesto";
    require_contains(
        read_text(site / "index.html"), "No diagnostic snapshot was selected",
        "native Pages must select diagnostic inputs explicitly"
    );
    {
        scoped_env selection(
            "MANIFESTO_SPHINX_DIAGNOSTICS", ".ecosystem/reports/style.json"
        );
        result = run_engels_cli(project, "check sphinx --theme alabaster");
        require_true(result.exit_code == 0, result.output);
        const auto page = read_text(site / "index.html");
        require_true(
            read_text(site / "diagnostics/report.json") == snapshot
                && read_text(source) == snapshot,
            "native Pages must publish the complete analyzer snapshot unchanged"
        );
        require_contains(
            page, "Local diagnostic snapshot",
            "selected snapshot renders through the native theme"
        );
        require_contains(
            page, "lib:first", "Pages preserves selected artifact identity"
        );
        require_contains(
            page, "Raw scope and measurements",
            "Pages includes scope measurements"
        );
        require_contains(
            page, "data-diagnostic-filter",
            "native Pages supplies category/severity controls"
        );
        require_true(
            fs::is_regular_file(site / "diagnostics/filters.js"),
            "native filters must have a copied script"
        );
        auto invalid = json::parse(snapshot);
        invalid["project"] = "other";
        write_text(source, invalid.dump(2));
        result = run_engels_cli(project, "check sphinx --theme alabaster");
        require_true(
            result.exit_code == 5 && read_text(site / "index.html") == page
                && read_text(site / "diagnostics/report.json") == snapshot,
            "invalid selected snapshots must preserve the published site and "
            "raw download"
        );
        require_contains(
            result.output, "project mismatch",
            "snapshot failures must identify the mismatch"
        );
        write_text(source, snapshot);
    }
    result = run_engels_cli(project, "check sphinx --theme alabaster");
    require_true(
        result.exit_code == 0 && !fs::exists(site / "diagnostics"),
        "removing snapshot selection must retire its generated files"
    );
}

} // namespace ecosystem_test_support
