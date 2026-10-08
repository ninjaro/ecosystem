#include "internal.hpp"

#include "workspace/tooling.hpp"

#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ecosystem {
namespace {

    std::string tsv_field(const std::string& value) {
        if (value.find('\0') != std::string::npos)
            throw std::runtime_error("NUL in asset metadata");
        if (value.find_first_of("\t\r\n\"") == std::string::npos)
            return value;
        std::string quoted = "\"";
        for (const char ch : value) {
            if (ch == '"')
                quoted += '"';
            quoted += ch;
        }
        return quoted + '"';
    }

} // namespace

void asset_detail::write_index(
    const fs::path& root, const std::string& category,
    const std::vector<asset_entry>& entries
) {
    using namespace asset_detail;
    const auto index = asset_path(root, category, "index.tsv");
    const auto pending = asset_path(root, category, ".index.tsv.pending");
    if (fs::exists(index) && fs::hard_link_count(index) != 1)
        throw std::runtime_error(
            "asset index uses a hard link: " + index.string()
        );
    if (fs::exists(pending))
        throw std::runtime_error(
            "asset index recovery exists: " + pending.string()
        );
    check_tree(root, false);
    check_readme(root, category);
    for (const auto other : categories)
        if (other != category)
            static_cast<void>(read_asset_index(root, std::string(other)));
    std::set<std::string> ids, paths;
    std::string text = "id\tpath\ttype\tdescription\tdatetime\n";
    for (const auto& entry : entries) {
        validate_entry(root, category, entry);
        if (!ids.insert(entry.id).second || !paths.insert(entry.path).second)
            throw std::runtime_error("duplicate asset identity or path");
        text += tsv_field(entry.id) + '\t' + tsv_field(entry.path) + '\t'
            + tsv_field(entry.type) + '\t' + tsv_field(entry.description) + '\t'
            + tsv_field(entry.datetime) + '\n';
    }
    static_cast<void>(json(text).dump());
    // The directory provides an exclusive, project-owned candidate without
    // overwriting a previous writer's recovery state.
    if (!fs::create_directory(pending))
        throw std::runtime_error(
            "asset index recovery exists: " + pending.string()
        );
    try {
        std::string error;
        if (!write_text_file(pending / "index.tsv", text, &error))
            throw std::runtime_error(error);
        fs::rename(pending / "index.tsv", index);
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(pending, ignored);
        throw;
    }
    std::error_code ignored;
    fs::remove(pending, ignored);
}

} // namespace ecosystem
