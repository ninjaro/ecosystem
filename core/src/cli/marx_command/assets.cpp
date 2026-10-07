#include "cli/marx_command.hpp"

#include "cli/marx_args.hpp"
#include "workspace/assets.hpp"

#include <ostream>

namespace ecosystem {

int run_assets_command(
    const std::filesystem::path& root, const args_list& args, std::ostream& out,
    std::ostream& err
) {
    std::string error;
    if (!marx_cli_support::validate_assets_request(args, &error)) {
        emit_command_error(err, command_error::invalid_request, error);
        return exit_code(command_error::invalid_request);
    }
    bool written = false;
    if (args[0] == "remove") {
        written = remove_asset_entry(root, args[1], args[2], &error);
    } else {
        asset_entry entry { args[2], args[3], args[4], {}, {} };
        std::string before;
        for (std::size_t i = 5; i < args.size(); i += 2) {
            if (args[i] == "--description")
                entry.description = args[i + 1];
            else if (args[i] == "--datetime")
                entry.datetime = args[i + 1];
            else
                before = args[i + 1];
        }
        written = put_asset_entry(root, args[1], entry, before, &error);
    }
    if (!written) {
        emit_command_error(err, command_error::task_failed, error);
        return exit_code(command_error::task_failed);
    }
    out << "updated assets/" << args[1] << "/index.tsv\n";
    return 0;
}

} // namespace ecosystem
