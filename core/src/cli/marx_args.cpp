#include "cli/marx_args.hpp"

#include <cxxopts.hpp>

#include <string>
#include <vector>

namespace ecosystem::marx_cli_support {

std::string expected_usage(const std::string& usage_tail) {
    return "expected: marx " + usage_tail;
}

std::vector<char*> argv_for_args(const args_list& args) {
    static char command_name[] = "marx";
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
        *error_message = marx_usage_text();
        return std::nullopt;
    }
}

bool known_marx_command(const std::string& command) {
    return command == "sync" || command == "format" || command == "mutate"
        || command == "build" || command == "benchmark" || command == "run"
        || command == "prerelease";
}

std::string marx_usage_text() {
    return "usage: marx <command> [options] [arguments]\n"
           "\n"
           "commands:\n"
           "  sync [--project <project> ...] [--group <group> ...]\n"
           "  format [component:artifact] [--project <project> ...] [--group "
           "<group> ...]\n"
           "  mutate add component <component-id> [--kind <kind>] "
           "[--artifact-id <artifact-id>]\n"
           "  mutate add module <component-id> <module-path>\n"
           "  mutate add files <component-id> <module-path>\n"
           "  mutate add file-unit <component-id> <module-id> --kind <kind>\n"
           "  mutate set facade-entry <component:artifact>\n"
           "  build <profile> [component:artifact] [--project <project> ...]\n"
           "  benchmark [component:artifact] [--project <project> ...] [-- "
           "<arg> ...]\n"
           "  run <profile> [component:artifact] [--project <project> ...] "
           "[--android-mode <mode>] [-- <arg> ...]\n"
           "  prerelease [component:artifact] [--project <project> ...] "
           "[--version-base <major.minor.patch>]\n";
}

bool validate_mutate_request(
    const args_list& args, std::string* error_message
) {
    if (args.size() < 2U) {
        *error_message = "expected: marx mutate <subcommand>";
        return false;
    }
    if (args[0] == "add" && args[1] == "component") {
        return parse_add_component_options("marx", args, error_message)
            .has_value();
    }
    if (args[0] == "add" && (args[1] == "module" || args[1] == "files")) {
        if (args.size() != 4U) {
            *error_message = "expected: marx mutate add " + args[1]
                + " <component-id> <module-path>";
            return false;
        }
        return true;
    }
    if (args[0] == "add" && args[1] == "file-unit") {
        if (args.size() != 6U || args[4] != "--kind") {
            *error_message = "expected: marx mutate add file-unit "
                "<component-id> <module-id> --kind <kind>";
            return false;
        }
        return true;
    }
    if (args[0] == "set" && args[1] == "facade-entry") {
        if (args.size() != 3U) {
            *error_message
                = "expected: marx mutate set facade-entry <component:artifact>";
            return false;
        }
        return true;
    }
    *error_message = "unsupported mutate operation";
    return false;
}

std::optional<marx_request> parse_marx_request(
    const args_list& args, std::string* error_message
) {
    cxxopts::Options options("marx", "");
    options.allow_unrecognised_options();
    options.add_options()("command", "", cxxopts::value<std::string>());
    options.parse_positional({ "command" });

    const std::optional<cxxopts::ParseResult> result
        = parse_options(&options, args, error_message);
    if (!result.has_value()) {
        return std::nullopt;
    }
    if (result->count("command") == 0U) {
        *error_message = marx_usage_text();
        return std::nullopt;
    }

    marx_request request;
    request.command = (*result)["command"].as<std::string>();
    if (args.size() > 1U) {
        request.command_args.assign(args.begin() + 1, args.end());
    }

    if (!known_marx_command(request.command)) {
        *error_message = "unknown marx command: " + request.command;
        return std::nullopt;
    }

    if (request.command == "build" && request.command_args.empty()) {
        *error_message
            = expected_usage("build <profile> [component:artifact]");
        return std::nullopt;
    }
    if (request.command == "run") {
        std::string usage_error;
        if (!parse_run_request("marx", request.command_args, &usage_error)
                 .has_value()) {
            *error_message = usage_error;
            return std::nullopt;
        }
    }
    if (request.command == "benchmark") {
        std::string usage_error;
        if (!parse_benchmark_request(
                "marx", request.command_args, &usage_error
            )
                 .has_value()) {
            *error_message = usage_error;
            return std::nullopt;
        }
    }
    if (request.command == "prerelease") {
        std::string usage_error;
        if (!parse_prerelease_request(
                "marx", request.command_args, &usage_error
            )
                 .has_value()) {
            *error_message = usage_error;
            return std::nullopt;
        }
    }
    if (request.command == "mutate"
        && !validate_mutate_request(request.command_args, error_message)) {
        return std::nullopt;
    }

    return request;
}

}  // namespace ecosystem::marx_cli_support
