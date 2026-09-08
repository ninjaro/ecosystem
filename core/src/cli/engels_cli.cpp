#include "cli/engels_cli.hpp"

#include "cli/actor_cli.hpp"
#include "cli/common_args.hpp"
#include "cli/engels_args.hpp"
#include "cli/engels_command.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace ecosystem {

int run_engels_request(
    const args_list& args, std::ostream& out, std::ostream& err
);

int run_engels_cli(
    const args_list& args, std::ostream& out, std::ostream& err
) {
    return run_actor_frontend(
        actor_frontend { "engels", engels_cli_support::engels_usage_text(),
                         run_engels_request },
        args, out, err
    );
}

int run_engels(const int argc, const char* const* argv) {
    const args_list args = collect_cli_args(argc, argv);
    return run_engels_cli(args, std::cout, std::cerr);
}

int run_engels_request(
    const std::vector<std::string>& args, std::ostream& out, std::ostream& err
) {
    if (args.empty() || args[0] == "help" || args[0] == "--help"
        || args[0] == "-h") {
        out << engels_cli_support::engels_usage_text();
        return args.empty() ? exit_code(command_error::invalid_request) : 0;
    }

    std::string error_message;
    if (!engels_cli_support::parse_engels_request(args, &error_message)
             .has_value()) {
        emit_command_error(err, command_error::invalid_request, error_message);
        return exit_code(command_error::invalid_request);
    }

    return run_engels_command(args, out, err);
}

}  // namespace ecosystem
