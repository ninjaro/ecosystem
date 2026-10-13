#pragma once

#include "cli/common_args.hpp"

#include <iosfwd>

namespace ecosystem {

int run_engels_command(
    const args_list& args, std::ostream& out, std::ostream& err
);

} // namespace ecosystem
