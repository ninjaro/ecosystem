#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace ecosystem {

int run_engels(int argc, const char* const* argv);

int run_engels_cli(
    const std::vector<std::string>& args, std::ostream& out, std::ostream& err
);

} // namespace ecosystem
