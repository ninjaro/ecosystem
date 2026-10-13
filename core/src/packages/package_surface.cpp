#include "packages/package_surface.hpp"

#include "packages/package_metadata.hpp"

#include <sstream>
#include <string>

namespace ecosystem {
namespace package_surface_support {

    void write_package_section(std::ostringstream* stream, bool* has_output) {
        if (*has_output) {
            *stream << "\n";
        }
        *has_output = true;
    }

} // namespace package_surface_support

using namespace package_surface_support;

std::string render_package_surface(
    const dependency_summary& dependencies,
    const package_surface_options& options
) {
    std::ostringstream stream;
    bool has_output = false;

    for (const package_surface_block block :
         enabled_surface_blocks(dependencies)) {
        const std::string block_contents
            = render_package_surface_block(block, dependencies, options);
        if (block_contents.empty()) {
            continue;
        }
        write_package_section(&stream, &has_output);
        stream << block_contents;
    }
    if (options.enable_testing) {
        write_package_section(&stream, &has_output);
        stream << "enable_testing()\n";
    }

    if (!has_output) {
        return {};
    }
    stream << "\n";
    return stream.str();
}

} // namespace ecosystem
