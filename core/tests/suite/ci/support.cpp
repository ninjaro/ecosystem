#include "../ci.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

std::string ci_shell(
    const std::string& yaml, const std::string& step_name,
    const std::size_t indentation
) {
    const auto step = yaml.find("- name: " + step_name + "\n");
    require_true(step != std::string::npos, "missing CI step: " + step_name);
    const std::string prefix(indentation, ' ');
    const std::string marker = prefix + "run: |\n";
    const auto start = yaml.find(marker, step);
    require_true(start != std::string::npos, "missing CI shell program");
    std::istringstream lines(yaml.substr(start + marker.size()));
    std::string program, line;
    while (std::getline(lines, line)) {
        if (!line.empty() && !line.starts_with(prefix + "  "))
            break;
        program += (line.empty() ? line : line.substr(indentation + 2)) + "\n";
    }
    return program;
}

std::string ci_bind(
    std::string text, const std::string& expression, const std::string& value
) {
    const auto token = "${{ " + expression + " }}";
    std::size_t offset = 0;
    while ((offset = text.find(token, offset)) != std::string::npos) {
        text.replace(offset, token.size(), value);
        offset += value.size();
    }
    return text;
}

} // namespace ecosystem_test_support
