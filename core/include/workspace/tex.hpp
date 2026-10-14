#pragma once

#include "workspace/tooling/process.hpp"

#include <iosfwd>

namespace ecosystem {

/** Select root-level TeX documents; nested files remain supporting inputs. */
std::vector<std::filesystem::path> tex_sources(const std::filesystem::path& root
);
command_error build_tex(
    const std::filesystem::path& project, std::ostream& out, std::ostream& err
);

} // namespace ecosystem
