#include "internal.hpp"

#include "workspace/project.hpp"
#include <algorithm>
#include <functional>
#include <regex>
#include <set>
#include <stdexcept>
#include <utility>

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

std::string runtime_asset_cache_pattern() {
    return R"((^|/)(\.cache|\.[^/]*_cache)(/|$))";
}

std::vector<fs::path> runtime_asset_files(
    const fs::path& root, const std::vector<std::string>& categories
) {
    const auto assets = fs::absolute(root).lexically_normal() / "assets";
    if (!fs::exists(assets) && !fs::is_symlink(assets))
        return {};
    if (!fs::is_directory(assets) || fs::is_symlink(assets))
        throw std::runtime_error("invalid runtime asset source: " + assets.string());
    const auto project = fs::canonical(root);
    const std::regex cache_pattern(runtime_asset_cache_pattern());
    const auto cache = [&](const fs::path& path) {
        return std::regex_search(path.generic_string(), cache_pattern);
    };
    const auto resolve = [&](const fs::path& path, const bool directory) {
        const auto real = fs::canonical(path);
        const auto relative = real.lexically_relative(project);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            throw std::runtime_error("runtime asset escapes its project: " + path.string());
        return std::pair { real, cache(directory ? relative : relative.parent_path()) };
    };
    std::vector<fs::path> files;
    std::function<void(const fs::path&, std::set<fs::path>)> walk;
    walk = [&](const fs::path& directory, std::set<fs::path> ancestors) {
        const auto [real, cached] = resolve(directory, true);
        if (cached)
            return;
        if (!ancestors.insert(real).second)
            throw std::runtime_error("cyclic runtime asset directory: " + directory.string());
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.is_directory()) {
                if (!cache(entry.path().filename()))
                    walk(entry.path(), ancestors);
            } else {
                const auto [real_file, file_cached] = resolve(entry.path(), false);
                if (!fs::is_regular_file(real_file))
                    throw std::runtime_error("invalid runtime asset file: " + entry.path().string());
                if (!file_cached)
                    files.push_back(entry.path());
            }
        }
    };
    const auto allowed = runtime_asset_categories();
    for (const auto& category : categories) {
        if (std::find(allowed.begin(), allowed.end(), category) == allowed.end())
            throw std::runtime_error("invalid runtime asset category: " + category);
        const auto directory = assets / category;
        if (fs::is_symlink(directory))
            throw std::runtime_error("runtime asset category uses a symlink: " + directory.string());
        if (!fs::exists(directory))
            continue;
        if (!fs::is_directory(directory))
            throw std::runtime_error("invalid runtime asset category: " + directory.string());
        walk(directory, { fs::canonical(assets) });
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

} // namespace ecosystem
