#include "internal.hpp"
#include "workspace/tooling.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ecosystem {
using namespace tooling_detail;

namespace {

    std::string shell_quote(const std::string& value) {
        std::string escaped = "'";
        for (const char character : value) {
            if (character == '\'') {
                escaped += "'\"'\"'";
            } else {
                escaped.push_back(character);
            }
        }
        escaped.push_back('\'');
        return escaped;
    }

    std::string shell_command(
        const std::vector<std::string>& args, const fs::path& cwd,
        const std::vector<std::pair<std::string, std::string>>& environment
    ) {
        std::ostringstream stream;
        if (!cwd.empty()) {
            stream << "cd " << shell_quote(cwd.string()) << " && ";
        }
        for (const auto& [name, value] : environment) {
            stream << name << "=" << shell_quote(value) << " ";
        }
        for (std::size_t index = 0; index < args.size(); ++index) {
            if (index > 0U) {
                stream << " ";
            }
            stream << shell_quote(args[index]);
        }
        return stream.str();
    }

} // namespace

bool command_exists(const std::string& name) {
    return !find_command_path(name).empty();
}

std::string find_command_path(const std::string& name) {
    const auto executable_file = [](const fs::path& path) {
        std::error_code error;
        return fs::is_regular_file(path, error) && !error
            && ::access(path.c_str(), X_OK) == 0;
    };
    if (name.find('/') != std::string::npos) {
        return executable_file(name) ? fs::absolute(name).string()
                                     : std::string();
    }

    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return {};
    }

    std::stringstream stream(path_env);
    std::string directory;
    while (std::getline(stream, directory, ':')) {
        const fs::path candidate = fs::path(directory) / name;
        if (executable_file(candidate)) {
            return fs::absolute(candidate).string();
        }
    }
    return {};
}

std::string
capture_command(const std::vector<std::string>& args, const fs::path& cwd) {
    return capture_command_result(args, cwd).output;
}

captured_command capture_command_result(
    const std::vector<std::string>& args, const fs::path& cwd,
    const std::vector<std::pair<std::string, std::string>>& environment
) {
    return scoped_command_log::capture(args, cwd, environment, false);
}

thread_local scoped_command_log* scoped_command_log::active_ = nullptr;

scoped_command_log::scoped_command_log(const fs::path& path)
    : previous_(active_)
    , path_(path)
    , stream_(path, std::ios::binary) {
    active_ = this;
    append("");
}

scoped_command_log::~scoped_command_log() { active_ = previous_; }

const std::string& scoped_command_log::error() const { return error_; }

std::string scoped_command_log::active_error() {
    return active_ == nullptr ? std::string() : active_->error();
}

void scoped_command_log::append(const std::string& text) {
    if (!error_.empty())
        return;
    stream_ << text;
    stream_.flush();
    if (!stream_)
        error_ = "unable to write command log: " + path_.string();
}

captured_command scoped_command_log::capture(
    const std::vector<std::string>& args, const fs::path& cwd,
    const std::vector<std::pair<std::string, std::string>>& environment,
    const bool echo_output
) {
    captured_command result;
    const auto started = std::chrono::steady_clock::now();
    if (active_ != nullptr) {
        const json command {
            { "argv", args },
            { "cwd", cwd.empty() ? fs::current_path().string() : cwd.string() }
        };
        active_->append("\n>>> " + command.dump() + "\n");
        if (!active_->error().empty()) {
            result.output = active_->error();
            return result;
        }
    }
    const std::string command
        = "(" + shell_command(args, cwd, environment) + ") 2>&1";
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        result.output = "unable to start command\n";
        if (active_ != nullptr)
            active_->append(result.output + "<<< {\"exit_code\":-1}\n");
        return result;
    }

    bool read_failed = false;
    char buffer[4096];
    for (;;) {
        const auto count = ::read(::fileno(pipe), buffer, sizeof(buffer));
        if (count == 0)
            break;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            read_failed = true;
            break;
        }
        const std::string chunk(buffer, static_cast<std::size_t>(count));
        if (echo_output) {
            std::cout << chunk;
            std::cout.flush();
        } else {
            result.output += chunk;
        }
        if (active_ != nullptr)
            active_->append(chunk);
    }
    const int status = ::pclose(pipe);
    result.exit_code = status == -1 || read_failed ? -1
        : WIFEXITED(status)                        ? WEXITSTATUS(status)
        : WIFSIGNALED(status)                      ? 128 + WTERMSIG(status)
                                                   : status;
    if (active_ != nullptr) {
        active_->append(
            "\n<<< "
            + json { { "exit_code", result.exit_code },
                     { "seconds",
                       std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - started
                       )
                           .count() } }
                  .dump()
            + "\n"
        );
        if (!active_->error().empty()) {
            result.exit_code = -1;
            result.output += "\n" + active_->error();
        }
    }
    return result;
}

int run_command(
    const std::vector<std::string>& args, const fs::path& cwd,
    const std::vector<std::pair<std::string, std::string>>& environment
) {
    if (scoped_command_log::active_ != nullptr)
        return scoped_command_log::capture(args, cwd, environment, true)
            .exit_code;
    const std::string command = shell_command(args, cwd, environment);
    const int status = std::system(command.c_str());
    if (status == -1) {
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return status;
}

} // namespace ecosystem
