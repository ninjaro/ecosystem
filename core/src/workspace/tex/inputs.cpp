#include "internal.hpp"

#include "../assets/internal.hpp"
#include "workspace/project.hpp"
#include "workspace/tex.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ecosystem {

void tex_detail::check_path(const fs::path& root, const fs::path& relative) {
    const auto errors = validate_project_paths(root, { relative });
    if (!errors.empty())
        throw std::runtime_error(errors.front());
    auto path = root;
    for (const auto& part : relative) {
        path /= part;
        if (fs::is_symlink(fs::symlink_status(path)))
            throw std::runtime_error(
                "TeX path uses a symlink: " + path.string()
            );
        if (fs::exists(path) && fs::is_regular_file(path)
            && fs::hard_link_count(path) != 1)
            throw std::runtime_error(
                "TeX path uses a hard link: " + path.string()
            );
    }
}

std::vector<fs::path> tex_sources(const fs::path& root) {
    tex_detail::check_path(root, "tex");
    const auto directory = root / "tex";
    if (!fs::exists(directory))
        return {};
    if (!fs::is_directory(directory))
        throw std::runtime_error("tex must be a project-owned directory");
    std::vector<fs::path> sources;
    for (const auto& file : fs::recursive_directory_iterator(directory)) {
        tex_detail::check_path(root, file.path().lexically_relative(root));
        if (!file.is_directory() && !file.is_regular_file())
            throw std::runtime_error(
                "TeX input must be a regular file or directory: "
                + file.path().string()
            );
        if (file.path().parent_path() != directory
            || file.path().extension() != ".tex")
            continue;
        if (!file.is_regular_file())
            throw std::runtime_error(
                "TeX source must be a regular file: " + file.path().string()
            );
        sources.push_back(file.path());
    }
    std::sort(sources.begin(), sources.end());
    return sources;
}

std::vector<tex_detail::document> tex_detail::prepare_documents(
    const fs::path& root, const std::vector<fs::path>& sources
) {
    for (const auto* name :
         { ".ecosystem/tex/build.log", ".ecosystem/tex/pending",
           ".ecosystem/tex/previous" })
        check_path(root, name);
    for (const auto* name :
         { ".ecosystem/tex/pending", ".ecosystem/tex/previous",
           "assets/showcase/.index.tsv.pending" })
        if (fs::exists(root / name))
            throw std::runtime_error(
                "TeX recovery exists: " + std::string(name)
            );
    asset_detail::check_tree(root, false);
    asset_detail::check_readme(root, "showcase");
    for (const auto category : asset_detail::categories)
        if (category != "showcase")
            static_cast<void>(read_asset_index(root, std::string(category)));
    const auto entries = asset_detail::load_index(root, "showcase", false);
    check_path(root, "assets/showcase/index.tsv");
    std::set<std::string> generated;
    std::vector<document> documents;
    for (const auto& source : sources) {
        const auto stem = source.stem().string();
        asset_entry entry {
            "tex:" + stem, "tex/" + stem + ".pdf", "pdf", stem, {}
        };
        asset_detail::validate_entry(root, "showcase", entry, false);
        const auto target = fs::path("assets/showcase") / entry.path;
        check_path(root, target);
        const auto found = std::find_if(
            entries.begin(), entries.end(),
            [&](const auto& item) {
                return item.id == entry.id || item.path == entry.path;
            }
        );
        if (found != entries.end()) {
            if (found->id != entry.id || found->path != entry.path
                || found->type != "pdf")
                throw std::runtime_error(
                    "TeX ownership conflicts with asset: " + entry.path
                );
            entry = *found;
        } else if (fs::exists(root / target)) {
            throw std::runtime_error(
                "TeX output is not controller-owned: " + entry.path
            );
        }
        if (fs::exists(root / target) && !fs::is_regular_file(root / target))
            throw std::runtime_error(
                "TeX output must be a regular file: " + entry.path
            );
        generated.insert(entry.id);
        documents.push_back({ source, {}, entry });
    }
    for (const auto& entry : entries)
        if (!generated.contains(entry.id))
            asset_detail::validate_entry(root, "showcase", entry);
    return documents;
}

} // namespace ecosystem
