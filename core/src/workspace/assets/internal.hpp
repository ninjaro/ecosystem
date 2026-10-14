#pragma once

#include "workspace/assets.hpp"

#include <array>
#include <string_view>

namespace ecosystem::asset_detail {

inline constexpr std::array<std::string_view, 8> categories {
    "brand", "showcase", "reports",   "dataset",
    "dumps", "sets",     "templates", "docs"
};

bool known_category(const std::string& name);
std::filesystem::path asset_path(
    const std::filesystem::path& root, const std::string& category,
    const std::string& name
);
void validate_entry(
    const std::filesystem::path& root, const std::string& category,
    const asset_entry& entry, bool check_file = true
);
std::vector<asset_entry> load_index(
    const std::filesystem::path& root, const std::string& category,
    bool check_files
);
void write_index(
    const std::filesystem::path& root, const std::string& category,
    const std::vector<asset_entry>& entries
);
void check_tree(const std::filesystem::path& root, bool indexes);
void check_ignore(const std::filesystem::path& assets);
void check_reports(const std::filesystem::path& assets);
void check_readme(
    const std::filesystem::path& root, const std::string& category
);

} // namespace ecosystem::asset_detail
