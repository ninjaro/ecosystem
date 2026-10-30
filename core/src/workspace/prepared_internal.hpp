#pragma once

#include "workspace/prepared.hpp"

#include <map>
#include <set>
#include <stdexcept>

namespace ecosystem::prepared_detail {

struct unavailable final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct reference_file {
    std::filesystem::path source;
    std::string artifact;
    std::string role;
    bool runtime = false;
};

struct context {
    std::filesystem::path root;
    std::filesystem::path input_path;
    std::filesystem::path payload;
    std::filesystem::path work;
    manifest project;
    artifact_ref selected;
    json input;
    std::string manifest_digest;
    std::string input_digest;
    std::string revision;
    std::map<std::string, reference_file> expected;
    json commands = json::array();
    json measurements = json::array();
};

void require(bool condition, const std::string& message);
std::string digest(const std::filesystem::path& path);
void regular(const std::filesystem::path& path);
void collect_inventory(context& value);
json check_inventory(context& value);

} // namespace ecosystem::prepared_detail
