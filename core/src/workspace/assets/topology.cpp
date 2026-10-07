#include "internal.hpp"

#include "workspace/project.hpp"
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ecosystem {
namespace {

    bool metadata(const fs::directory_entry& entry) {
        const auto name = entry.path().filename().string();
        return entry.is_regular_file()
            && (name == "index.tsv" || name == "readme.md"
                || name == ".gitkeep");
    }

    void check_branches(const fs::path& assets, const std::string& category) {
        const auto directory = assets / category;
        if (!fs::exists(directory))
            return;
        const std::set<std::string> branches { "test", "train", "stress",
                                               "todo" };
        bool data = false;
        for (const auto& entry : fs::directory_iterator(directory)) {
            data = true;
            if (metadata(entry))
                continue;
            const auto name = entry.path().filename().string();
            if (!branches.contains(name) || !entry.is_directory())
                throw std::runtime_error(
                    entry.path().string() + ": invalid asset branch"
                );
            if (category == "dumps"
                && !fs::is_directory(assets / "dataset" / name))
                throw std::runtime_error(
                    entry.path().string()
                    + ": dump branch has no matching dataset branch"
                );
        }
        if (category == "dataset" && data
            && !fs::is_directory(directory / "test"))
            throw std::runtime_error(
                directory.string() + ": nonempty dataset requires test/"
            );
    }

} // namespace

void asset_detail::check_tree(const fs::path& root, const bool indexes) {
    const auto assets = root / "assets";
    if (!fs::exists(fs::symlink_status(assets)))
        return;
    const auto errors = validate_project_paths(root, { "assets" });
    if (!errors.empty())
        throw std::runtime_error(errors.front());
    if (!fs::is_directory(assets) || fs::is_symlink(assets))
        throw std::runtime_error("assets must be a project-owned directory");
    for (const auto& entry : fs::directory_iterator(assets)) {
        const auto name = entry.path().filename().string();
        if ((name == ".gitignore" || name == ".gitkeep")
            && entry.is_regular_file())
            continue;
        if (!asset_detail::known_category(name) || !entry.is_directory()
            || entry.is_symlink())
            throw std::runtime_error(
                entry.path().string()
                + ": unknown asset category or invalid directory"
            );
        if (name == "dataset" || name == "dumps")
            check_branches(assets, name);
        if (name == "reports")
            asset_detail::check_reports(assets);
        if (indexes)
            static_cast<void>(read_asset_index(root, name));
    }
    asset_detail::check_ignore(assets);
}

std::vector<std::string> validate_assets(const fs::path& root) {
    try {
        asset_detail::check_tree(root, true);
        return {};
    } catch (const std::exception& error) {
        return { error.what() };
    }
}

std::vector<std::string> runtime_asset_categories() {
    std::vector<std::string> result;
    for (const auto category : asset_detail::categories)
        if (category != "showcase")
            result.emplace_back(category);
    return result;
}

} // namespace ecosystem
