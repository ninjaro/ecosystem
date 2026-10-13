#pragma once

#include <string_view>

namespace ecosystem {

enum class package_group {
    required,
    profile_kde,
    optional,
};

std::string_view package_group_heading(package_group group);

} // namespace ecosystem
