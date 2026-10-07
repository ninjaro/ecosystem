#include "../../suite/test_support.hpp"
#include "../assets.hpp"
#include "workspace/assets.hpp"

namespace ecosystem_test_support {

void test_asset_controller_order() {
    temp_dir root;
    write_text(root.path() / "assets/showcase/a.png", "image a");
    write_text(root.path() / "assets/showcase/b.pdf", "document b");
    std::string error;
    ecosystem::asset_entry a { "a", "a.png", "image", "First", "2026-10-07" };
    ecosystem::asset_entry b {
        "b", "b.pdf", "pdf", "Quote \"here\"\tand\nline", {}
    };
    require_true(
        ecosystem::put_asset_entry(root.path(), "showcase", a, {}, &error),
        error
    );
    require_true(
        ecosystem::put_asset_entry(root.path(), "showcase", b, "a", &error),
        error
    );
    auto entries = ecosystem::read_asset_index(root.path(), "showcase");
    require_true(
        entries.size() == 2 && entries[0].id == "b" && entries[1].id == "a",
        "insertion before an ID controls order"
    );
    a.description = "Changed";
    require_true(
        ecosystem::put_asset_entry(root.path(), "showcase", a, {}, &error),
        error
    );
    entries = ecosystem::read_asset_index(root.path(), "showcase");
    require_true(
        entries[0].id == "b" && entries[1].description == "Changed",
        "replacement preserves position"
    );
    const auto csv = ecosystem::capture_command_result(
        { "python3", "-c",
          "import csv,sys; rows=list(csv.DictReader(open(sys.argv[1]), "
          "delimiter='\\t')); assert [r['id'] for r in rows]==['b','a']; "
          "assert rows[0]['description']=='Quote \"here\"\\tand\\nline'",
          (root.path() / "assets/showcase/index.tsv").string() },
        root.path()
    );
    require_true(
        csv.exit_code == 0,
        "generated TSV must round trip through the presentation CSV reader: "
            + csv.output
    );
    require_true(
        ecosystem::put_asset_entry(root.path(), "showcase", a, "b", &error),
        error
    );
    entries = ecosystem::read_asset_index(root.path(), "showcase");
    require_true(entries[0].id == "a", "existing IDs can be reordered");
    fs::remove(root.path() / "assets/showcase/a.png");
    require_true(
        ecosystem::remove_asset_entry(root.path(), "showcase", "a", &error),
        error
    );
    require_true(
        ecosystem::remove_asset_entry(root.path(), "showcase", "b", &error),
        error
    );
    require_true(
        fs::is_regular_file(root.path() / "assets/showcase/b.pdf")
            && ecosystem::read_asset_index(root.path(), "showcase").empty(),
        "removal releases index ownership without deleting media"
    );
    require_true(
        !fs::exists(root.path() / "assets/showcase/.index.tsv.pending"),
        "successful updates leave no recovery directory"
    );
}

void test_asset_controller_errors() {
    temp_dir root;
    write_text(root.path() / "assets/showcase/a.png", "image");
    std::string error;
    const ecosystem::asset_entry a { "a", "a.png", "image", "Label", {} };
    require_true(
        ecosystem::put_asset_entry(root.path(), "showcase", a, {}, &error),
        error
    );
    const auto index = root.path() / "assets/showcase/index.tsv";
    const auto original = read_text(index);
    auto wrong = a;
    wrong.path = "missing.png";
    require_true(
        !ecosystem::put_asset_entry(root.path(), "showcase", wrong, {}, &error),
        "absent media cannot replace an entry"
    );
    require_true(
        !ecosystem::put_asset_entry(
            root.path(), "showcase", a, "absent", &error
        ),
        "unknown order target must fail"
    );
    require_true(
        !ecosystem::remove_asset_entry(
            root.path(), "showcase", "absent", &error
        ),
        "unknown removal must fail"
    );
    wrong = a;
    wrong.id = "other";
    require_true(
        !ecosystem::put_asset_entry(root.path(), "showcase", wrong, {}, &error),
        "different IDs cannot claim the same path"
    );
    wrong = a;
    wrong.description = std::string("invalid\0metadata", 16);
    require_true(
        !ecosystem::put_asset_entry(root.path(), "showcase", wrong, {}, &error),
        "metadata must remain text"
    );
    wrong.description = std::string(1, static_cast<char>(0xff));
    require_true(
        !ecosystem::put_asset_entry(root.path(), "showcase", wrong, {}, &error),
        "metadata must be valid UTF-8 for presentation"
    );
    require_true(
        read_text(index) == original, "failed requests preserve the last index"
    );
    write_text(
        root.path() / "assets/showcase/.index.tsv.pending/keep", "recovery"
    );
    require_true(
        !ecosystem::put_asset_entry(root.path(), "showcase", a, {}, &error),
        "previous recovery state must not be overwritten"
    );
    require_contains(error, "recovery", "explain retained recovery state");
    require_true(
        read_text(root.path() / "assets/showcase/.index.tsv.pending/keep")
                == "recovery"
            && read_text(index) == original,
        "failure preserves recovery and published data"
    );
    fs::remove_all(root.path() / "assets/showcase/.index.tsv.pending");
    write_text(index, "malformed\n");
    require_true(
        !ecosystem::put_asset_entry(root.path(), "showcase", a, {}, &error)
            && read_text(index) == "malformed\n",
        "invalid existing indexes must not be silently reconstructed"
    );
    fs::remove(index);
    write_text(
        root.path() / "assets/showcase/readme.md", "Missing carousel marker"
    );
    require_true(
        !ecosystem::put_asset_entry(root.path(), "showcase", a, {}, &error)
            && !fs::exists(index),
        "a first index must validate its authored readme before writing"
    );
}

void test_asset_controller_aliases() {
    for (const bool hard_link : { false, true }) {
        temp_dir root;
        write_text(root.path() / "assets/showcase/a.png", "image");
        const auto retained = root.path() / "retained.tsv";
        const std::string original = "id\tpath\ttype\tdescription\tdatetime\na"
                                     "\ta.png\timage\tKeep\t\n";
        write_text(retained, original);
        const auto index = root.path() / "assets/showcase/index.tsv";
        if (hard_link)
            fs::create_hard_link(retained, index);
        else
            fs::create_symlink(retained, index);
        std::string error;
        require_true(
            !ecosystem::remove_asset_entry(
                root.path(), "showcase", "a", &error
            ),
            "aliased indexes must be rejected before replacement"
        );
        require_true(
            read_text(retained) == original
                && !fs::exists(
                    root.path() / "assets/showcase/.index.tsv.pending"
                ),
            "aliases must not mutate their target or stage an update"
        );
    }
}

void test_asset_controller_cli() {
    temp_dir root;
    write_sample_build_project(root.path(), "asset_controller");
    write_text(root.path() / "assets/showcase/a.png", "image");
    auto result = run_marx_cli(
        root.path(),
        "mutate assets put showcase a a.png image --description 'CLI image' "
        "--datetime 2026-10-07"
    );
    require_true(result.exit_code == 0, result.output);
    const auto entries = ecosystem::read_asset_index(root.path(), "showcase");
    require_true(
        entries.size() == 1 && entries[0].description == "CLI image"
            && entries[0].datetime == "2026-10-07",
        "Marx owns explicit asset metadata"
    );
    result = run_engels_cli(root.path(), "mutate assets remove showcase a");
    require_true(result.exit_code == 2, "Engels cannot mutate index ownership");
    require_contains(
        result.output, "available via marx",
        "redirect asset mutations to their actor"
    );
    const auto original = read_text(root.path() / "assets/showcase/index.tsv");
    for (const auto* request :
         { "mutate assets put", "mutate assets remove showcase",
           "mutate assets put showcase a a.png image --unknown x",
           "mutate assets put showcase a a.png image --description x "
           "--description "
           "y" }) {
        result = run_marx_cli(root.path(), request);
        require_true(
            result.exit_code == 2
                && read_text(root.path() / "assets/showcase/index.tsv")
                    == original,
            "invalid CLI requests cannot change index state"
        );
    }
    result = run_marx_cli(root.path(), "mutate assets remove showcase a");
    require_true(
        result.exit_code == 0
            && fs::is_regular_file(root.path() / "assets/showcase/a.png"),
        result.output
    );
    fs::remove(root.path() / "manifest.json");
    result
        = run_marx_cli(root.path(), "mutate assets put showcase a a.png image");
    require_true(
        result.exit_code == 2
            && ecosystem::read_asset_index(root.path(), "showcase").empty(),
        "asset mutations require a valid project manifest"
    );
}

} // namespace ecosystem_test_support
