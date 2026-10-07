#include "../../suite/test_support.hpp"
#include "../assets.hpp"
#include "workspace/assets.hpp"

namespace ecosystem_test_support {

void test_asset_ignore_rules() {
    temp_dir root;
    const auto ignore = root.path() / "assets/.gitignore";
    write_text(root.path() / "assets/dataset/test/input.dat", "input");
    write_text(root.path() / "assets/dumps/test/output.dat", "output");
    write_text(root.path() / "assets/sets/dumps/settings.dat", "runtime");
    require_true(
        ecosystem::capture_command_result({ "git", "init", "-q" }, root.path())
                .exit_code
            == 0,
        "ignore fixture must have a Git repository"
    );
    const auto ignored = [&](const std::string& path) {
        const auto result = ecosystem::capture_command_result(
            { "git", "check-ignore", "--no-index", "--quiet", path },
            root.path()
        );
        require_true(result.exit_code <= 1, result.output);
        return result.exit_code == 0;
    };
    for (const auto* rule : { "dumps", "dumps/", "dumps/   " }) {
        write_text(ignore, std::string(rule) + "\n");
        require_true(
            ignored("assets/sets/dumps/settings.dat"),
            "Git applies unanchored dump rules to nested runtime directories"
        );
        require_true(
            !ecosystem::validate_assets(root.path()).empty(),
            "unanchored root dump rules must fail"
        );
    }
    for (const auto* rule :
         { "/dumps", "/dumps/", "dumps/test/*", "/dumps/   " }) {
        write_text(ignore, std::string(rule) + "\n");
        require_true(
            ecosystem::validate_assets(root.path()).empty()
                && ignored("assets/dumps/test/output.dat")
                && !ignored("assets/sets/dumps/settings.dat"),
            "accepted filters must affect only root dumps"
        );
    }
    write_text(
        ignore,
        "# retained dumps\r\n/dumps/\r\n!dumps/test/example.dat\r\n"
        "dumps/train/*.bin\r\n"
    );
    require_true(
        ecosystem::validate_assets(root.path()).empty(),
        "dump filters may select subsets"
    );
    for (const auto* rule :
         { "*", "dataset/", "!brand/logo.svg", "../outside", "!dumps/", " " }) {
        write_text(ignore, std::string(rule) + "\n");
        require_true(
            !ecosystem::validate_assets(root.path()).empty(),
            "only root dump filters are allowed"
        );
    }
    write_text(ignore, "\n");
    const auto original = read_text(ignore);
    require_true(
        ecosystem::validate_assets(root.path()).empty()
            && read_text(ignore) == original,
        "empty lines are allowed and validation must remain read only"
    );
}

void test_asset_report_names() {
    temp_dir root;
    const auto reports = root.path() / "assets/reports";
    for (const auto* name : { ".gitkeep", "run--latest.txt", "coverage--latest",
                              "nested/run--20261007-091213.json",
                              "archive--20261007-091213.tar.gz" })
        write_text(reports / name, "retained report");
    write_text(
        reports / "index.tsv", "id\tpath\ttype\tdescription\tdatetime\n"
    );
    write_text(reports / "readme.md", "Report selection\n<carousel/>\n");
    write_text(root.path() / "assets/sets/reports/settings.json", "runtime");
    require_true(
        ecosystem::validate_assets(root.path()).empty(),
        "report suffixes and category metadata are valid"
    );
    const auto index = read_text(reports / "index.tsv");
    for (const auto* name :
         { "run.txt", "run--latest-copy.txt", "run--2026107-091213.json",
           "run--20261007-09121.json", "run--20261007-09x213.json",
           "nested/readme.md" }) {
        const auto file = reports / name;
        write_text(file, "invalid retained name");
        require_contains(
            join_lines(ecosystem::validate_assets(root.path())), name,
            "retained reports require a latest or timestamp suffix"
        );
        require_true(
            read_text(file) == "invalid retained name",
            "validation must preserve retained reports"
        );
        std::string error;
        require_true(
            !ecosystem::put_asset_entry(
                root.path(), "reports",
                { "current", "run--latest.txt", "text", "Current report", {} },
                {}, &error
            ) && read_text(reports / "index.tsv") == index,
            "invalid report ownership must prevent index mutation"
        );
        fs::remove(file);
    }
    require_true(
        ecosystem::validate_assets(root.path()).empty(),
        "removing invalid report names restores validation"
    );
}

} // namespace ecosystem_test_support
