#include "internal.hpp"

#include "workspace/project.hpp"
#include "workspace/tooling.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ecosystem::asset_detail {

bool known_category(const std::string& name) {
    return std::find(categories.begin(), categories.end(), name)
        != categories.end();
}

fs::path asset_path(
    const fs::path& root, const std::string& category, const std::string& name
) {
    if (!known_category(category))
        throw std::runtime_error("unknown asset category: " + category);
    if (name.empty() || name.front() == '/'
        || name.find('\\') != std::string::npos
        || name.find(':') != std::string::npos
        || name.find("//") != std::string::npos)
        throw std::runtime_error("unsafe asset path: " + name);
    const fs::path relative(name);
    for (const auto& part : relative)
        if (part.empty() || part == "." || part == "..")
            throw std::runtime_error("unsafe asset path: " + name);
    auto path = root;
    for (const auto& part : fs::path("assets") / category / relative) {
        path /= part;
        if (fs::is_symlink(fs::symlink_status(path)))
            throw std::runtime_error(
                "asset path uses a symlink: " + path.string()
            );
    }
    return path;
}

void validate_entry(
    const fs::path& root, const std::string& category, const asset_entry& entry,
    const bool check_file
) {
    const auto control = [](unsigned char ch) { return ch < 32 || ch == 127; };
    if (entry.id.empty() || entry.type.empty()
        || std::any_of(entry.id.begin(), entry.id.end(), control)
        || std::any_of(entry.type.begin(), entry.type.end(), control))
        throw std::runtime_error("missing or invalid asset identity");
    const auto file = asset_path(root, category, entry.path);
    if (check_file && !fs::is_regular_file(file))
        throw std::runtime_error("missing asset file: " + file.string());
    std::string extension = file.extension().string();
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }
    );
    const std::vector<std::string> images { ".png", ".jpg",  ".jpeg",
                                            ".gif", ".webp", ".avif" };
    const bool media
        = entry.type == "image" || entry.type == "video" || entry.type == "pdf";
    const bool supported = entry.type == "image"
        ? std::find(images.begin(), images.end(), extension) != images.end()
        : entry.type == "video" ? extension == ".mp4" || extension == ".webm"
        : entry.type == "pdf"   ? extension == ".pdf"
                                : true;
    if (!supported || (category == "showcase" && !media))
        throw std::runtime_error("unsupported asset media: " + entry.path);
    if (entry.path == "index.tsv" || entry.path == "readme.md")
        throw std::runtime_error(
            "asset index cannot select its metadata: " + entry.path
        );
}

namespace {

    std::vector<std::vector<std::string>> parse_rows(const std::string& text) {
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> row;
        std::string field;
        bool quoted = false, closed = false, started = false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char ch = text[i];
            if (ch == '\0')
                throw std::runtime_error("NUL in asset index");
            if (quoted) {
                if (ch == '"') {
                    if (i + 1 < text.size() && text[i + 1] == '"') {
                        field += '"';
                        ++i;
                    } else {
                        quoted = false;
                        closed = true;
                    }
                } else {
                    field += ch;
                }
                continue;
            }
            if (ch == '\t' || ch == '\n' || ch == '\r') {
                row.push_back(field);
                field.clear();
                closed = false;
                started = false;
                if (ch != '\t') {
                    if (ch == '\r' && i + 1 < text.size()
                        && text[i + 1] == '\n')
                        ++i;
                    rows.push_back(row);
                    row.clear();
                }
            } else if (closed) {
                throw std::runtime_error("text after quoted asset field");
            } else if (!started && ch == '"') {
                quoted = true;
                started = true;
            } else {
                field += ch;
                started = true;
            }
        }
        if (quoted)
            throw std::runtime_error("unterminated quoted asset field");
        if (started || closed || !row.empty()) {
            row.push_back(field);
            rows.push_back(row);
        }
        return rows;
    }

} // namespace

std::vector<asset_entry> load_index(
    const fs::path& root, const std::string& category, const bool check_files
) {
    const auto path = asset_path(root, category, "index.tsv");
    if (!fs::exists(path))
        return {};
    if (!fs::is_regular_file(path))
        throw std::runtime_error(
            path.string() + ": index must be a regular file"
        );
    std::string error;
    const auto text = read_text_file(path, &error);
    if (!error.empty())
        throw std::runtime_error(error);
    // The shared JSON serializer validates UTF-8 for the Python presentation
    // reader.
    static_cast<void>(json(text).dump());
    const auto rows = parse_rows(text);
    const std::vector<std::string> columns { "id", "path", "type",
                                             "description", "datetime" };
    if (rows.empty() || rows.front() != columns)
        throw std::runtime_error(
            path.string() + ": invalid asset index columns"
        );
    std::set<std::string> ids, paths;
    std::vector<asset_entry> result;
    for (std::size_t i = 1; i < rows.size(); ++i) {
        const auto& row = rows[i];
        if (row.size() != columns.size())
            throw std::runtime_error(
                path.string() + ": invalid asset row " + std::to_string(i + 1)
            );
        const asset_entry entry { row[0], row[1], row[2], row[3], row[4] };
        if (!ids.insert(entry.id).second || !paths.insert(entry.path).second)
            throw std::runtime_error(
                path.string() + ": duplicate asset identity or path"
            );
        validate_entry(root, category, entry, check_files);
        result.push_back(entry);
    }
    check_readme(root, category);
    return result;
}

void check_readme(const fs::path& root, const std::string& category) {
    const auto readme = asset_path(root, category, "readme.md");
    if (fs::exists(readme)) {
        if (!fs::is_regular_file(readme))
            throw std::runtime_error(
                readme.string() + ": expected a regular file"
            );
        std::string error;
        const auto contents = read_text_file(readme, &error);
        if (!error.empty() || contents.find("<carousel/>") == std::string::npos)
            throw std::runtime_error(
                readme.string() + ": indexed assets require <carousel/>"
            );
    }
}

} // namespace ecosystem::asset_detail

namespace ecosystem {

std::vector<asset_entry>
read_asset_index(const fs::path& root, const std::string& category) {
    return asset_detail::load_index(root, category, true);
}

} // namespace ecosystem
