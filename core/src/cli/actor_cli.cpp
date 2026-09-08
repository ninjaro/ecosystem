#include "cli/actor_cli.hpp"

#include "cli/common_args.hpp"

#include <cxxopts.hpp>

#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace ecosystem::actor_cli_support {

struct queued_request {
    enum class state {
        pending,
        running,
        completed,
        failed,
        skipped,
    };

    std::size_t id = 0U;
    std::string owner = "engels";
    args_list args;
    std::string summary;
    state current_state = state::pending;
    int exit_status = 0;
    std::string output;
    std::string error;
};

struct actor_frontdoor_request {
    bool quiet = false;
    bool interactive = false;
    bool help = false;
    args_list request_args;
};

std::string actor_usage_text(const actor_frontend& frontend) {
    return frontend.usage
        + "\nshared options:\n"
          "  --quiet <command> ... [--then <command> ...]\n"
          "  --interactive (queue, log <id>, skip <id>, clear, exit)\n";
}

bool is_actor_option_token(const std::string& value) {
    return value == "--quiet" || value == "--interactive" || value == "--help"
        || value == "-h" || value == "help" || value == "interactive";
}

std::string normalized_actor_option_token(const std::string& value) {
    if (value == "help" || value == "-h") {
        return "--help";
    }
    if (value == "interactive") {
        return "--interactive";
    }
    return value;
}

void split_actor_option_prefix(
    const args_list& args, args_list* option_args, args_list* request_args
) {
    std::size_t index = 0U;
    for (; index < args.size(); ++index) {
        if (!is_actor_option_token(args[index])) {
            break;
        }
        option_args->push_back(normalized_actor_option_token(args[index]));
    }
    request_args->assign(args.begin() + static_cast<long>(index), args.end());
}

