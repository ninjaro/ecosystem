#pragma once

#include "manifest.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ecosystem {

enum class command_error {
    ok = 0,
    invalid_request = 2,
    unsupported_by_manifest = 3,
    missing_local_tooling = 4,
    task_failed = 5,
};

struct tool_status {
    std::string name;
    bool available = false;
    std::string path;
    std::string version;
    int version_exit_code = -1;
    std::string version_error;
};

struct captured_command {
    int exit_code = -1;
    std::string output;
};

int exit_code(command_error error_class);

bool command_exists(const std::string& name);
std::string find_command_path(const std::string& name);
std::string capture_command(
    const std::vector<std::string>& args,
    const std::filesystem::path& cwd = std::filesystem::path()
);
captured_command capture_command_result(
    const std::vector<std::string>& args,
    const std::filesystem::path& cwd = std::filesystem::path(),
    const std::vector<std::pair<std::string, std::string>>& environment = {}
);
int run_command(
    const std::vector<std::string>& args, const std::filesystem::path& cwd,
    const std::vector<std::pair<std::string, std::string>>& environment = {}
);

// Records native commands on this thread for the lifetime of the scope.
// Environment values are deliberately excluded from the transcript.
class scoped_command_log {
public:
    explicit scoped_command_log(const std::filesystem::path& path);
    ~scoped_command_log();
    scoped_command_log(const scoped_command_log&) = delete;
    scoped_command_log& operator=(const scoped_command_log&) = delete;

    const std::string& error() const;
    static std::string active_error();

private:
    friend captured_command
    capture_command_result(const std::vector<std::string>&, const std::filesystem::path&, const std::vector<std::pair<std::string, std::string>>&);
    friend int
    run_command(const std::vector<std::string>&, const std::filesystem::path&, const std::vector<std::pair<std::string, std::string>>&);
    static captured_command capture(
        const std::vector<std::string>& args, const std::filesystem::path& cwd,
        const std::vector<std::pair<std::string, std::string>>& environment,
        bool echo_output
    );
    void append(const std::string& text);

    static thread_local scoped_command_log* active_;
    scoped_command_log* previous_;
    std::filesystem::path path_;
    std::ofstream stream_;
    std::string error_;
};

tool_status probe_tool(
    const std::string& name, const std::vector<std::string>& version_args = {}
);
std::string find_gradle_command_path(const std::filesystem::path& project_root);
json toolchains_report(
    const std::optional<std::string>& profile = std::nullopt,
    const std::filesystem::path& project_root = {}
);

} // namespace ecosystem
