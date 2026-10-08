#include "analysis/diagnostic.hpp"

namespace ecosystem {

json to_json(const diagnostic_finding& finding) {
    return { { "rule", finding.rule },
             { "category",
               finding.category.empty()
                   ? finding.rule.substr(0, finding.rule.find('.'))
                   : finding.category },
             { "severity", finding.severity },
             { "authority", finding.authority },
             { "enforcement", finding.enforcement },
             { "artifact", finding.artifact },
             { "file", finding.file },
             { "line", finding.line },
             { "column", finding.column },
             { "entity_kind", finding.entity_kind },
             { "entity", finding.entity },
             { "referent",
               finding.referent ? json(*finding.referent) : json(nullptr) },
             { "message", finding.message },
             { "measurements", finding.measurements },
             { "thresholds", finding.thresholds } };
}

} // namespace ecosystem
