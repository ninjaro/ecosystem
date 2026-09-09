#include "workspace/template_text.hpp"

#include "workspace/tooling.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

fs::path executable_path() {
    std::error_code error;
#if defined(__linux__)
    return fs::read_symlink("/proc/self/exe", error);
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size);
    if (_NSGetExecutablePath(path.data(), &size) == 0)
        return fs::weakly_canonical(path.data(), error);
#elif defined(_WIN32)
    std::vector<wchar_t> path(32768);
    const auto size = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size())
    );
    if (size > 0 && size < path.size())
        return fs::weakly_canonical(std::wstring(path.data(), size), error);
#endif
    return {};
}

fs::path source_template_root() {
#ifdef MANIFESTO_SOURCE_TEMPLATE_ROOT
    return MANIFESTO_SOURCE_TEMPLATE_ROOT;
#else
    // Allows bootstrapping a newly generated build with an older CMake surface.
    return fs::path(__FILE__)
               .parent_path()
               .parent_path()
               .parent_path()
               .parent_path()
        / "templates";
#endif
}

fs::path version_template_root() {
#ifdef MANIFESTO_BUILD_ROOT
    const auto executable = executable_path();
    if (!executable.empty()) {
        std::error_code error;
        const auto build = fs::weakly_canonical(MANIFESTO_BUILD_ROOT, error);
        if (!error && executable.parent_path() == build)
            return source_template_root();
        // Installed data is one bundle. Missing members must not be filled in
        // from a different source checkout or incidental workspace templates.
        return (executable.parent_path() / MANIFESTO_INSTALL_TEMPLATE_PATH)
            .lexically_normal();
    }
    return {};
#else
    return source_template_root();
#endif
}

void append_unique_path(std::vector<fs::path> *paths, std::set<std::string> *seen,
                        const fs::path &path) {
  const std::string key = path.lexically_normal().generic_string();
  if (!key.empty() && seen->insert(key).second) {
    paths->push_back(path);
  }
}

std::vector<fs::path>
template_root_candidates() {
    std::vector<fs::path> candidates;
    std::set<std::string> seen;
    std::string env_root = env_or_empty("MANIFESTO_TEMPLATE_ROOT");
    if (env_root.empty()) {
        env_root = env_or_empty("ECOSYSTEM_TEMPLATE_ROOT");
    }
    if (!env_root.empty()) {
        append_unique_path(&candidates, &seen, fs::path(env_root));
    }

    append_unique_path(&candidates, &seen, version_template_root());
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
    for (const fs::path& root : template_root_candidates()) {
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
        *error_message = "unable to locate manifesto template file; searched";
        for (const auto& root : template_root_candidates())
            *error_message += " " + root.generic_string();
        for (const auto& relative : relative_paths)
            *error_message += " [" + relative.generic_string() + "]";
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
