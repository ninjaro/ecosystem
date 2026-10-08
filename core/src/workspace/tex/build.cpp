#include "internal.hpp"

#include "../command_internal.hpp"
#include "workspace/tex.hpp"
#include "workspace/tooling.hpp"

#include <fstream>
#include <ostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ecosystem {

command_error
build_tex(const fs::path& project, std::ostream& out, std::ostream& err) {
    const auto root = fs::absolute(project);
    bool staged = false;
    const auto pending = root / ".ecosystem/tex/pending";
    try {
        const auto sources = tex_sources(root);
        if (sources.empty()) {
            command_support::print_error(
                err, command_error::unsupported_by_manifest,
                "project has no root-level tex/*.tex documents"
            );
            return command_error::unsupported_by_manifest;
        }
        auto documents = tex_detail::prepare_documents(root, sources);
        const auto tools = toolchains_report("tex", root);
        for (const auto& [name, tool] : tools.items()) {
            if (!tool.at("available").get<bool>()
                || tool.at("version_exit_code") != 0) {
                command_support::print_error(
                    err, command_error::missing_local_tooling,
                    name + " is unavailable or its version probe failed: "
                        + tool.at("version_error").get<std::string>()
                );
                return command_error::missing_local_tooling;
            }
        }
        fs::create_directories(pending.parent_path());
        if (!fs::create_directory(pending))
            throw std::runtime_error(
                "TeX recovery exists: " + pending.string()
            );
        staged = true;
        scoped_command_log log(pending.parent_path() / "build.log");
        if (!log.error().empty())
            throw std::runtime_error(log.error());
        std::size_t position = 0;
        for (auto& document : documents) {
            const auto output = pending / std::to_string(position++);
            fs::create_directory(output);
            // TeX needs matching output directories for nested include .aux
            // files.
            for (const auto& input :
                 fs::recursive_directory_iterator(root / "tex"))
                if (input.is_directory())
                    fs::create_directories(
                        output / input.path().lexically_relative(root / "tex")
                    );
            const auto result = capture_command_result(
                { tools.at("latexmk").at("path").get<std::string>(), "-norc",
                  "-pdf", "-interaction=nonstopmode", "-halt-on-error",
                  "-no-shell-escape", "-outdir=" + output.string(),
                  "./" + document.source.filename().string() },
                root / "tex"
            );
            if (result.exit_code != 0)
                throw std::runtime_error(
                    "TeX failed for " + document.source.filename().string()
                    + " (exit " + std::to_string(result.exit_code)
                    + "); see .ecosystem/tex/build.log"
                );
            document.staged
                = output / (document.source.stem().string() + ".pdf");
            tex_detail::check_path(
                root, document.staged.lexically_relative(root)
            );
            if (!fs::is_regular_file(document.staged)
                || fs::file_size(document.staged) <= 5)
                throw std::runtime_error(
                    "TeX did not produce a fresh PDF for "
                    + document.source.filename().string()
                );
            std::ifstream pdf(document.staged, std::ios::binary);
            char header[5] {};
            pdf.read(header, 5);
            if (std::string(header, 5) != "%PDF-")
                throw std::runtime_error(
                    "TeX produced an invalid PDF for "
                    + document.source.filename().string()
                );
        }
        tex_detail::publish_documents(root, documents);
        fs::remove_all(pending);
        for (const auto& document : documents)
            out << "generated assets/showcase/" << document.entry.path << "\n";
        out << "TeX log: .ecosystem/tex/build.log\n";
        return command_error::ok;
    } catch (const std::exception& failure) {
        if (staged) {
            std::error_code ignored;
            fs::remove_all(pending, ignored);
        }
        command_support::print_error(
            err, command_error::task_failed, failure.what()
        );
        return command_error::task_failed;
    }
}

} // namespace ecosystem
