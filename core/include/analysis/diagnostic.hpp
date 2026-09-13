#pragma once

#include "manifest.hpp"

#include <optional>
#include <ostream>

namespace ecosystem {

struct diagnostic_finding {
    std::string rule;
    std::string artifact;
    std::string file;
    unsigned line = 1;
    unsigned column = 1;
    std::string entity_kind;
    std::string entity;
    std::string message;
    json measurements = json::object();
    json thresholds = json::object();
    std::optional<std::string> referent {};
    std::string category {};
    std::string severity = "hint";
    std::string authority = "manifesto";
    std::string enforcement = "advisory";
};

json to_json(const diagnostic_finding& finding);
// Both checks and report commands render the canonical JSON result.
void render_diagnostic_report(const json& report, std::ostream& out);

} // namespace ecosystem
