#include "../../suite/test_support.hpp"
#include "../assets.hpp"
#include "workspace/assets.hpp"

namespace ecosystem_test_support {

void test_asset_index_order() {
    temp_dir root;
    write_text(root.path() / "assets/showcase/b #.PNG", "image");
    write_text(root.path() / "assets/showcase/a.pdf", "pdf");
    write_text(
        root.path() / "assets/showcase/index.tsv",
        "id\tpath\ttype\tdescription\tdatetime\r\n"
        "b\tb #.PNG\timage\t\"Quote \"\"here\"\"\tand\nline\"\t2026-10-07\r\n"
        "a\ta.pdf\tpdf\tSecond\t\r\n"
    );
    const auto entries = ecosystem::read_asset_index(root.path(), "showcase");
    require_true(
        entries.size() == 2 && entries[0].id == "b" && entries[1].id == "a",
        "index order is authored through the controller"
    );
    require_true(
        entries[0].description == "Quote \"here\"\tand\nline"
            && entries[1].datetime.empty(),
        "quoted metadata and empty values are preserved"
    );
    require_true(
        ecosystem::validate_assets(root.path()).empty(),
        "missing readme uses the virtual carousel"
    );
    write_text(
        root.path() / "assets/showcase/readme.md", "# Media\n<carousel/>\n"
    );
    require_true(
        ecosystem::validate_assets(root.path()).empty(),
        "an authored carousel marker is valid"
    );
    write_text(root.path() / "assets/dataset/test/input.ex", "input");
    write_text(
        root.path() / "assets/dataset/index.tsv",
        "id\tpath\ttype\tdescription\tdatetime\ninput\ttest/"
        "input.ex\tdata\tInput\t\n"
    );
    require_true(
        ecosystem::validate_assets(root.path()).empty(),
        "dataset indexes can name ordinary runtime data"
    );
}

void test_asset_index_errors() {
    const std::string header = "id\tpath\ttype\tdescription\tdatetime\n";
    for (const auto& text : std::vector<std::string> {
             "", "wrong\tcolumns\n", header + "one\ta.png\timage\tLabel\n",
             header + "one\ta.png\timage\t\"unfinished\t\n",
             header + "one\ta.png\timage\t\"quoted\"extra\t\n",
             header + "\ta.png\timage\tLabel\t\n",
             header + "one\tmissing.png\timage\tLabel\t\n",
             header + "one\t../a.png\timage\tLabel\t\n",
             header + "one\t/a.png\timage\tLabel\t\n",
             header + "one\tnested//a.png\timage\tLabel\t\n",
             header + "one\ta.png\tpdf\tLabel\t\n",
             header + "one\ta.png\tdata\tLabel\t\n",
             header
                 + "one\ta.png\timage\tFirst\t\none\tb.png\timage\tSecond\t\n",
             header
                 + "one\ta.png\timage\tFirst\t\ntwo\ta."
                   "png\timage\tSecond\t\n" }) {
        temp_dir root;
        write_text(root.path() / "assets/showcase/a.png", "image");
        write_text(root.path() / "assets/showcase/b.png", "image");
        write_text(root.path() / "assets/showcase/index.tsv", text);
        require_true(
            !ecosystem::validate_assets(root.path()).empty(),
            "invalid index must fail: " + text
        );
        require_true(
            read_text(root.path() / "assets/showcase/index.tsv") == text,
            "validation must preserve invalid input"
        );
    }
    temp_dir root;
    write_text(root.path() / "assets/showcase/a.png", "image");
    write_text(
        root.path() / "assets/showcase/index.tsv",
        header + "one\ta.png\timage\tLabel\t\n"
    );
    write_text(root.path() / "assets/showcase/readme.md", "Missing marker");
    require_contains(
        join_lines(ecosystem::validate_assets(root.path())), "<carousel/>",
        "indexed readmes need a carousel"
    );
    fs::remove(root.path() / "assets/showcase/readme.md");
    fs::rename(
        root.path() / "assets/showcase/a.png",
        root.path() / "assets/showcase/original.png"
    );
    fs::create_symlink("original.png", root.path() / "assets/showcase/a.png");
    require_contains(
        join_lines(ecosystem::validate_assets(root.path())), "symlink",
        "indexed media must not alias another input"
    );
}

} // namespace ecosystem_test_support
