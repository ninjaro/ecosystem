#pragma once

#include "manifest.hpp"

#include <filesystem>

namespace ecosystem {

string_list
forbidden_repository_entries(const std::filesystem::path& project_root);

} // namespace ecosystem
