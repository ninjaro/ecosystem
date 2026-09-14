#pragma once

#include "manifest.hpp"
#include "workspace/tooling.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace ecosystem {

struct source_dependency {
    std::string repository;
    std::string revision;
    std::string package;
    artifact_ref artifact;
};

std::optional<source_dependency> source_dependency_for(const component& value);
std::string source_dependency_override(const source_dependency& value);

// Build and install independent providers before configuring the consumer.
// The returned options identify installed CMake packages, never build
// artifacts.
command_error prepare_source_dependencies(
    const std::filesystem::path& project_root, const manifest& value,
    const std::string& profile, string_list* cmake_options,
    std::string* error_message
);

} // namespace ecosystem
