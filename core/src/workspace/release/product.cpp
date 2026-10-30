#include "internal.hpp"

#include <cctype>

namespace ecosystem::release_detail {
const std::regex semver_pattern(R"(^[0-9]+\.[0-9]+\.[0-9]+$)");

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

std::string package_token(const std::string& value) {
    std::string normalized;
    bool last_was_dash = false;
    for (const char character : value) {
        const auto raw = static_cast<unsigned char>(character);
        if (std::isalnum(raw)) {
            normalized.push_back(static_cast<char>(std::tolower(raw)));
            last_was_dash = false;
            continue;
        }
        if (!last_was_dash) {
            normalized.push_back('-');
            last_was_dash = true;
        }
    }

    while (!normalized.empty() && normalized.front() == '-') {
        normalized.erase(normalized.begin());
    }
    while (!normalized.empty() && normalized.back() == '-') {
        normalized.pop_back();
    }
    if (normalized.empty()) {
        return "package";
    }
    return normalized;
}

std::string package_description(
    const manifest& manifest_value, const resolved_artifact& resolved
) {
    const std::optional<artifact_ref> facade_ref
        = parse_artifact_ref(manifest_value.facade_entry_artifact);
    if (facade_ref.has_value()
        && format_artifact_ref(*facade_ref)
            == format_artifact_ref(resolved.ref)) {
        return manifest_value.description;
    }
    return manifest_value.description + " (" + format_artifact_ref(resolved.ref)
        + ")";
}

} // namespace ecosystem::release_detail

namespace ecosystem {
using namespace release_detail;

fs::path local_prerelease_dir(const fs::path& project_root) {
    return local_state_dir(project_root) / "prerelease";
}

std::string prerelease_package_name(
    const manifest& manifest_value, const artifact_ref& ref,
    const artifact& artifact_value
) {
    const std::optional<artifact_ref> facade_ref
        = parse_artifact_ref(manifest_value.facade_entry_artifact);
    std::string project_name = package_token(manifest_value.id);
    if (facade_ref.has_value()
        && format_artifact_ref(*facade_ref) == format_artifact_ref(ref)) {
        return project_name;
    }
    return project_name + "-" + package_token(ref.component_id) + "-"
        + package_token(artifact_value.id);
}

} // namespace ecosystem
