#include "internal.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ecosystem {

bool put_asset_entries(
    const fs::path& root, const std::string& category,
    const std::vector<asset_entry>& updates, std::string* error
) {
    error->clear();
    try {
        auto entries = asset_detail::load_index(root, category, false);
        std::set<std::string> ids;
        for (const auto& entry : updates) {
            if (!ids.insert(entry.id).second)
                throw std::runtime_error(
                    "duplicate asset update ID: " + entry.id
                );
            const auto position = std::find_if(
                entries.begin(), entries.end(),
                [&](const auto& item) { return item.id == entry.id; }
            );
            if (position == entries.end())
                entries.push_back(entry);
            else
                *position = entry;
        }
        asset_detail::write_index(root, category, entries);
        return true;
    } catch (const std::exception& failure) {
        *error = failure.what();
        return false;
    }
}

bool put_asset_entry(
    const fs::path& root, const std::string& category, const asset_entry& entry,
    const std::string& before, std::string* error
) {
    error->clear();
    try {
        auto entries = asset_detail::load_index(root, category, false);
        const auto position = std::find_if(
            entries.begin(), entries.end(),
            [&](const auto& item) { return item.id == entry.id; }
        );
        if (before.empty() || before == entry.id) {
            if (position == entries.end()) {
                if (before == entry.id)
                    throw std::runtime_error(
                        "asset cannot precede a missing ID: " + before
                    );
                entries.push_back(entry);
            } else {
                *position = entry;
            }
        } else {
            if (position != entries.end())
                entries.erase(position);
            const auto target = std::find_if(
                entries.begin(), entries.end(),
                [&](const auto& item) { return item.id == before; }
            );
            if (target == entries.end())
                throw std::runtime_error(
                    "unknown asset order target: " + before
                );
            entries.insert(target, entry);
        }
        asset_detail::write_index(root, category, entries);
        return true;
    } catch (const std::exception& failure) {
        *error = failure.what();
        return false;
    }
}

bool remove_asset_entry(
    const fs::path& root, const std::string& category, const std::string& id,
    std::string* error
) {
    error->clear();
    try {
        auto entries = asset_detail::load_index(root, category, false);
        const auto position = std::find_if(
            entries.begin(), entries.end(),
            [&](const auto& entry) { return entry.id == id; }
        );
        if (position == entries.end())
            throw std::runtime_error("unknown asset ID: " + id);
        entries.erase(position);
        asset_detail::write_index(root, category, entries);
        return true;
    } catch (const std::exception& failure) {
        *error = failure.what();
        return false;
    }
}

} // namespace ecosystem
