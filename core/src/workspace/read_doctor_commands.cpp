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
        out << "artifact: " << format_artifact_ref(*requested_artifact) << "\n";
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

    if (profile) {
        const bool build_profile
            = contains_string(known_build_profiles, *profile);
        const bool check_profile
            = contains_string(known_check_profiles, *profile);
        if (!build_profile && !check_profile) {
            print_error(
                err, command_error::invalid_request,
                "unknown profile: " + *profile
            );
            err << "valid profiles: "
                << join_strings(supported_build_profiles(manifest_value), " ")
                << " "
                << join_strings(supported_check_profiles(manifest_value), " ")
                << "\n";
            return command_error::invalid_request;
        }
        if ((build_profile && !supports_build_profile(manifest_value, *profile))
            || (check_profile
                && !supports_check_profile(manifest_value, *profile))) {
            print_error(
                err, command_error::unsupported_by_manifest,
                "manifest does not support "
                    + std::string(build_profile ? "build" : "check")
                    + " profile " + *profile
            );
            err << "valid " << (build_profile ? "build" : "check")
                << " profiles: "
                << join_strings(
                       build_profile ? supported_build_profiles(manifest_value)
                                     : supported_check_profiles(manifest_value),
                       " "
                   )
                << "\n";
            return command_error::unsupported_by_manifest;
        }
        if ((*profile == "java" || *profile == "sphinx")
            && requested_artifact) {
            print_error(
                err, command_error::invalid_request,
                *profile + " check does not support artifact filters"
            );
            return command_error::invalid_request;
        }
        if (*profile == "sphinx" && !project_has_docs_surface(project_root)) {
            print_error(
                err, command_error::unsupported_by_manifest,
                "project does not define docs/index.md or docs/index.rst for "
                "sphinx"
            );
            return command_error::unsupported_by_manifest;
        }
        if ((*profile == "java"
             || (*profile == "ci" && !requested_artifact
                 && project_supports_java_check(project_root, manifest_value)))
            && (!has_java_gradle_surface(project_root)
                || artifact_refs_for_kind(manifest_value, "shared_lib")
                       .empty())) {
            print_error(
                err, command_error::unsupported_by_manifest,
                "java checks require a java/ Gradle test surface and a shared "
                "library artifact"
            );
            return command_error::unsupported_by_manifest;
        }
        if ((*profile == "tests" || *profile == "coverage"
             || *profile == "leaks"
             || (*profile == "ci" && has_tests_enabled(manifest_value)))
            && collect_test_targets(manifest_value, requested_artifact)
                   .empty()) {
            print_error(
                err, command_error::unsupported_by_manifest,
                "no test targets resolve for the requested artifact"
            );
            return command_error::unsupported_by_manifest;
        }
    }

    auto tools = toolchains_report(profile.value_or("debug"), project_root);
    if (profile == "ci") {
        if (!has_tests_enabled(manifest_value))
            tools.erase("ctest");
        if (!requested_artifact
            && project_supports_java_check(project_root, manifest_value))
            tools.update(toolchains_report("java", project_root));
    }
    bool missing_tools = false;
    out << "local tools:\n";
    for (const auto& [name, tool] : tools.items()) {
        const bool available = tool.at("available").get<bool>();
        const bool required = tool.at("required").get<bool>();
        const bool probe_failed
            = available && tool.at("version_exit_code") != 0;
        out << "  " << tool.at("label").get<std::string>() << ": "
            << (!available         ? "missing"
                    : probe_failed ? "version probe failed"
                                   : "ok")
            << (required ? " [required]" : " [optional]") << "\n";
        if (available) {
            out << "    path: " << tool.at("path").get<std::string>() << "\n";
            const auto version = tool.at("version").get<std::string>();
            out << "    version: "
                << (version.empty() ? "unavailable" : version)
                << " (probe exit " << tool.at("version_exit_code") << ")\n";
            if (probe_failed)
                out << "    " << tool.at("version_error").get<std::string>()
                    << "\n";
        }
        missing_tools
            = missing_tools || (required && (!available || probe_failed));
        if (name == "clang")
            out << "    clang++ is the C-compiler fallback when clang is "
                   "absent\n";
    }
    if (tools.empty())
        out << "  no external tools required by this profile\n";
    if (profile == "leaks")
        out << "leak environment: working ASan/UBSan/LSan runtimes and "
               "untraced execution required; "
               "availability is not a sanitizer runtime test\n";
    if (profile == "sphinx")
        out << "documentation environment: sphinx_rtd_theme and, for Markdown, "
               "myst_parser "
               "must be importable by sphinx-build\n";
    if (profile == "java")
        out << "Java environment: JDK/JNI development files and the selected "
               "Gradle runtime required\n";
    if (profile == "android") {
        const auto environment = detect_android_environment();
        out << "android environment:\n"
            << android_environment_report(environment).dump(2) << "\n";
        missing_tools = missing_tools || !environment.errors.empty();
    }
    if (missing_tools) {
        emit_declared_dependencies(
            out, manifest_value, requested_artifact, profile
        );
        print_error(
            err, command_error::missing_local_tooling,
            "required local tooling or environment is unavailable; see the "
            "missing entries above"
        );
        return command_error::missing_local_tooling;
    }

    const dependency_summary dependencies
        = summarize_dependencies(manifest_value, requested_artifact);
    command_error status
        = missing_files.empty() && tracked_surface_issues.empty()
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
    if (status == command_error::ok && tools.contains("cmake")) {
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
    emit_declared_dependencies(
        out, manifest_value, requested_artifact, profile
    );
    if (missing_files.empty() && tracked_surface_issues.empty()) {
        emit_configured_package_state(
            out, project_root, manifest_value, requested_artifact, cache_status,
            configured_package_state_refreshed
        );
    }

    return status;
}

command_error run_workspace_doctor(
    const workspace_context& workspace,
    const std::optional<std::string>& profile, const workspace_scope& scope,
    std::ostream& out, std::ostream& err
) {
    const command_error validity
        = validate_workspace_scope(workspace, scope, err);
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

} // namespace ecosystem::command_support

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

} // namespace ecosystem
