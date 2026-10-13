#include "packages/package_group.hpp"

namespace ecosystem {

std::string_view package_group_heading(const package_group group) {
    switch (group) {
    case package_group::required:
        return "required";
    case package_group::profile_kde:
        return "profile kde";
    case package_group::optional:
        return "optional";
    }

    return "unknown";
}

} // namespace ecosystem
