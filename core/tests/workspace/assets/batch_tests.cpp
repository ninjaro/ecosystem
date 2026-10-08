#include "../../suite/test_support.hpp"
#include "../assets.hpp"
#include "workspace/assets.hpp"

namespace ecosystem_test_support {

void test_asset_batch_update() {
    temp_dir root;
    for (const auto* name : { "a", "b", "c" })
        write_text(
            root.path() / "assets/showcase" / (std::string(name) + ".pdf"),
            "PDF"
        );
    std::string error;
    ecosystem::asset_entry a { "a", "a.pdf", "pdf", "First", {} };
    ecosystem::asset_entry b { "b", "b.pdf", "pdf", "Second", {} };
    const ecosystem::asset_entry c { "c", "c.pdf", "pdf", "Third", {} };
    require_true(
        ecosystem::put_asset_entries(root.path(), "showcase", { b, a }, &error),
        error
    );
    a.description = "Updated";
    require_true(
        ecosystem::put_asset_entries(root.path(), "showcase", { a, c }, &error),
        error
    );
    const auto entries = ecosystem::read_asset_index(root.path(), "showcase");
    require_true(
        entries.size() == 3 && entries[0].id == "b" && entries[1].id == "a"
            && entries[1].description == "Updated" && entries[2].id == "c",
        "batch replacement preserves order and appends new IDs in supplied "
        "order"
    );
    const auto index = root.path() / "assets/showcase/index.tsv";
    const auto original = read_text(index);
    auto invalid = c;
    invalid.path = "missing.pdf";
    a.description = "Must not publish";
    require_true(
        !ecosystem::put_asset_entries(
            root.path(), "showcase", { a, invalid }, &error
        ) && read_text(index) == original,
        "a later missing file must reject the whole batch"
    );
    invalid.path = "a.pdf";
    require_true(
        !ecosystem::put_asset_entries(
            root.path(), "showcase", { a, invalid }, &error
        ) && read_text(index) == original,
        "batch paths cannot claim another entry's file"
    );
    require_true(
        !ecosystem::put_asset_entries(root.path(), "showcase", { a, a }, &error)
            && read_text(index) == original,
        "duplicate update IDs must not silently win"
    );
    write_text(
        root.path() / "assets/showcase/.index.tsv.pending/keep", "recovery"
    );
    require_true(
        !ecosystem::put_asset_entries(root.path(), "showcase", { a, b }, &error)
            && read_text(index) == original
            && read_text(
                   root.path() / "assets/showcase/.index.tsv.pending/keep"
               ) == "recovery",
        "batch updates preserve retained recovery state"
    );
}

} // namespace ecosystem_test_support