std::vector<char*> argv_for_args(const args_list& args) {
    static char command_name[] = "actor";
    std::vector<char*> argv;
    argv.reserve(args.size() + 1U);
    argv.push_back(command_name);
    for (const std::string& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    return argv;
}

std::optional<actor_frontdoor_request> parse_actor_frontdoor_request(
    const actor_frontend& frontend, const args_list& args,
    std::string* error_message
) {
    args_list option_args;
    actor_frontdoor_request request;
    split_actor_option_prefix(args, &option_args, &request.request_args);

    cxxopts::Options options(frontend.name, "");
    options.add_options()(
        "quiet", "Suppress successful batch-stage output",
        cxxopts::value<bool>()->default_value("false")->implicit_value("true")
    )(
        "interactive", "Enter the interactive actor shell",
        cxxopts::value<bool>()->default_value("false")->implicit_value("true")
    )(
        "help", "Show actor usage",
        cxxopts::value<bool>()->default_value("false")->implicit_value("true")
    );

    try {
        std::vector<char*> argv = argv_for_args(option_args);
        const int argc = static_cast<int>(argv.size());
        char** argv_data = argv.data();
        const cxxopts::ParseResult result = options.parse(argc, argv_data);
        request.quiet = result["quiet"].as<bool>();
        request.interactive = result["interactive"].as<bool>();
        request.help = result["help"].as<bool>();
    } catch (const cxxopts::exceptions::exception&) {
        *error_message = actor_usage_text(frontend);
        return std::nullopt;
    }

    if (request.help
        && (!request.request_args.empty() || request.interactive)) {
        *error_message = actor_usage_text(frontend);
        return std::nullopt;
    }
    if (request.interactive && !request.request_args.empty()) {
        *error_message = frontend.name
            + " interactive mode does not accept queued requests";
        return std::nullopt;
    }
    return request;
}

std::vector<std::string> split_words(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> words;
    std::string value;
    while (stream >> value) {
        words.push_back(value);
    }
    return words;
}

std::optional<std::size_t> parse_queue_id(const std::string& text) {
    std::size_t id = 0;
    const auto [end, error]
        = std::from_chars(text.data(), text.data() + text.size(), id);
    if (error != std::errc() || end != text.data() + text.size() || id == 0) {
        return std::nullopt;
    }
    return id;
}

std::string join_words(const std::vector<std::string>& words) {
    std::ostringstream stream;
    for (std::size_t index = 0U; index < words.size(); ++index) {
        if (index != 0U) {
            stream << " ";
        }
        stream << words[index];
    }
    return stream.str();
}

std::string state_label(const queued_request::state value) {
    switch (value) {
    case queued_request::state::pending:
        return "pending";
    case queued_request::state::running:
        return "running";
    case queued_request::state::completed:
        return "completed";
    case queued_request::state::failed:
        return "failed";
    case queued_request::state::skipped:
        return "skipped";
    }
    return "pending";
}

std::optional<std::string> command_owner(const std::string& command_name) {
    if (command_name == "list" || command_name == "check"
        || command_name == "doctor" || command_name == "report") {
        return std::string("engels");
    }
    if (command_name == "sync" || command_name == "mutate"
        || command_name == "build" || command_name == "benchmark"
        || command_name == "run" || command_name == "prerelease") {
        return std::string("marx");
    }
    return std::nullopt;
}

std::optional<std::vector<args_list>>
split_actor_requests(const args_list& args, std::string* error_message) {
    std::vector<args_list> requests;
    args_list current;
    bool passthrough = false;
    for (const std::string& arg : args) {
        if (arg == "--") {
            passthrough = true;
        }
        if (arg != "--then" || passthrough) {
            current.push_back(arg);
            continue;
        }
        if (current.empty()) {
            *error_message = "request chain cannot start with --then";
            return std::nullopt;
        }
        requests.push_back(current);
        current.clear();
    }

    if (current.empty()) {
        *error_message = "request chain cannot end with --then";
        return std::nullopt;
    }
    requests.push_back(current);
    return requests;
}

std::string frontend_label(const std::string& owner) { return owner; }

bool validate_actor_request(
    const actor_frontend& frontend, const args_list& args, std::ostream& err
) {
    if (args.empty()) {
        emit_command_error(
            err, command_error::invalid_request, "empty request"
        );
        return false;
    }
    const auto owner = command_owner(args.front());
    if (!owner.has_value()) {
        emit_command_error(
            err, command_error::invalid_request,
            "unknown command: " + args.front()
        );
        return false;
    }
    if (*owner != frontend.name) {
        emit_command_error(
            err, command_error::invalid_request,
            "command '" + args.front() + "' is available via " + *owner
                + "; run `" + *owner + " " + args.front() + "`"
        );
        return false;
    }
    return true;
}

class shell_queue {
public:
    explicit shell_queue(const actor_frontend& frontend)
        : frontend_(frontend) {
        worker_ = std::thread(&shell_queue::worker_loop, this);
    }

    ~shell_queue() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            for (const std::size_t id : pending_ids_) {
                requests_[id - 1U].current_state
                    = queued_request::state::skipped;
            }
            pending_ids_.clear();
        }
        condition_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void enqueue(
        const std::string& owner, const args_list& args, std::ostream& out
    ) {
        std::size_t request_id = 0U;
        std::string summary;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            request_id = requests_.size() + 1U;
            summary = join_words(args);
            requests_.push_back(
                queued_request {
                    request_id,
                    owner,
                    args,
                    summary,
                    queued_request::state::pending,
                    0,
                    {},
                    {},
                }
            );
            pending_ids_.push_back(request_id);
        }
        condition_.notify_all();
        out << "queued #" << request_id << " -> " << frontend_label(owner)
            << " " << summary << "\n";
    }

    void emit_queue(std::ostream& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (requests_.empty()) {
            out << "queue: empty\n";
            return;
        }

        out << "queue:\n";
        for (const queued_request& request : requests_) {
            out << "  #" << request.id << " ["
                << state_label(request.current_state) << "] "
                << frontend_label(request.owner) << " " << request.summary
                << "\n";
        }
    }

    void emit_log(const std::size_t id, std::ostream& out, std::ostream& err) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (id == 0U || id > requests_.size()) {
            err << "error[invalid_request]: unknown queue item: "
                << std::to_string(id) << "\n";
            return;
        }

        const queued_request& request = requests_[id - 1U];
        out << "#" << request.id << " " << frontend_label(request.owner) << " "
            << request.summary << " [" << state_label(request.current_state)
            << "]\n";
        if (!request.output.empty()) {
            out << request.output;
            if (request.output.back() != '\n') {
                out << "\n";
            }
        }
        if (!request.error.empty()) {
            out << request.error;
            if (request.error.back() != '\n') {
                out << "\n";
            }
        }
    }

    void skip(const std::size_t id, std::ostream& out, std::ostream& err) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (id == 0U || id > requests_.size()) {
            err << "error[invalid_request]: unknown queue item: "
                << std::to_string(id) << "\n";
            return;
        }
        queued_request& request = requests_[id - 1U];
        if (request.current_state != queued_request::state::pending) {
            err << "error[invalid_request]: queue item cannot be skipped: #"
                << request.id << "\n";
            return;
        }
        pending_ids_.erase(
            std::remove(pending_ids_.begin(), pending_ids_.end(), id),
            pending_ids_.end()
        );
        request.current_state = queued_request::state::skipped;
        out << "skipped #" << request.id << "\n";
    }

    void clear(std::ostream& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::size_t id : pending_ids_) {
            requests_[id - 1U].current_state = queued_request::state::skipped;
        }
        const std::size_t cleared = pending_ids_.size();
        pending_ids_.clear();
        out << "cleared " << cleared << " pending item(s)\n";
    }

