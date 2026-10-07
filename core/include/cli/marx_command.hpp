#pragma once

#include "cli/common_args.hpp"

#include <filesystem>
#include <iosfwd>

namespace ecosystem {

int run_assets_command(
    const std::filesystem::path& root, const args_list& args, std::ostream& out,
    std::ostream& err
);

int run_marx_command(
    const args_list& args, std::ostream& out, std::ostream& err
);

} // namespace ecosystem
