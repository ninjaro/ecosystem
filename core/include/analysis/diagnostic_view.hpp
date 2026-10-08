#pragma once

#include "analysis/diagnostic.hpp"

#include <vector>

namespace ecosystem {

struct diagnostic_group {
    std::string category;
    std::string severity;
    std::vector<const json*> findings;
};

/// Groups borrow findings from the immutable canonical snapshot.
std::vector<diagnostic_group>
group_diagnostics(const json& report, const diagnostic_view& view);

void render_diagnostic_markdown(
    const json& report, std::ostream& out, const diagnostic_view& view
);

} // namespace ecosystem
