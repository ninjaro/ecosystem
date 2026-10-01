#pragma once

#include "manifest.hpp"
#include "workspace/mutation.hpp"
#include "workspace/release.hpp"
#include "workspace/tooling.hpp"

#include <iosfwd>
#include <optional>
#include <string>

namespace ecosystem {

using args_list = string_list;

struct split_passthrough_args_result {
    args_list command_args;
    args_list passthrough_args;
};

struct run_request {
    args_list command_args;
    args_list passthrough_args;
    std::optional<std::string> android_mode;
};

struct benchmark_request {
    args_list scope_args;
    args_list passthrough_args;
};

struct check_request {
    args_list scope_args;
    std::optional<std::string> sphinx_theme;
    bool changed_files = false;
};

struct prerelease_request {
    args_list scope_args;
    std::optional<std::string> version_base;
    prerelease_signing_options signing_options;
};

args_list collect_cli_args(int argc, const char* const* argv);

void emit_command_error(
    std::ostream& err, command_error error_class, const std::string& message
);

split_passthrough_args_result split_passthrough_args(const args_list& args);

std::optional<run_request> parse_run_request(
    const std::string& actor_name, const args_list& args,
    std::string* error_message
);

std::optional<benchmark_request> parse_benchmark_request(
    const std::string& actor_name, const args_list& args,
    std::string* error_message
);

std::optional<check_request> parse_check_request(
    const std::string& actor_name, const args_list& args,
    std::string* error_message
);

std::optional<prerelease_request> parse_prerelease_request(
    const std::string& actor_name, const args_list& args,
    std::string* error_message
);

std::optional<add_component_options> parse_add_component_options(
    const std::string& actor_name, const args_list& args,
    std::string* error_message
);

}  // namespace ecosystem
