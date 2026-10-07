#include "../../suite/test_support.hpp"
#include "../assets.hpp"
#include "workspace/assets.hpp"

namespace ecosystem_test_support {

void test_asset_categories() {
    temp_dir root;
    require_true(
        ecosystem::validate_assets(root.path()).empty(), "assets are optional"
    );
    for (const auto* category : { "brand", "showcase", "reports", "dataset",
                                  "dumps", "sets", "templates" })
        fs::create_directories(root.path() / "assets" / category);
    require_true(
        ecosystem::validate_assets(root.path()).empty(),
        "fixed categories are valid"
    );
    write_text(root.path() / "assets/stray.txt", "not categorized");
    require_contains(
        join_lines(ecosystem::validate_assets(root.path())), "stray.txt",
        "root dumping must be diagnosed"
    );
    fs::remove(root.path() / "assets/stray.txt");
    fs::create_directory(root.path() / "assets/arbitrary");
    require_contains(
        join_lines(ecosystem::validate_assets(root.path())), "arbitrary",
        "unknown categories must fail"
    );
    fs::remove(root.path() / "assets/arbitrary");
    fs::remove(root.path() / "assets/sets");
    write_text(root.path() / "assets/sets", "not a directory");
    require_true(
        !ecosystem::validate_assets(root.path()).empty(),
        "category files must fail"
    );
}

void test_asset_branches() {
    temp_dir root;
    write_text(root.path() / "assets/dataset/train/nested/input.ex", "input");
    require_contains(
        join_lines(ecosystem::validate_assets(root.path())), "requires test/",
        "nonempty datasets need tests"
    );
    fs::create_directory(root.path() / "assets/dataset/test");
    write_text(root.path() / "assets/dumps/train/deeper/output.dat", "output");
    require_true(
        ecosystem::validate_assets(root.path()).empty(),
        "nested data and mirrored dumps are unrestricted"
    );
    fs::create_directory(root.path() / "assets/dumps/stress");
    require_contains(
        join_lines(ecosystem::validate_assets(root.path())), "matching dataset",
        "dumps cannot invent a branch"
    );
    fs::create_directory(root.path() / "assets/dataset/stress");
    require_true(
        ecosystem::validate_assets(root.path()).empty(),
        "dump branches need not mirror every dataset branch"
    );
    write_text(root.path() / "assets/dataset/loose.ex", "input");
    require_contains(
        join_lines(ecosystem::validate_assets(root.path())), "loose.ex",
        "dataset inputs belong below a branch"
    );
    fs::remove(root.path() / "assets/dataset/loose.ex");
    fs::create_directory(root.path() / "assets/dataset/custom");
    require_contains(
        join_lines(ecosystem::validate_assets(root.path())), "custom",
        "branch names are fixed"
    );
}

void test_asset_repo_gate() {
    temp_dir root;
    write_sample_build_project(root.path(), "asset_repo");
    write_text(root.path() / "assets/sets/config.json", "{}");
    auto result = run_marx_cli(root.path(), "sync");
    require_true(result.exit_code == 0, result.output);
    result = run_engels_cli(root.path(), "check repo");
    require_true(result.exit_code == 0, result.output);
    const auto manifest = read_text(root.path() / "manifest.json");
    write_text(root.path() / "assets/loose.txt", "stray");
    result = run_engels_cli(root.path(), "check repo");
    require_true(
        result.exit_code == 5,
        "asset violations must fail required repository checks"
    );
    require_contains(
        result.output, "loose.txt", "report the invalid asset path"
    );
    require_true(
        read_text(root.path() / "manifest.json") == manifest
            && fs::is_regular_file(root.path() / "assets/loose.txt"),
        "checks must not repair authored files"
    );
}

} // namespace ecosystem_test_support
