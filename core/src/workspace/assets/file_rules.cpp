#include "internal.hpp"

#include "workspace/tooling.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ecosystem::asset_detail {
namespace {

    bool report_name(const fs::path& file) {
        const auto name = file.filename().string();
        const auto separator = name.rfind("--");
        if (separator == std::string::npos || separator == 0)
            return false;
        auto suffix = name.substr(separator + 2);
        if (const auto dot = suffix.find('.'); dot != std::string::npos)
            suffix.resize(dot);
        if (suffix == "latest")
            return true;
        const auto digit = [](const char ch) { return ch >= '0' && ch <= '9'; };
        return suffix.size() == 15 && suffix[8] == '-'
            && std::all_of(suffix.begin(), suffix.begin() + 8, digit)
            && std::all_of(suffix.begin() + 9, suffix.end(), digit);
    }

} // namespace

void check_reports(const fs::path& assets) {
    const auto reports = assets / "reports";
    for (const auto& entry : fs::recursive_directory_iterator(reports)) {
        if (!entry.is_regular_file())
            continue;
        const auto name = entry.path().filename().string();
        if (entry.path().parent_path() == reports
            && (name == ".gitkeep" || name == "index.tsv" || name == "readme.md"
            ))
            continue;
        if (!report_name(entry.path()))
            throw std::runtime_error(
                entry.path().string()
                + ": report requires --latest or --YYYYMMDD-HHMMSS before its "
                  "extension"
            );
    }
}

void check_ignore(const fs::path& assets) {
    const auto ignore = assets / ".gitignore";
    if (!fs::exists(ignore))
        return;
    if (fs::is_symlink(ignore))
        throw std::runtime_error(
            ignore.string() + ": invalid asset ignore file"
        );
    std::string error;
    const auto text = read_text_file(ignore, &error);
    if (!error.empty())
        throw std::runtime_error(
            ignore.string() + ": invalid asset ignore file"
        );
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        if (line.front() == '!')
            line.erase(0, 1);
        const bool anchored = !line.empty() && line.front() == '/';
        if (anchored)
            line.erase(0, 1);
        // Git's trailing slash does not anchor a name. Spaces after it are
        // ignored.
        const bool subpath = line.starts_with("dumps/")
            && line.find_first_not_of(' ', 6) != std::string::npos;
        if (!(anchored && (line == "dumps" || line.starts_with("dumps/")))
            && !subpath)
            throw std::runtime_error(
                ignore.string()
                + ": patterns may filter only root dumps/; use /dumps/ or a "
                  "dump subpath"
            );
    }
}

} // namespace ecosystem::asset_detail
