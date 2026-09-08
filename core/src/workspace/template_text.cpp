#include "workspace/template_text.hpp"

#include "workspace/tooling.hpp"

#include <algorithm>

#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>

namespace fs = std::filesystem;

namespace ecosystem {
namespace template_text_support {

std::string env_or_empty(const char *name) {
  const char *value = std::getenv(name);
  return value == nullptr ? std::string() : std::string(value);
}

bool path_exists(const fs::path &path) {
  std::error_code error;
  return fs::exists(path, error) && !error;
}

std::string trim_copy(const std::string &value) {
  const std::size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const std::size_t last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1U);
}

std::string replace_all(std::string value, const std::string &needle,
                        const std::string &replacement) {
  std::size_t offset = 0U;
  while ((offset = value.find(needle, offset)) != std::string::npos) {
    value.replace(offset, needle.size(), replacement);
    offset += replacement.size();
  }
  return value;
}

std::string unresolved_placeholder(const std::string &value) {
  std::size_t start = value.find("{{");
  while (start != std::string::npos) {
    if (start > 0U && value[start - 1U] == '$') {
      start = value.find("{{", start + 2U);
      continue;
    }
    const std::size_t end = value.find("}}", start + 2U);
    if (end == std::string::npos) {
      return trim_copy(value.substr(start));
    }
    return value.substr(start, end - start + 2U);
  }
  return {};
}

fs::path source_root_from_this_file() {
  fs::path current = fs::path(__FILE__).lexically_normal().parent_path();
  while (!current.empty()) {
    if (path_exists(current / "manifest.json")) {
      return current;
    }
    if (current == current.root_path()) {
      break;
    }
    current = current.parent_path();
  }
  return fs::path(__FILE__)
      .lexically_normal()
      .parent_path()
      .parent_path()
      .parent_path()
      .parent_path()
      .parent_path();
}

void append_unique_path(std::vector<fs::path> *paths, std::set<std::string> *seen,
                        const fs::path &path) {
  const std::string key = path.lexically_normal().generic_string();
  if (!key.empty() && seen->insert(key).second) {
    paths->push_back(path);
  }
}

void append_parent_template_roots(std::vector<fs::path> *paths,
                                  std::set<std::string> *seen,
                                  const fs::path &start_path) {
  fs::path current = start_path.parent_path();
  while (!current.empty()) {
    append_unique_path(paths, seen, current / "templates");
    if (current == current.root_path()) {
      break;
    }
    current = current.parent_path();
  }
}

std::vector<fs::path>
template_root_candidates(const bool actor_contract = false) {
    std::vector<fs::path> candidates;
    std::set<std::string> seen;
    std::string env_root = env_or_empty("MANIFESTO_TEMPLATE_ROOT");
    if (env_root.empty()) {
        env_root = env_or_empty("ECOSYSTEM_TEMPLATE_ROOT");
    }
    if (!env_root.empty()) {
        append_unique_path(&candidates, &seen, fs::path(env_root));
    }

    const fs::path source_root = source_root_from_this_file();
    // CI adapters must agree with the actor contract in this tool version.
    if (actor_contract) {
        append_unique_path(&candidates, &seen, source_root / "templates");
    }
    append_parent_template_roots(&candidates, &seen, source_root);
    append_parent_template_roots(&candidates, &seen, fs::current_path());
    append_unique_path(&candidates, &seen, fs::current_path() / "templates");
    append_unique_path(&candidates, &seen, source_root / "templates");
    return candidates;
}

} // namespace template_text_support

using namespace template_text_support;

fs::path template_root_path() {
  for (const fs::path &candidate : template_root_candidates()) {
    if (path_exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

fs::path locate_template_path(const std::vector<fs::path> &relative_paths) {
    const bool actor_contract = std::any_of(
        relative_paths.begin(), relative_paths.end(), [](const fs::path& path) {
            const std::string text = path.generic_string();
            return text.starts_with(".github/")
                || text.starts_with("tracked/.github/");
        }
    );
    for (const fs::path& root : template_root_candidates(actor_contract)) {
        if (!path_exists(root)) {
            continue;
        }
        for (const fs::path& relative_path : relative_paths) {
            const fs::path candidate = root / relative_path;
            if (path_exists(candidate)) {
                return candidate;
            }
        }
    }
    return {};
}

std::string render_text_template_candidates(
    const std::vector<fs::path> &relative_paths,
    const template_bindings &bindings, std::string *error_message) {
  const fs::path template_path = locate_template_path(relative_paths);
  if (template_path.empty()) {
    if (error_message != nullptr) {
      *error_message = "unable to locate manifesto template file";
    }
    return {};
  }

  std::error_code status_error;
  if (!fs::is_regular_file(template_path, status_error) || status_error) {
      if (error_message != nullptr) {
          *error_message = "template is not a readable regular file: "
              + template_path.generic_string();
      }
      return {};
  }

  std::string read_error;
  std::string contents = read_text_file(template_path, &read_error);
  if (!read_error.empty()) {
    if (error_message != nullptr) {
      *error_message = read_error;
    }
    return {};
  }

  for (const auto &[name, value] : bindings) {
    contents = replace_all(contents, "{{" + name + "}}", value);
  }

  const std::string unresolved = unresolved_placeholder(contents);
  if (!unresolved.empty()) {
    if (error_message != nullptr) {
      *error_message = "unresolved template placeholder " + unresolved + " in "
          + template_path.generic_string();
    }
    return {};
  }

  return contents;
}

std::string render_text_template(const fs::path &relative_path,
                                 const template_bindings &bindings,
                                 std::string *error_message) {
  return render_text_template_candidates({relative_path}, bindings,
                                         error_message);
}

std::string render_required_text_template(
    const fs::path& relative_path, const template_bindings& bindings
) {
    std::string error_message;
    const std::string contents
        = render_text_template(relative_path, bindings, &error_message);
    if (!error_message.empty()) {
        throw template_render_error(
            relative_path.generic_string() + ": " + error_message
        );
    }
    return contents;
}

} // namespace ecosystem
