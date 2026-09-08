#include "cli/marx_cli.hpp"

#include "cli/actor_cli.hpp"
#include "cli/common_args.hpp"
#include "cli/marx_args.hpp"
#include "cli/marx_command.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace ecosystem {

int run_marx_request(
    const args_list& args, std::ostream& out, std::ostream& err
);

int run_marx_cli(const args_list& args, std::ostream& out, std::ostream& err) {
    return run_actor_frontend(
        actor_frontend { "marx", marx_cli_support::marx_usage_text(),
                         run_marx_request },
        args, out, err
    );
}

int run_marx(const int argc, const char* const* argv) {
    const args_list args = collect_cli_args(argc, argv);
    return run_marx_cli(args, std::cout, std::cerr);
}

int run_marx_request(
    const std::vector<std::string>& args, std::ostream& out, std::ostream& err
) {
    if (args.empty() || args[0] == "help" || args[0] == "--help"
        || args[0] == "-h") {
        out << marx_cli_support::marx_usage_text();
        return args.empty() ? exit_code(command_error::invalid_request) : 0;
    }

    std::string error_message;
    if (!marx_cli_support::parse_marx_request(args, &error_message)
             .has_value()) {
        emit_command_error(err, command_error::invalid_request, error_message);
        return exit_code(command_error::invalid_request);
    }

    return run_marx_command(args, out, err);
}

}  // namespace ecosystem