private:
    void worker_loop() {
        while (true) {
            queued_request request;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this]() {
                    return stopping_ || !pending_ids_.empty();
                });
                if (stopping_ && pending_ids_.empty()) {
                    return;
                }
                const std::size_t id = pending_ids_.front();
                pending_ids_.pop_front();
                request = requests_[id - 1U];
                requests_[id - 1U].current_state
                    = queued_request::state::running;
            }

            if (!quiet_) {
                std::lock_guard<std::mutex> io_lock(io_mutex_);
                std::cout << "running #" << request.id << " via "
                          << frontend_label(request.owner) << ": "
                          << request.summary << "\n";
            }

            std::ostringstream request_out;
            std::ostringstream request_err;
            int exit_status = exit_code(command_error::task_failed);
            try {
                exit_status
                    = frontend_.execute(request.args, request_out, request_err);
            } catch (const std::exception& error) {
                emit_command_error(
                    request_err, command_error::task_failed, error.what()
                );
            } catch (...) {
                emit_command_error(
                    request_err, command_error::task_failed,
                    "unexpected queued request failure"
                );
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                queued_request& stored = requests_[request.id - 1U];
                stored.exit_status = exit_status;
                stored.output = request_out.str();
                stored.error = request_err.str();
                stored.current_state = exit_status == 0
                    ? queued_request::state::completed
                    : queued_request::state::failed;
            }

            std::lock_guard<std::mutex> io_lock(io_mutex_);
            std::cout << "completed #" << request.id << " via "
                      << frontend_label(request.owner) << " with exit "
                      << exit_status << "\n";
            if (exit_status != 0) {
                if (!request_out.str().empty()) {
                    std::cout << request_out.str();
                    if (request_out.str().back() != '\n') {
                        std::cout << "\n";
                    }
                }
                if (!request_err.str().empty()) {
                    std::cerr << request_err.str();
                    if (request_err.str().back() != '\n') {
                        std::cerr << "\n";
                    }
                }
            }
        }
    }

    actor_frontend frontend_;
    bool quiet_ = false;
    bool stopping_ = false;
    std::mutex mutex_;
    std::mutex io_mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    std::deque<std::size_t> pending_ids_;
    std::vector<queued_request> requests_;
};

int dispatch_actor_requests(
    const actor_frontend& frontend, const std::vector<args_list>& requests,
    const bool quiet, std::ostream& out, std::ostream& err
) {
    for (const args_list& request : requests) {
        if (!validate_actor_request(frontend, request, err)) {
            return exit_code(command_error::invalid_request);
        }
    }
    int status = 0;
    for (const args_list& request : requests) {
        if (request.empty()) {
            continue;
        }
        const std::optional<std::string> owner = command_owner(request.front());
        if (!owner.has_value()) {
            emit_command_error(
                err, command_error::invalid_request,
                "unknown command: " + request.front()
            );
            return exit_code(command_error::invalid_request);
        }

        if (quiet) {
            std::ostringstream request_out;
            std::ostringstream request_err;
            const int request_status
                = frontend.execute(request, request_out, request_err);
            if (request_status != 0) {
                if (!request_out.str().empty()) {
                    out << request_out.str();
                    if (request_out.str().back() != '\n') {
                        out << "\n";
                    }
                }
                if (!request_err.str().empty()) {
                    err << request_err.str();
                    if (request_err.str().back() != '\n') {
                        err << "\n";
                    }
                }
                return request_status;
            }
            status = request_status;
            continue;
        }

        const int request_status = frontend.execute(request, out, err);
        if (request_status != 0) {
            return request_status;
        }
        status = request_status;
    }
    return status;
}

