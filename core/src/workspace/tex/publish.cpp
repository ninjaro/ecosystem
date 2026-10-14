#include "internal.hpp"

#include "workspace/tooling.hpp"

#include <ranges>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ecosystem::tex_detail {

void publish_documents(
    const fs::path& root, const std::vector<document>& documents
) {
    const auto previous = root / ".ecosystem/tex/previous";
    check_path(root, ".ecosystem/tex/previous");
    if (!fs::create_directory(previous))
        throw std::runtime_error("TeX recovery exists: " + previous.string());

    struct replacement {
        fs::path target;
        fs::path backup;
        bool saved = false;
        bool installed = false;
    };

    std::vector<replacement> replacements;
    try {
        const auto index = root / "assets/showcase/index.tsv";
        check_path(root, "assets/showcase/index.tsv");
        const bool had_index = fs::exists(index);
        if (had_index)
            fs::copy_file(index, previous / "index.tsv");
        json state { { "had_index", had_index }, { "files", json::array() } };
        for (const auto& document : documents) {
            const auto target = root / "assets/showcase" / document.entry.path;
            check_path(root, target.lexically_relative(root));
            if (fs::exists(target) && !fs::is_regular_file(target))
                throw std::runtime_error(
                    "TeX output must be a regular file: " + document.entry.path
                );
            state["files"].push_back({ { "path", document.entry.path },
                                       { "had_file", fs::exists(target) } });
        }
        std::string error;
        if (!write_text_file(
                previous / "state.json", state.dump(2) + "\n", &error
            ))
            throw std::runtime_error(error);
        std::vector<asset_entry> updates;
        for (const auto& document : documents) {
            const auto target = root / "assets/showcase" / document.entry.path;
            const auto backup
                = previous / (std::to_string(replacements.size()) + ".pdf");
            replacements.push_back({ target, backup });
            auto& replacement = replacements.back();
            fs::create_directories(target.parent_path());
            if (fs::exists(target)) {
                fs::rename(target, backup);
                replacement.saved = true;
            }
            fs::rename(document.staged, target);
            replacement.installed = true;
            updates.push_back(document.entry);
        }
        if (!put_asset_entries(root, "showcase", updates, &error))
            throw std::runtime_error(error);
    } catch (const std::exception& failure) {
        std::string rollback;
        for (auto& replacement : std::views::reverse(replacements)) {
            std::error_code error;
            if (replacement.installed)
                fs::remove(replacement.target, error);
            if (!error && replacement.saved)
                fs::rename(replacement.backup, replacement.target, error);
            if (error)
                rollback += "; " + replacement.target.string() + ": "
                    + error.message();
        }
        if (!rollback.empty())
            throw std::runtime_error(
                std::string(failure.what())
                + "; restore .ecosystem/tex/previous" + rollback
            );
        std::error_code error;
        fs::remove_all(previous, error);
        throw;
    }
    std::error_code error;
    fs::remove_all(previous, error);
    if (error)
        throw std::runtime_error(
            "TeX PDFs were registered; unable to remove "
            ".ecosystem/tex/previous: "
            + error.message()
        );
}

} // namespace ecosystem::tex_detail
