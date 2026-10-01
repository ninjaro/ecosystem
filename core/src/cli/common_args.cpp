#include "cli/common_args.hpp"

#include <cxxopts.hpp>

#include <string>
#include <vector>

namespace ecosystem::common_args_support {

std::string expected_usage(
    const std::string& actor_name, const std::string& usage_tail
) {
    return "expected: " + actor_name + " " + usage_tail;
}

std::vector<char*> argv_for_args(
    const std::string& command_name, const args_list& args
) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1U);
    argv.push_back(const_cast<char*>(command_name.c_str()));
    for (const std::string& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    return argv;
}

std::vector<std::string> positional_values(
    const cxxopts::ParseResult& result, const std::string& key
) {
    if (result.count(key) == 0U) {
        return {};
    }
    return result[key].as<std::vector<std::string>>();
}

bool duplicate_option(
    const cxxopts::ParseResult& result, const std::string& key
) {
    return result.count(key) > 1U;
}

void append_repeated_scope_option(
    args_list* scope_args, const cxxopts::ParseResult& result,
    const std::string& key
) {
    if (result.count(key) == 0U) {
        return;
    }
    for (const std::string& value :
         result[key].as<std::vector<std::string>>()) {
        scope_args->push_back("--" + key);
        scope_args->push_back(value);
    }
}

args_list scope_args_from_result(const cxxopts::ParseResult& result) {
    args_list scope_args;
    append_repeated_scope_option(&scope_args, result, "project");
    append_repeated_scope_option(&scope_args, result, "group");
    const std::vector<std::string> positional = positional_values(result, "scope");
    scope_args.insert(scope_args.end(), positional.begin(), positional.end());
    return scope_args;
}

std::optional<cxxopts::ParseResult> parse_options(
    cxxopts::Options* options, const std::string& command_name,
    const args_list& args, std::string* error_message,
    const std::string& usage_message
) {
    try {
        std::vector<char*> argv = argv_for_args(command_name, args);
        const int argc = static_cast<int>(argv.size());
        char** argv_data = argv.data();
        return options->parse(argc, argv_data);
    } catch (const cxxopts::exceptions::exception&) {
        *error_message = usage_message;
        return std::nullopt;
    }
}

std::string mutate_add_component_usage(const std::string& actor_name) {
    return expected_usage(
        actor_name,
        "mutate add component <component-id> [--kind <kind>] "
        "[--artifact-id <artifact-id>] [--link <component:artifact>]..."
    );
}

}  // namespace ecosystem::common_args_support

