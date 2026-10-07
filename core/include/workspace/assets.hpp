#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ecosystem {

struct asset_entry {
    std::string id;
    std::string path;
    std::string type;
    std::string description;
    std::string datetime;
};

/** Read ordered entries. Missing indexes are empty; invalid inputs throw. */
std::vector<asset_entry> read_asset_index(
    const std::filesystem::path& root, const std::string& category
);
std::vector<std::string> validate_assets(const std::filesystem::path& root);
std::vector<std::string> runtime_asset_categories();
/** Replace an ID in place, or insert it before another ID (append by default).
 */
bool put_asset_entry(
    const std::filesystem::path& root, const std::string& category,
    const asset_entry& entry, const std::string& before, std::string* error
);
/** Remove index ownership only; retain the media file. */
bool remove_asset_entry(
    const std::filesystem::path& root, const std::string& category,
    const std::string& id, std::string* error
);

} // namespace ecosystem
