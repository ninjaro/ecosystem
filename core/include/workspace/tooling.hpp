#pragma once

#include "workspace/tooling/android.hpp"
#include "workspace/tooling/build.hpp"
#include "workspace/tooling/process.hpp"
#include "workspace/tooling/sphinx.hpp"

namespace ecosystem {

std::filesystem::path local_state_dir(const std::filesystem::path& project_root
);
std::filesystem::path
local_developer_source_dir(const std::filesystem::path& project_root);
std::filesystem::path
local_developer_cmakelists_path(const std::filesystem::path& project_root);
std::filesystem::path local_report_dir(const std::filesystem::path& project_root
);
std::filesystem::path
local_benchmark_dir(const std::filesystem::path& project_root);
std::filesystem::path local_sphinx_dir(const std::filesystem::path& project_root
);
bool write_text_file(
    const std::filesystem::path& path, const std::string& contents,
    std::string* error_message
);
std::string
read_text_file(const std::filesystem::path& path, std::string* error_message);
void ensure_local_artifacts(
    const std::filesystem::path& project_root, bool with_clang_format,
    bool with_clang_tidy
);

} // namespace ecosystem
