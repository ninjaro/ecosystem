#include "../release.hpp"

namespace ecosystem_test_support {
json prepared_input(const fs::path& project, const cli_result& result) {
    require_true(
        result.exit_code == 0, "preparation must succeed: " + result.output
    );
    const std::string prefix = "prepared release input: ";
    const auto start = result.output.find(prefix);
    require_true(
        start != std::string::npos, "preparation reports its exact handoff"
    );
    const auto path = result.output.substr(
        start + prefix.size(),
        result.output.find('\n', start) - start - prefix.size()
    );
    return json::parse(read_text(project / path));
}

fs::path prepared_payload(const json& input) {
    const auto& first = input.at("distribution").at("files").at(0);
    auto path = fs::path(input.at("project").at("root").get<std::string>())
        / first.at("source").get<std::string>();
    for (const auto& part :
         fs::path(first.at("install_path").get<std::string>())) {
        (void)part;
        path = path.parent_path();
    }
    return path;
}

fs::path prepare_fixture(
    const fs::path& project, const ecosystem::manifest& value,
    const ecosystem::resolved_artifact& primary, const fs::path& binary
) {
    std::string error;
    require_true(
        ecosystem::save_manifest(project / "manifest.json", value, &error),
        error
    );
    const auto attempts = ecosystem::local_prerelease_dir(project) / "attempts";
    fs::path target;
    for (int n = 0; n < 128; ++n) {
        const auto next
            = attempts / ("fixture-" + std::to_string(n)) / "prepared";
        if (!fs::exists(next)) {
            target = next;
            break;
        }
    }
    require_true(!target.empty(), "fixture attempt capacity");
    auto output = binary;
    const auto canonical
        = ecosystem::local_build_dir(project, "release")
        / ecosystem::artifact_output_name(
              value, *primary.component_value, *primary.artifact_value
        );
    if (output != canonical) {
        fs::create_directories(canonical.parent_path());
        fs::copy_file(output, canonical, fs::copy_options::overwrite_existing);
        output = canonical;
    }
    const auto digest
        = ecosystem::prepared_file_sha256(project / "manifest.json", &error);
    fs::path input;
    const auto result = ecosystem::export_prepared_release_input(
        project, value, primary, output, target, digest, std::nullopt, &input,
        &error
    );
    require_true(result == ecosystem::command_error::ok, error);
    return input;
}
} // namespace ecosystem_test_support
