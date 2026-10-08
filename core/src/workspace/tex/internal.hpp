#pragma once

#include "workspace/assets.hpp"

namespace ecosystem::tex_detail {

struct document {
    std::filesystem::path source;
    std::filesystem::path staged;
    asset_entry entry;
};

void check_path(
    const std::filesystem::path& root, const std::filesystem::path& relative
);
std::vector<document> prepare_documents(
    const std::filesystem::path& root,
    const std::vector<std::filesystem::path>& sources
);
void publish_documents(
    const std::filesystem::path& root, const std::vector<document>& documents
);

} // namespace ecosystem::tex_detail
