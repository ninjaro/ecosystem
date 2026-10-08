#include "cli/engels_args.hpp"

namespace ecosystem::engels_cli_support {

bool parse_report_view(
    args_list* args, bool* json_output, diagnostic_view* view,
    std::string* error_message
) {
    args_list remaining;
    bool output_selected = false;
    for (std::size_t index = 0; index < args->size(); ++index) {
        const auto& arg = args->at(index);
        if (arg == "--json" || arg == "--markdown") {
            if (output_selected) {
                *error_message = "choose --json or --markdown only once";
                return false;
            }
            output_selected = true;
            *json_output = arg == "--json";
            if (arg == "--markdown")
                view->format = diagnostic_format::markdown;
        } else if (arg == "--category" || arg == "--severity") {
            auto& target
                = arg == "--category" ? view->category : view->severity;
            if (target || index + 1 == args->size()
                || args->at(index + 1).empty()
                || args->at(index + 1).starts_with("--")) {
                *error_message
                    = arg + " requires one nonempty value, only once";
                return false;
            }
            target = args->at(++index);
        } else {
            remaining.push_back(arg);
        }
    }
    if (*json_output && (view->category || view->severity)) {
        *error_message
            = "--json emits the full snapshot; filters require a display view";
        return false;
    }
    *args = std::move(remaining);
    return true;
}

} // namespace ecosystem::engels_cli_support