int run_actor_shell(
    const actor_frontend& frontend, std::ostream& out, std::ostream& err
) {
    shell_queue queue(frontend);
    out << frontend.name << " interactive mode\n";
    out << "type `help` for commands\n";

    std::string line;
    while (true) {
        out << frontend.name << "> ";
        out.flush();
        if (!std::getline(std::cin, line)) {
            out << "\n";
            return 0;
        }

        const std::vector<std::string> words = split_words(line);
        if (words.empty()) {
            continue;
        }
        if (words.front() == "help") {
            out << actor_usage_text(frontend);
            continue;
        }
        if (words.front() == "queue") {
            queue.emit_queue(out);
            continue;
        }
        if (words.front() == "clear") {
            queue.clear(out);
            continue;
        }
        if (words.front() == "exit" || words.front() == "quit") {
            return 0;
        }
        if (words.front() == "skip") {
            if (words.size() != 2U) {
                err << "error[invalid_request]: expected: skip <id>\n";
                continue;
            }
            const auto id = parse_queue_id(words[1]);
            if (id.has_value()) {
                queue.skip(*id, out, err);
            } else {
                err << "error[invalid_request]: invalid queue id: " << words[1]
                    << "\n";
            }
            continue;
        }
        if (words.front() == "log") {
            if (words.size() != 2U) {
                err << "error[invalid_request]: expected: log <id>\n";
                continue;
            }
            const auto id = parse_queue_id(words[1]);
            if (id.has_value()) {
                queue.emit_log(*id, out, err);
            } else {
                err << "error[invalid_request]: invalid queue id: " << words[1]
                    << "\n";
            }
            continue;
        }
        std::string request_error;
        const std::optional<std::vector<args_list>> requests
            = split_actor_requests(words, &request_error);
        if (!requests.has_value()) {
            emit_command_error(
                err, command_error::invalid_request, request_error
            );
            continue;
        }
        const bool valid = std::all_of(
            requests->begin(), requests->end(), [&](const args_list& request) {
                return validate_actor_request(frontend, request, err);
            }
        );
        if (!valid) {
            continue;
        }
        for (const args_list& request : *requests) {
            queue.enqueue(frontend.name, request, out);
        }
    }
}

} // namespace ecosystem::actor_cli_support

namespace ecosystem {

int run_actor_frontend(
    const actor_frontend& frontend, const std::vector<std::string>& args,
    std::ostream& out, std::ostream& err
) {
    std::string parse_error;
    const std::optional<actor_cli_support::actor_frontdoor_request> request
        = actor_cli_support::parse_actor_frontdoor_request(
            frontend, args, &parse_error
        );
    if (!request.has_value()) {
        emit_command_error(err, command_error::invalid_request, parse_error);
        return exit_code(command_error::invalid_request);
    }

    if (request->help) {
        out << actor_cli_support::actor_usage_text(frontend);
        return 0;
    }
    if (request->interactive) {
        return actor_cli_support::run_actor_shell(frontend, out, err);
    }
    if (request->request_args.empty()) {
        out << actor_cli_support::actor_usage_text(frontend);
        return exit_code(command_error::invalid_request);
    }

    std::string chain_error;
    const std::optional<std::vector<args_list>> requests
        = actor_cli_support::split_actor_requests(
            request->request_args, &chain_error
        );
    if (!requests.has_value()) {
        emit_command_error(err, command_error::invalid_request, chain_error);
        return exit_code(command_error::invalid_request);
    }

    return actor_cli_support::dispatch_actor_requests(
        frontend, *requests, request->quiet, out, err
    );
}

} // namespace ecosystem
