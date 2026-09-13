#pragma once

#include "analysis/personal.hpp"
#include "clang_internal.hpp"

#include <map>
#include <set>

namespace ecosystem::personal_support {

namespace fs = std::filesystem;

struct source_file {
    fs::path path;
    fs::path relative;
    fs::path owner_root;
    std::string artifact;
    std::string contents;
    bool test = false;
    bool entry = false;
    std::set<unsigned> effective_lines {};
    unsigned physical_lines = 0;
};

struct inspection {
    personal_report& report;
    std::map<fs::path, source_file> files;
    std::set<std::string> allowlist;
    std::set<std::string> seen;
    std::set<fs::path> included;
    std::set<fs::path> measured {};
};

struct source_location {
    fs::path file;
    unsigned line = 0;
    unsigned column = 0;
    unsigned offset = 0;
};

source_location locate(CXSourceLocation location);
void add_finding(inspection& state, diagnostic_finding finding);
void inspect_name(
    inspection& state, const source_file& file, const std::string& name,
    const std::string& entity_kind, unsigned line, unsigned column, bool test
);

void inspect_file_volume(inspection& state, CXTranslationUnit unit);
void inspect_structure(
    inspection& state, CXCursor cursor, const source_file& file
);
void finish_structure(inspection& state);

} // namespace ecosystem::personal_support
