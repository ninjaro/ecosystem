#include "cli/engels_args.hpp"

#include <cxxopts.hpp>

#include <string>
#include <vector>

namespace ecosystem::engels_cli_support {

std::string expected_usage(const std::string& usage_tail) {
    return "expected: engels " + usage_tail;
}

std::vector<char*> argv_for_args(const args_list& args) {
    static char command_name[] = "engels";
    std::vector<char*> argv;
    argv.reserve(args.size() + 1U);
    argv.push_back(command_name);
    for (const std::string& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    return argv;
}

std::optional<cxxopts::ParseResult> parse_options(
    cxxopts::Options* options, const args_list& args,
    std::string* error_message
) {
    try {
        std::vector<char*> argv = argv_for_args(args);
        const int argc = static_cast<int>(argv.size());
        char** argv_data = argv.data();
        return options->parse(argc, argv_data);
    } catch (const cxxopts::exceptions::exception&) {
        *error_message = engels_usage_text();
        return std::nullopt;
    }
}

bool known_engels_command(const std::string& command) {
    return command == "list" || command == "check" || command == "doctor"
        || command == "report";
}

std::string engels_usage_text() {
    return "usage: engels <command> [options] [arguments]\n"
           "\n"
           "commands:\n"
           "  list "
           "[components|artifacts|profiles|platforms|matrix|projects|groups]\n"
           "  check <profile> [component:artifact] [--project <project> ...]\n"
           "  doctor [profile] [component:artifact] [--project <project> ...]\n"
           "  doctor <component:artifact> --profile <profile>\n"
           "  report <kind> [component:artifact] [--project <project> ...] "
           "[--json]\n"
           "\ncheck profiles: format tidy tests coverage leaks naming style "
           "repo ci "
           "doxy sphinx java\n"
           "report kinds: matrix cxx naming style toolchains\n";
}

std::optional<engels_request> parse_engels_request(
    const args_list& args, std::string* error_message
) {
    cxxopts::Options options("engels", "");
    options.allow_unrecognised_options();
    options.add_options()("command", "", cxxopts::value<std::string>());
    options.parse_positional({ "command" });

    const std::optional<cxxopts::ParseResult> result
        = parse_options(&options, args, error_message);
    if (!result.has_value()) {
        return std::nullopt;
    }
    if (result->count("command") == 0U) {
        *error_message = engels_usage_text();
        return std::nullopt;
    }

    engels_request request;
    request.command = (*result)["command"].as<std::string>();
    if (args.size() > 1U) {
        request.command_args.assign(args.begin() + 1, args.end());
    }

    if (!known_engels_command(request.command)) {
        *error_message = "unknown engels command: " + request.command;
        return std::nullopt;
    }

    if (request.command == "list" && request.command_args.size() > 1U) {
        *error_message = expected_usage(
            "list [components|artifacts|profiles|platforms|matrix|projects|groups]"
        );
        return std::nullopt;
    }
    if (request.command == "check") {
        std::string usage_error;
        if (!parse_check_request("engels", request.command_args, &usage_error)
                 .has_value()) {
            *error_message = usage_error;
            return std::nullopt;
        }
    }
    if (request.command == "report" && request.command_args.empty()) {
        *error_message = expected_usage(
            "report <kind> [component:artifact] [--project <project> ...] "
            "[--json]"
        );
        return std::nullopt;
    }

    return request;
}

}  // namespace ecosystem::engels_cli_support
