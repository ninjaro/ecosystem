#pragma once

#include "cli/common_args.hpp"

#include <optional>
#include <string>

namespace ecosystem::marx_cli_support {

struct marx_request {
    std::string command;
    args_list command_args;
};

std::string marx_usage_text();
bool validate_assets_request(const args_list& args, std::string* error_message);

std::optional<marx_request>
parse_marx_request(const args_list& args, std::string* error_message);

} // namespace ecosystem::marx_cli_support
