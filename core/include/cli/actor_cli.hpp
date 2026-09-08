#pragma once

#include "cli/common_args.hpp"

#include <functional>

namespace ecosystem {

struct actor_frontend {
    std::string name;
    std::string usage;
    std::function<int(const args_list&, std::ostream&, std::ostream&)> execute;
};

// Shared batch/queue machinery; each instance executes only its actor's
// commands.
int run_actor_frontend(
    const actor_frontend& frontend, const args_list& args, std::ostream& out,
    std::ostream& err
);

} // namespace ecosystem
