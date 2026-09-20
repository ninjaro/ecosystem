#include "workspace/read_commands.hpp"

#include "command_internal.hpp"

#include <optional>
#include <sstream>
#include <string>

namespace ecosystem::command_support {

command_error run_doctor(
    const fs::path& project_root, const manifest& manifest_value,
    std::optional<std::string> profile,
    std::optional<artifact_ref> requested_artifact, std::ostream& out,
    std::ostream& err
) {
    const string_list missing_files
        = missing_declared_files(manifest_value, project_root);
    const string_list tracked_surface_issues
        = tracked_surface_drift(project_root, manifest_value);
    if (profile.has_value()) {
        out << "profile: " << *profile << "\n";
    }
    if (requested_artifact.has_value()) {
        out << "artifact: " << format_artifact_ref(*requested_artifact)
            << "\n";
        if (!resolve_artifact(manifest_value, requested_artifact).has_value()) {
            print_error(
                err, command_error::invalid_request, "artifact does not exist"
            );
            return command_error::invalid_request;
        }
    }

    if (!missing_files.empty()) {
        err << "declared files missing:\n";
        for (const std::string& file : missing_files) {
            err << "  " << file << "\n";
        }
    }
    if (!tracked_surface_issues.empty()) {
        err << "tracked surfaces out of sync:\n";
        for (const std::string& issue : tracked_surface_issues) {
            err << "  " << issue << "\n";
        }
    }

    if (profile.has_value()) {
        if (contains_string(known_build_profiles, *profile)) {
            if (!supports_build_profile(manifest_value, *profile)) {
                print_error(
                    err, command_error::unsupported_by_manifest,
                    "manifest does not support build profile " + *profile
                );
                err << "valid build profiles:";
                for (const std::string& supported :
                     supported_build_profiles(manifest_value)) {
                    err << " " << supported;
                }
                err << "\n";
                return command_error::unsupported_by_manifest;
            }
            const tool_status cmake_tool = probe_tool("cmake");
            const tool_status clang_tool = probe_tool("clang++");
            out << "cmake: " << (cmake_tool.available ? "ok" : "missing")
                << "\n";
            out << "clang++: " << (clang_tool.available ? "ok" : "missing")
                << "\n";
            if (!cmake_tool.available || !clang_tool.available) {
                return command_error::missing_local_tooling;
            }
            if (*profile == "android") {
                const auto environment = detect_android_environment();
                out << "android environment:\n"
                    << android_environment_report(environment).dump(2) << "\n";
                if (!environment.errors.empty())
                    return command_error::missing_local_tooling;
            }
        } else if (contains_string(known_check_profiles, *profile)) {
            if (!supports_check_profile(manifest_value, *profile)) {
                print_error(
                    err, command_error::unsupported_by_manifest,
                    "manifest does not support check profile " + *profile
                );
                err << "valid check profiles:";
                for (const std::string& supported :
                     supported_check_profiles(manifest_value)) {
                    err << " " << supported;
                }
                err << "\n";
                return command_error::unsupported_by_manifest;
            }
            if (*profile == "format" || *profile == "ci") {
                const tool_status format_tool = probe_tool("clang-format");
                out << "clang-format: "
                    << (format_tool.available ? "ok" : "missing") << "\n";
                if (!format_tool.available) {
                    return command_error::missing_local_tooling;
                }
            }
            if (*profile == "doxy") {
                const tool_status doxygen_tool = probe_tool("doxygen");
                out << "doxygen: "
                    << (doxygen_tool.available ? "ok" : "missing") << "\n";
                const tool_status dot_tool = probe_tool("dot", { "-V" });
                out << "Graphviz dot: "
                    << (dot_tool.available ? "ok" : "missing") << "\n";
                if (!doxygen_tool.available || !dot_tool.available) {
                    return command_error::missing_local_tooling;
                }
            }
            if (*profile == "sphinx") {
                if (!project_has_docs_surface(project_root)) {
                    print_error(
                        err, command_error::unsupported_by_manifest,
                        "project does not define docs/index.md or "
                        "docs/index.rst for sphinx"
                    );
                    return command_error::unsupported_by_manifest;
                }
                const tool_status sphinx_tool = probe_tool("sphinx-build");
                out << "sphinx-build: "
                    << (sphinx_tool.available ? "ok" : "missing") << "\n";
                if (!sphinx_tool.available) {
                    return command_error::missing_local_tooling;
                }
            }
            if (*profile == "tests" || *profile == "coverage"
                || *profile == "ci") {
                const tool_status ctest_tool = probe_tool("ctest");
                out << "ctest: "
                    << (ctest_tool.available ? "ok" : "missing") << "\n";
                if (!ctest_tool.available) {
                    return command_error::missing_local_tooling;
                }
            }
            if (*profile == "coverage") {
                const tool_status cov_tool = probe_tool("llvm-cov");
                const tool_status prof_tool = probe_tool("llvm-profdata");
                out << "llvm-cov: "
                    << (cov_tool.available ? "ok" : "missing") << "\n";
                out << "llvm-profdata: "
                    << (prof_tool.available ? "ok" : "missing") << "\n";
                if (!cov_tool.available || !prof_tool.available) {
                    return command_error::missing_local_tooling;
                }
            }
        } else {
            print_error(
                err, command_error::invalid_request,
                "unknown profile: " + *profile
            );
            return command_error::invalid_request;
        }
    }

    const dependency_summary dependencies
        = summarize_dependencies(manifest_value, requested_artifact);
    command_error status = missing_files.empty() && tracked_surface_issues.empty()
        ? command_error::ok
        : command_error::invalid_request;
    build_cache_status cache_status = inspect_build_cache(
        project_root, doctor_cache_profile(profile),
        {
            project_root / "manifest.json",
            local_developer_cmakelists_path(project_root),
        }
    );
    bool configured_package_state_refreshed = false;
    if (status == command_error::ok) {
        const doctor_cache_refresh_result refresh_result = refresh_doctor_cache(
            project_root, manifest_value, dependencies, requested_artifact,
            profile
        );
        if (refresh_result.status != command_error::ok
            && !refresh_result.error_message.empty()) {
            print_error(
                err, refresh_result.status, refresh_result.error_message
            );
        }
        cache_status = refresh_result.cache_status;
        configured_package_state_refreshed = refresh_result.refreshed;
        status = combine_status(status, refresh_result.status);
    }

    out << "supported build profiles:";
    for (const std::string& supported :
         supported_build_profiles(manifest_value)) {
        out << " " << supported;
    }
    out << "\n";
    out << "supported check profiles:";
    for (const std::string& supported :
         supported_check_profiles(manifest_value)) {
        out << " " << supported;
    }
    out << "\n";
    emit_declared_dependencies(out, manifest_value, requested_artifact, profile);
    if (missing_files.empty() && tracked_surface_issues.empty()) {
        emit_configured_package_state(
            out, project_root, manifest_value, requested_artifact,
            cache_status, configured_package_state_refreshed
        );
    }

    return status;
}

command_error run_workspace_doctor(
    const workspace_context& workspace,
    const std::optional<std::string>& profile, const workspace_scope& scope,
    std::ostream& out, std::ostream& err
) {
    const command_error validity = validate_workspace_scope(workspace, scope, err);
    if (validity != command_error::ok) {
        return validity;
    }

    command_error status = command_error::ok;
    for (const workspace_project* project :
         selected_workspace_projects(workspace, scope)) {
        const std::vector<artifact_ref> requested_artifacts
            = workspace_artifacts_for_project(scope, project);
        std::ostringstream project_out;
        std::ostringstream project_err;
        if (requested_artifacts.empty()) {
            status = combine_status(
                status,
                command_support::run_doctor(
                    project->root, *project->manifest_value, profile,
                    std::nullopt, project_out, project_err
                )
            );
        } else {
            for (const artifact_ref& requested_artifact : requested_artifacts) {
                status = combine_status(
                    status,
                    command_support::run_doctor(
                        project->root, *project->manifest_value, profile,
                        requested_artifact, project_out, project_err
                    )
                );
                ensure_stream_trailing_newline(&project_out);
                ensure_stream_trailing_newline(&project_err);
            }
        }
        emit_workspace_project_output(
            workspace, project, project_out, project_err, out, err
        );
    }
    return status;
}

command_error parse_workspace_doctor_request(
    const workspace_context& workspace, const string_list& args,
    std::optional<std::string>* profile, workspace_scope* scope,
    std::ostream& err
) {
    string_list scope_args;
    for (std::size_t index = 0U; index < args.size(); ++index) {
        if (args[index] == "--profile") {
            if (index + 1U >= args.size()) {
                print_error(
                    err, command_error::invalid_request,
                    "--profile requires a value"
                );
                return command_error::invalid_request;
            }
            if (profile->has_value()) {
                print_error(
                    err, command_error::invalid_request,
                    "doctor accepts at most one profile"
                );
                return command_error::invalid_request;
            }
            *profile = args[index + 1U];
            ++index;
            continue;
        }
        if (args[index] == "--project") {
            scope_args.push_back(args[index]);
            if (index + 1U >= args.size()) {
                print_error(
                    err, command_error::invalid_request,
                    "--project requires a value"
                );
                return command_error::invalid_request;
            }
            scope_args.push_back(args[index + 1U]);
            ++index;
            continue;
        }
        if (args[index] == "--group") {
            scope_args.push_back(args[index]);
            if (index + 1U >= args.size()) {
                print_error(
                    err, command_error::invalid_request,
                    "--group requires a value"
                );
                return command_error::invalid_request;
            }
            scope_args.push_back(args[index + 1U]);
            ++index;
            continue;
        }
        if (args[index].find(':') != std::string::npos) {
            scope_args.push_back(args[index]);
            continue;
        }
        if (profile->has_value()) {
            print_error(
                err, command_error::invalid_request,
                "doctor accepts at most one profile"
            );
            return command_error::invalid_request;
        }
        *profile = args[index];
    }

    return parse_workspace_scope(workspace, scope_args, true, scope, err);
}

}  // namespace ecosystem::command_support

namespace ecosystem {

command_error run_doctor(
    const std::filesystem::path& project_root, const manifest& manifest_value,
    std::optional<std::string> profile,
    std::optional<artifact_ref> requested_artifact, std::ostream& out,
    std::ostream& err
) {
    return command_support::run_doctor(
        project_root, manifest_value, profile, requested_artifact, out, err
    );
}

command_error run_workspace_doctor(
    const workspace_context& workspace,
    const std::optional<std::string>& profile, const workspace_scope& scope,
    std::ostream& out, std::ostream& err
) {
    return command_support::run_workspace_doctor(
        workspace, profile, scope, out, err
    );
}

command_error parse_workspace_doctor_request(
    const workspace_context& workspace, const string_list& args,
    std::optional<std::string>* profile, workspace_scope* scope,
    std::ostream& err
) {
    return command_support::parse_workspace_doctor_request(
        workspace, args, profile, scope, err
    );
}

}  // namespace ecosystem