namespace ecosystem {

args_list collect_cli_args(const int argc, const char* const* argv) {
    args_list args;
    for (int index = 1; index < argc; ++index) {
        args.emplace_back(argv[index]);
    }
    return args;
}

void emit_command_error(
    std::ostream& err, const command_error error_class,
    const std::string& message
) {
    switch (error_class) {
    case command_error::invalid_request:
        err << "error[invalid_request]: " << message << "\n";
        break;
    case command_error::unsupported_by_manifest:
        err << "error[unsupported_by_manifest]: " << message << "\n";
        break;
    case command_error::missing_local_tooling:
        err << "error[missing_local_tooling]: " << message << "\n";
        break;
    case command_error::task_failed:
        err << "error[task_failed]: " << message << "\n";
        break;
    case command_error::ok:
        break;
    }
}

split_passthrough_args_result split_passthrough_args(const args_list& args) {
    split_passthrough_args_result result;
    bool passthrough = false;
    for (const std::string& arg : args) {
        if (!passthrough && arg == "--") {
            passthrough = true;
            continue;
        }
        if (passthrough) {
            result.passthrough_args.push_back(arg);
        } else {
            result.command_args.push_back(arg);
        }
    }
    return result;
}

std::optional<run_request> parse_run_request(
    const std::string& actor_name, const args_list& args,
    std::string* error_message
) {
    const split_passthrough_args_result split = split_passthrough_args(args);
    const std::string usage_message = common_args_support::expected_usage(
        actor_name,
        "run <profile> [component:artifact] [--android-mode "
        "<auto|emulator|device>] [-- <arg> ...]"
    );

    cxxopts::Options options(actor_name + " run", "");
    options.add_options()(
        "project", "Workspace project selector",
        cxxopts::value<std::vector<std::string>>()
    )(
        "group", "Workspace group selector",
        cxxopts::value<std::vector<std::string>>()
    )(
        "android-mode", "Android deployment mode",
        cxxopts::value<std::string>()
    )("scope", "", cxxopts::value<std::vector<std::string>>());
    options.parse_positional({ "scope" });

    const std::optional<cxxopts::ParseResult> result
        = common_args_support::parse_options(
            &options, actor_name, split.command_args, error_message,
            usage_message
        );
    if (!result.has_value()) {
        return std::nullopt;
    }
    if (common_args_support::duplicate_option(*result, "android-mode")) {
        *error_message = usage_message;
        return std::nullopt;
    }

    run_request request;
    request.command_args = common_args_support::scope_args_from_result(*result);
    request.passthrough_args = split.passthrough_args;
    if (result->count("android-mode") != 0U) {
        request.android_mode = (*result)["android-mode"].as<std::string>();
    }
    return request;
}

std::optional<benchmark_request> parse_benchmark_request(
    const std::string& actor_name, const args_list& args,
    std::string* error_message
) {
    const split_passthrough_args_result split = split_passthrough_args(args);
    const std::string usage_message = common_args_support::expected_usage(
        actor_name, "benchmark [component:artifact] [-- <arg> ...]"
    );

    cxxopts::Options options(actor_name + " benchmark", "");
    options.add_options()(
        "project", "Workspace project selector",
        cxxopts::value<std::vector<std::string>>()
    )(
        "group", "Workspace group selector",
        cxxopts::value<std::vector<std::string>>()
    )("scope", "", cxxopts::value<std::vector<std::string>>());
    options.parse_positional({ "scope" });

    const std::optional<cxxopts::ParseResult> result
        = common_args_support::parse_options(
            &options, actor_name, split.command_args, error_message,
            usage_message
        );
    if (!result.has_value()) {
        return std::nullopt;
    }

    benchmark_request request;
    request.scope_args = common_args_support::scope_args_from_result(*result);
    request.passthrough_args = split.passthrough_args;
    return request;
}

std::optional<check_request> parse_check_request(
    const std::string& actor_name, const args_list& args,
    std::string* error_message
) {
    const std::string usage_message = common_args_support::expected_usage(
        actor_name,
        "check <profile> [component:artifact] [--theme <theme>] [--changes]"
    );

    cxxopts::Options options(actor_name + " check", "");
    auto add_option = options.add_options();
    add_option(
        "project", "Workspace project selector",
        cxxopts::value<std::vector<std::string>>()
    );
    add_option(
        "group", "Workspace group selector",
        cxxopts::value<std::vector<std::string>>()
    );
    add_option("theme", "Sphinx theme override", cxxopts::value<std::string>());
    add_option("changes", "Verify formatting for validated event changes");
    add_option("scope", "", cxxopts::value<std::vector<std::string>>());
    options.parse_positional({ "scope" });

    const std::optional<cxxopts::ParseResult> result
        = common_args_support::parse_options(
            &options, actor_name, args, error_message, usage_message
        );
    if (!result.has_value()) {
        return std::nullopt;
    }
    if (common_args_support::duplicate_option(*result, "theme")
        || common_args_support::duplicate_option(*result, "changes")) {
        *error_message = usage_message;
        return std::nullopt;
    }

    check_request request;
    request.changed_files
        = result->count("changes") != 0U && (*result)["changes"].as<bool>();
    request.scope_args = common_args_support::scope_args_from_result(*result);
    if (result->count("theme") != 0U) {
        request.sphinx_theme = (*result)["theme"].as<std::string>();
    }
    return request;
}

std::optional<prerelease_request> parse_prerelease_request(
    const std::string& actor_name, const args_list& args,
    std::string* error_message
) {
    const std::string usage_message = common_args_support::expected_usage(
        actor_name,
        "prerelease [component:artifact] [--version-base "
        "<major.minor.patch>] [--sign] [--sign-key <key-id>]"
    );

    cxxopts::Options options(actor_name + " prerelease", "");
    options.add_options()(
        "project", "Workspace project selector",
        cxxopts::value<std::vector<std::string>>()
    )(
        "group", "Workspace group selector",
        cxxopts::value<std::vector<std::string>>()
    )(
        "version-base", "Base version", cxxopts::value<std::string>()
    )(
        "sign", "Sign prerelease metadata"
    )(
        "sign-key", "Signing key id", cxxopts::value<std::string>()
    )("scope", "", cxxopts::value<std::vector<std::string>>());
    options.parse_positional({ "scope" });

    const std::optional<cxxopts::ParseResult> result
        = common_args_support::parse_options(
            &options, actor_name, args, error_message, usage_message
        );
    if (!result.has_value()) {
        return std::nullopt;
    }
    if (common_args_support::duplicate_option(*result, "version-base")
        || common_args_support::duplicate_option(*result, "sign-key")) {
        *error_message = usage_message;
        return std::nullopt;
    }

    prerelease_request request;
    request.scope_args = common_args_support::scope_args_from_result(*result);
    request.signing_options.sign = result->count("sign") != 0U
        || result->count("sign-key") != 0U;
    if (result->count("version-base") != 0U) {
        request.version_base = (*result)["version-base"].as<std::string>();
    }
    if (result->count("sign-key") != 0U) {
        request.signing_options.key_id
            = (*result)["sign-key"].as<std::string>();
    }
    return request;
}

std::optional<add_component_options> parse_add_component_options(
    const std::string& actor_name, const args_list& args,
    std::string* error_message
) {
    if (args.size() < 3U) {
        *error_message = common_args_support::mutate_add_component_usage(
            actor_name
        );
        return std::nullopt;
    }

    const args_list option_args(args.begin() + 3, args.end());
    cxxopts::Options options(actor_name + " mutate add component", "");
    options.add_options()(
        "kind", "Component template kind", cxxopts::value<std::string>()
    )(
        "artifact-id", "Artifact id override", cxxopts::value<std::string>()
    )(
        "link", "Artifact link", cxxopts::value<std::vector<std::string>>()
    );

    const std::optional<cxxopts::ParseResult> result
        = common_args_support::parse_options(
            &options, actor_name, option_args, error_message,
            common_args_support::mutate_add_component_usage(actor_name)
        );
    if (!result.has_value()) {
        return std::nullopt;
    }
    if (common_args_support::duplicate_option(*result, "kind")
        || common_args_support::duplicate_option(*result, "artifact-id")) {
        *error_message = common_args_support::mutate_add_component_usage(
            actor_name
        );
        return std::nullopt;
    }

    add_component_options options_value;
    if (result->count("kind") != 0U) {
        options_value.template_kind = (*result)["kind"].as<std::string>();
    }
    if (result->count("artifact-id") != 0U) {
        options_value.artifact_id = (*result)["artifact-id"].as<std::string>();
    }
    if (result->count("link") != 0U) {
        for (const std::string& link_value :
             (*result)["link"].as<std::vector<std::string>>()) {
            const std::optional<artifact_ref> link_ref
                = parse_artifact_ref(link_value);
            if (!link_ref.has_value()) {
                *error_message
                    = "artifact link must use component:artifact form";
                return std::nullopt;
            }
            options_value.artifact_links.push_back(*link_ref);
        }
    }
    return options_value;
}

}  // namespace ecosystem
