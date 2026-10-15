#pragma once

#include "workspace/project.hpp"
#include "workspace/template_text.hpp"
#include "workspace/tooling.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <map>
#include <unistd.h>

namespace ecosystem::verification_detail {

inline bool may_reuse() {
    const auto* setting = std::getenv("ECOSYSTEM_REUSE_VERIFICATION");
    return setting != nullptr && std::string(setting) == "1";
}

inline json fresh_input_keys(const std::filesystem::path& root) {
    if (!may_reuse())
        return json::object();
    const auto helper = locate_template_path("tooling/verification_inputs.py");
    const auto tidy = probe_tool("clang-tidy");
    auto configuration = root / ".clang-tidy";
    if (!std::filesystem::is_regular_file(configuration))
        configuration = locate_template_path("tooling/clang-tidy.tpl");
    if (helper.empty() || configuration.empty() || !tidy.available)
        return json::object();
    const auto result = capture_command_result(
        { "python3", helper.string(), root.string(),
          (local_build_dir(root, "debug") / "compile_commands.json").string(),
          std::to_string(::getpid()), configuration.string(), tidy.path },
        root
    );
    try {
        if (result.exit_code == 0) {
            const auto keys = json::parse(result.output).at("sources");
            if (keys.is_object())
                return keys;
        }
    } catch (const json::exception&) { }
    return json::object();
}

inline std::filesystem::path cache_file(
    const std::filesystem::path& root, const json& keys,
    const std::filesystem::path& source, const std::string& kind
) {
    const auto name = std::filesystem::weakly_canonical(source).string();
    if (!keys.contains(name) || !keys.at(name).contains("key"))
        return {};
    const auto key = keys.at(name).at("key").get<std::string>();
    if (key.size() != 64
        || key.find_first_not_of("0123456789abcdef") != std::string::npos)
        return {};
    const auto path = std::filesystem::path(".ecosystem/cache/verification")
        / (kind + "-" + key + ".json");
    if (!validate_project_paths(root, { path }).empty())
        return {};
    auto current = root;
    for (const auto& part : path) {
        current /= part;
        std::error_code error;
        if (std::filesystem::is_symlink(
                std::filesystem::symlink_status(current, error)
            ))
            return {};
    }
    return root / path;
}

inline std::string payload_digest(const json& payload) {
    const auto helper = locate_template_path("tooling/verification_inputs.py");
    if (helper.empty())
        return {};
    auto result = capture_command_result({ "python3", helper.string(),
                                           "--digest", payload.dump() });
    if (result.exit_code != 0)
        return {};
    if (!result.output.empty() && result.output.back() == '\n')
        result.output.pop_back();
    return result.output;
}

inline json read_success(const std::filesystem::path& file) {
    if (!may_reuse() || file.empty())
        return nullptr;
    std::string error;
    try {
        if (!std::filesystem::is_regular_file(file)
            || std::filesystem::file_size(file) > 256U * 1024U)
            return nullptr;
        const auto row = json::parse(read_text_file(file, &error));
        if (error.empty() && row.size() == 3 && row.at("status") == "clean"
            && row.at("digest") == payload_digest(row.at("payload")))
            return row.at("payload");
    } catch (const std::exception&) { }
    return nullptr;
}

inline void
publish_success(const std::filesystem::path& file, const json& payload) {
    if (file.empty())
        return;
    std::string error;
    const auto digest = payload_digest(payload);
    if (digest.size() != 64)
        return;
    const auto temporary = std::filesystem::path(
        file.string() + "." + std::to_string(::getpid()) + ".tmp"
    );
    std::error_code temporary_error;
    if (std::filesystem::symlink_status(temporary, temporary_error).type()
        != std::filesystem::file_type::not_found)
        return;
    if (write_text_file(
            temporary,
            json { { "status", "clean" },
                   { "digest", digest },
                   { "payload", payload } }
                .dump(),
            &error
        )) {
        std::error_code ignored;
        std::filesystem::rename(temporary, file, ignored);
        std::filesystem::remove(temporary, ignored);
    }
}

// One immutable before/after snapshot covers both analysis engines. Writes are
// provisional until the complete batch confirms that its inputs stayed stable.
class input_batch {
public:
    explicit input_batch(
        const std::filesystem::path& root, bool supported_profile = true
    )
        : root_(root)
        , keys_(supported_profile ? fresh_input_keys(root) : json::object())
        , previous_(active_) {
        active_ = this;
    }

    ~input_batch() { active_ = previous_; }

    input_batch(const input_batch&) = delete;
    input_batch& operator=(const input_batch&) = delete;

    static input_batch* active() { return active_; }

    [[nodiscard]] const json& keys() const { return keys_; }

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

    void defer(const std::filesystem::path& file, const json& payload) {
        pending_[file] = payload;
    }

    void publish(const json& final_keys) {
        for (const auto& [file, payload] : pending_) {
            const auto kind
                = file.filename().string().starts_with("ast-") ? "ast" : "tidy";
            for (auto row = final_keys.begin(); row != final_keys.end(); ++row)
                if (cache_file(root_, final_keys, row.key(), kind) == file) {
                    publish_success(file, payload);
                    break;
                }
        }
    }

private:
    std::filesystem::path root_;
    json keys_;
    input_batch* previous_;
    std::map<std::filesystem::path, json> pending_;
    static inline thread_local input_batch* active_ = nullptr;
};

inline json input_keys(const std::filesystem::path& root) {
    const auto* batch = input_batch::active();
    return batch != nullptr && batch->root() == root ? batch->keys()
                                                     : fresh_input_keys(root);
}

inline void
write_success(const std::filesystem::path& file, const json& payload) {
    if (file.empty())
        return;
    auto* batch = input_batch::active();
    if (batch != nullptr)
        batch->defer(file, payload);
    else
        publish_success(file, payload);
}

inline double elapsed(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now() - start
    )
        .count();
}

} // namespace ecosystem::verification_detail
