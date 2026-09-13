#pragma once

#include "analysis/clang.hpp"

#include <filesystem>
#include <optional>

namespace ecosystem {

struct tidy_check_report {
    cxx_analysis_report analysis;
    bool compilation_database_available = false;
    std::string compilation_database;
    bool clang_tidy_available = false;
    std::string clang_tidy_path;
    bool clang_tidy_used = false;
    int clang_tidy_exit_code = 0;
    string_list clang_tidy_output;
    std::string clang_tidy_skip_reason;
};

tidy_check_report run_tidy_check(
    const manifest& value, const std::filesystem::path& project_root,
    const std::optional<artifact_ref>& requested_artifact, bool include_tests,
    bool include_benchmarks, const std::string& profile
);

json to_json(const tidy_check_report& value);

}  // namespace ecosystem
