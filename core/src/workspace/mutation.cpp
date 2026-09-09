#include "workspace/mutation.hpp"

#include "workspace/project.hpp"
#include "workspace/template_text.hpp"
#include "workspace/tooling.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace ecosystem {
namespace mutation_support {

    component*
    find_mutable_component(manifest* value, const std::string& component_id) {
        const auto* selected = find_component(*value, component_id);
        for (auto& item : value->components)
            if (&item == selected)
                return &item;
        return nullptr;
    }

std::string normalize_module_path(std::string value) {
  value = fs::path(std::move(value)).lexically_normal().generic_string();
  if (value.rfind("./", 0U) == 0U) {
    value.erase(0U, 2U);
  }
  while (value.size() > 1U && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

bool contains_value(const std::vector<std::string> &values,
                    const std::string &candidate) {
  return std::find(values.begin(), values.end(), candidate) != values.end();
}

bool starts_with(const std::string &value, const std::string &prefix) {
  return value.rfind(prefix, 0U) == 0U;
}

bool is_supported_component_template_kind(const std::string &template_kind) {
  return template_kind == "static_lib" || template_kind == "shared_lib" ||
         template_kind == "interface_lib" || template_kind == "exe" ||
         template_kind == "qt_app" || template_kind == "tests" ||
         template_kind == "benchmarks";
}

bool is_library_artifact_kind(const std::string &kind) {
  return kind == "static_lib" || kind == "shared_lib" ||
         kind == "interface_lib";
}

bool artifact_refs_match(const artifact_ref &left, const artifact_ref &right) {
  return left.component_id == right.component_id &&
         left.artifact_id == right.artifact_id;
}

bool component_template_supports_default_links(
    const std::string &template_kind) {
  return template_kind == "exe" || template_kind == "qt_app" ||
         template_kind == "tests" || template_kind == "benchmarks";
}

std::string default_component_artifact_id(const std::string &component_id,
                                          const std::string &template_kind,
                                          const std::string &artifact_id) {
  if (!artifact_id.empty()) {
    return artifact_id;
  }
  if (template_kind == "exe" || template_kind == "qt_app") {
    return "app";
  }
  if (template_kind == "tests") {
    return "tests";
  }
  if (template_kind == "benchmarks") {
    return "bench";
  }
  return component_id;
}

mutation_report write_if_missing(const fs::path &path,
                                 const std::string &contents,
                                 const bool overwrite = false) {
  mutation_report report;
  std::error_code error;
  if (!overwrite && fs::exists(path, error) && !error) {
    return report;
  }
  std::string error_message;
  if (!write_text_file(path, contents, &error_message)) {
    report.errors.push_back(error_message);
    return report;
  }
  report.written_files.push_back(path);
  return report;
}

std::optional<std::string>
render_mutation_template(const fs::path &relative_path,
                         const template_bindings &bindings,
                         mutation_report *report) {
  std::string error_message;
  const std::string contents =
      render_text_template(relative_path, bindings, &error_message);
  if (!error_message.empty()) {
    report->errors.push_back(error_message);
    return std::nullopt;
  }
  return contents;
}

std::optional<std::string>
render_mutation_template(const fs::path &relative_path,
                         const template_bindings &bindings,
                         string_list *errors) {
  std::string error_message;
  const std::string contents =
      render_text_template(relative_path, bindings, &error_message);
  if (!error_message.empty()) {
    errors->push_back(error_message);
    return std::nullopt;
  }
  return contents;
}

void append_report(mutation_report *destination,
                   const mutation_report &source) {
  destination->written_files.insert(destination->written_files.end(),
                                    source.written_files.begin(),
                                    source.written_files.end());
  destination->errors.insert(destination->errors.end(), source.errors.begin(),
                             source.errors.end());
}

struct scaffold_file {
  fs::path path;
  std::string contents;
};

bool preflight_scaffold(
    const fs::path& project_root, const std::vector<scaffold_file>& files,
    mutation_report* report
) {
    std::vector<fs::path> paths;
    for (const auto& file : files)
        paths.push_back(file.path.lexically_relative(project_root));
    auto errors = validate_project_paths(project_root, paths);
    report->errors.insert(report->errors.end(), errors.begin(), errors.end());
    return errors.empty();
}

void extend_owned_scope(
    component* owner, const fs::path& project_root,
    const std::vector<scaffold_file>& files
) {
    if (!owner->ownership)
        return;
    for (const auto& file : files) {
        const auto relative
            = file.path
                  .lexically_relative(component_root_path(project_root, *owner))
                  .generic_string();
        if (!contains_value(owner->ownership->scopes, relative))
            owner->ownership->scopes.push_back(relative);
    }
}

void refresh_owned_files(
    manifest* value, const fs::path& root, mutation_report* report
) {
    if (!report->errors.empty())
        return;
    const auto errors = discover_owned_files(value, root);
    report->errors.insert(report->errors.end(), errors.begin(), errors.end());
}

void write_scaffold_files(const std::vector<scaffold_file> &scaffold_files,
                          mutation_report *report) {
  for (const scaffold_file &file_value : scaffold_files) {
    append_report(report,
                  write_if_missing(file_value.path, file_value.contents));
  }
}

std::vector<scaffold_file> prepare_module_scaffold_files(
    const fs::path &project_root, const component &component_value,
    const std::string &module_path, string_list *errors) {
  std::vector<scaffold_file> scaffold_files;
  const fs::path root = component_root_path(project_root, component_value);
  if (const std::optional<std::string> header_contents =
          render_mutation_template("mutation/header.hpp.tpl", {}, errors);
      header_contents.has_value()) {
    scaffold_files.push_back(scaffold_file{
        root / "include" / (module_path + ".hpp"), *header_contents});
  }
  if (const std::optional<std::string> source_contents =
          render_mutation_template("mutation/source.cpp.tpl",
                                   {{"module_path", module_path}}, errors);
      source_contents.has_value()) {
    scaffold_files.push_back(
        scaffold_file{root / "src" / (module_path + ".cpp"), *source_contents});
  }
  return scaffold_files;
}

mutation_report scaffold_module_files(const fs::path &project_root,
                                      const component &component_value,
                                      const std::string &module_path) {
  mutation_report report;
  const std::vector<scaffold_file> scaffold_files =
      prepare_module_scaffold_files(project_root, component_value, module_path,
                                    &report.errors);
  if (!report.errors.empty()) {
    return report;
  }
  if (!preflight_scaffold(project_root, scaffold_files, &report))
      return report;
  write_scaffold_files(scaffold_files, &report);
  return report;
}

void ensure_component_layout(const fs::path &root, mutation_report *report) {
  const std::vector<fs::path> directories{
      root / "include",
      root / "src",
      root / "tests",
      root / "benchmarks",
  };
  for (const fs::path &path : directories) {
    std::error_code error;
    fs::create_directories(path, error);
    if (error) {
      report->errors.push_back("unable to create directory " +
                               path.generic_string() + ": " + error.message());
    }
  }
}

fs::path resolve_source_only_path(const fs::path &component_root,
                                  const std::string &unit_id) {
  if (starts_with(unit_id, "tests/")) {
    return component_root / "tests" / (unit_id.substr(6U) + ".cpp");
  }
  if (starts_with(unit_id, "benchmarks/")) {
    return component_root / "benchmarks" / (unit_id.substr(11U) + ".cpp");
  }
  if (starts_with(unit_id, "src/")) {
    return component_root / (unit_id + ".cpp");
  }
  return component_root / "src" / (unit_id + ".cpp");
}

fs::path resolve_header_path(const fs::path &component_root,
                             const std::string &unit_id,
                             const std::string &extension) {
  if (starts_with(unit_id, "tests/")) {
    return component_root / "tests" / (unit_id.substr(6U) + extension);
  }
  if (starts_with(unit_id, "benchmarks/")) {
    return component_root / "benchmarks" / (unit_id.substr(11U) + extension);
  }
  if (starts_with(unit_id, "include/")) {
    return component_root / (unit_id + extension);
  }
  return component_root / "include" / (unit_id + extension);
}

std::vector<fs::path> file_unit_paths(const fs::path &component_root,
                                      const std::string &unit_id,
                                      const std::string &kind) {
  if (kind == "header_only") {
    return {resolve_header_path(component_root, unit_id, ".hpp")};
  }
  if (kind == "header_only_h") {
    return {resolve_header_path(component_root, unit_id, ".h")};
  }
  if (kind == "header_template_impl") {
    return {
        resolve_header_path(component_root, unit_id, ".hpp"),
        resolve_header_path(component_root, unit_id, ".tpp"),
    };
  }
  if (kind == "source_only") {
    return {resolve_source_only_path(component_root, unit_id)};
  }
  if (kind == "source_pair_h") {
    return {
        resolve_header_path(component_root, unit_id, ".h"),
        resolve_source_only_path(component_root, unit_id),
    };
  }
  return {};
}

std::optional<std::string> file_unit_contents(const std::string &unit_id,
                                              const std::string &kind,
                                              const fs::path &path,
                                              string_list *errors) {
  const std::string extension = path.extension().generic_string();
  if (extension == ".hpp" || extension == ".h") {
    if (kind == "header_template_impl") {
        return render_mutation_template(
            "mutation/header_template_impl.hpp.tpl",
            { { "unit_id", fs::path(unit_id).filename().generic_string() } },
            errors
        );
    }
    return render_mutation_template("mutation/header.hpp.tpl", {}, errors);
  }
  if (extension == ".tpp") {
    return std::string();
  }
  if (kind == "source_pair_h") {
    return render_mutation_template("mutation/source_pair.cpp.tpl",
                                    {{"unit_id", unit_id}}, errors);
  }
  return render_mutation_template("mutation/source_only.cpp.tpl",
                                  {{"unit_id", unit_id}}, errors);
}

const artifact *resolve_artifact(const manifest &value,
                                 const artifact_ref &ref) {
    const component* component_value
        = find_component(value, format_artifact_ref(ref));
    if (component_value == nullptr) {
        return nullptr;
    }
  return find_artifact(*component_value, ref.artifact_id);
}

bool artifact_ref_is_library(const manifest &value, const artifact_ref &ref) {
  const artifact *artifact_value = resolve_artifact(value, ref);
  return artifact_value != nullptr &&
         is_library_artifact_kind(artifact_value->kind);
}

void append_unique_artifact_ref(std::vector<artifact_ref> *refs,
                                const artifact_ref &candidate) {
  for (const artifact_ref &ref : *refs) {
    if (artifact_refs_match(ref, candidate)) {
      return;
    }
  }
  refs->push_back(candidate);
}

std::vector<artifact_ref> direct_library_artifact_links(const manifest &value,
                                                        const artifact &value_) {
  std::vector<artifact_ref> refs;
  for (const std::string &link : value_.link) {
    const std::optional<artifact_ref> ref = parse_artifact_ref(link);
    if (!ref.has_value() || !artifact_ref_is_library(value, *ref)) {
      continue;
    }
    append_unique_artifact_ref(&refs, *ref);
  }
  return refs;
}

std::vector<artifact_ref>
component_library_artifact_refs(const component &component_value) {
  std::vector<artifact_ref> refs;
  for (const artifact &artifact_value : component_value.artifacts) {
    if (!is_library_artifact_kind(artifact_value.kind)) {
      continue;
    }
    refs.push_back(artifact_ref{component_value.id, artifact_value.id});
  }
  return refs;
}

std::vector<artifact_ref> library_artifact_refs(const manifest &value) {
  std::vector<artifact_ref> refs;
  for (const component &component_value : value.components) {
    for (const artifact &artifact_value : component_value.artifacts) {
      if (!is_library_artifact_kind(artifact_value.kind)) {
        continue;
      }
      refs.push_back(artifact_ref{component_value.id, artifact_value.id});
    }
  }
  return refs;
}

string_list format_artifact_links(const std::vector<artifact_ref> &refs) {
  string_list links;
  for (const artifact_ref &ref : refs) {
    links.push_back(format_artifact_ref(ref));
  }
  return links;
}

void collect_library_artifact_closure(const manifest &value,
                                      const artifact_ref &ref,
                                      std::vector<artifact_ref> *refs,
                                      string_list *seen_refs) {
  if (!artifact_ref_is_library(value, ref)) {
    return;
  }
  const std::string formatted_ref = format_artifact_ref(ref);
  if (contains_value(*seen_refs, formatted_ref)) {
    return;
  }
  seen_refs->push_back(formatted_ref);
  refs->push_back(ref);

  const artifact *artifact_value = resolve_artifact(value, ref);
  if (artifact_value == nullptr) {
    return;
  }
  for (const artifact_ref &linked_ref :
       direct_library_artifact_links(value, *artifact_value)) {
    collect_library_artifact_closure(value, linked_ref, refs, seen_refs);
  }
}

std::vector<artifact_ref>
facade_default_library_artifact_refs(const manifest &value) {
  const std::optional<artifact_ref> facade_ref =
      parse_artifact_ref(value.facade_entry_artifact);
  if (!facade_ref.has_value()) {
    return {};
  }

  std::vector<artifact_ref> start_refs;
  const component* facade_component
      = find_component(value, format_artifact_ref(*facade_ref));
  const artifact *facade_artifact = resolve_artifact(value, *facade_ref);
  if (artifact_ref_is_library(value, *facade_ref)) {
    append_unique_artifact_ref(&start_refs, *facade_ref);
  }
  if (facade_component != nullptr) {
    for (const artifact_ref &ref :
         component_library_artifact_refs(*facade_component)) {
      append_unique_artifact_ref(&start_refs, ref);
    }
  }
  if (facade_artifact != nullptr) {
    for (const artifact_ref &ref :
         direct_library_artifact_links(value, *facade_artifact)) {
      append_unique_artifact_ref(&start_refs, ref);
    }
  }

  std::vector<artifact_ref> refs;
  string_list seen_refs;
  for (const artifact_ref &ref : start_refs) {
    collect_library_artifact_closure(value, ref, &refs, &seen_refs);
  }
  return refs;
}

string_list default_component_artifact_links(const manifest &value,
                                             const std::string &template_kind) {
  if (!component_template_supports_default_links(template_kind)) {
    return {};
  }
  const std::vector<artifact_ref> facade_refs =
      facade_default_library_artifact_refs(value);
  if (!facade_refs.empty()) {
    return format_artifact_links(facade_refs);
  }

  const std::vector<artifact_ref> refs = library_artifact_refs(value);
  if (refs.size() == 1U) {
    return {format_artifact_ref(refs.front())};
  }
  return {};
}

string_list
validate_artifact_links(const manifest &value,
                        const std::vector<artifact_ref> &artifact_links) {
  string_list errors;
  std::vector<std::string> seen_links;
  for (const artifact_ref &ref : artifact_links) {
    const std::string formatted_ref = format_artifact_ref(ref);
    if (contains_value(seen_links, formatted_ref)) {
      errors.push_back("duplicate artifact link: " + formatted_ref);
      continue;
    }
    seen_links.push_back(formatted_ref);

    const component* component_value
        = find_component(value, format_artifact_ref(ref));
    if (component_value == nullptr) {
      errors.push_back("unknown linked component: " + ref.component_id);
      continue;
    }
    if (find_artifact(*component_value, ref.artifact_id) == nullptr) {
      errors.push_back("unknown linked artifact: " + formatted_ref);
    }
  }
  return errors;
}

struct component_template_value {
  std::string description;
  std::string artifact_kind;
  json stack = json::object();
  json tests = json::object();
  json benchmarks = json::object();
  std::vector<std::string> modules;
  std::vector<file_unit> file_units;
  std::vector<scaffold_file> scaffold_files;
};

std::optional<component_template_value>
make_component_template(const fs::path &project_root,
                        const std::string &component_id,
                        const std::string &template_kind, string_list *errors) {
  component_template_value template_value;
  template_value.artifact_kind = template_kind;

  if (template_kind == "static_lib") {
    template_value.description = component_id + " static library component.";
    template_value.modules.push_back(component_id);
    if (const std::optional<std::string> header_contents =
            render_mutation_template("mutation/header.hpp.tpl", {}, errors);
        header_contents.has_value()) {
      template_value.scaffold_files.push_back(
          {project_root / component_id / "include" / (component_id + ".hpp"),
           *header_contents});
    }
    if (const std::optional<std::string> source_contents =
            render_mutation_template("mutation/source.cpp.tpl",
                                     {{"module_path", component_id}}, errors);
        source_contents.has_value()) {
      template_value.scaffold_files.push_back(
          {project_root / component_id / "src" / (component_id + ".cpp"),
           *source_contents});
    }
    return template_value;
  }

  if (template_kind == "shared_lib") {
    template_value.description = component_id + " shared library component.";
    template_value.artifact_kind = "shared_lib";
    template_value.modules.push_back(component_id);
    if (const std::optional<std::string> header_contents =
            render_mutation_template("mutation/header.hpp.tpl", {}, errors);
        header_contents.has_value()) {
      template_value.scaffold_files.push_back(
          {project_root / component_id / "include" / (component_id + ".hpp"),
           *header_contents});
    }
    if (const std::optional<std::string> source_contents =
            render_mutation_template("mutation/source.cpp.tpl",
                                     {{"module_path", component_id}}, errors);
        source_contents.has_value()) {
      template_value.scaffold_files.push_back(
          {project_root / component_id / "src" / (component_id + ".cpp"),
           *source_contents});
    }
    return template_value;
  }

  if (template_kind == "interface_lib") {
    template_value.description = component_id + " interface library component.";
    template_value.artifact_kind = "interface_lib";
    template_value.file_units.push_back(file_unit{component_id, "header_only"});
    if (const std::optional<std::string> header_contents =
            render_mutation_template("mutation/header.hpp.tpl", {}, errors);
        header_contents.has_value()) {
      template_value.scaffold_files.push_back(
          {project_root / component_id / "include" / (component_id + ".hpp"),
           *header_contents});
    }
    return template_value;
  }

  if (template_kind == "exe") {
    template_value.description = component_id + " executable component.";
    template_value.artifact_kind = "exe";
    template_value.file_units.push_back(file_unit{"main", "source_only"});
    if (const std::optional<std::string> main_contents =
            render_mutation_template("mutation/main.cpp.tpl", {}, errors);
        main_contents.has_value()) {
      template_value.scaffold_files.push_back(
          {project_root / component_id / "src/main.cpp", *main_contents});
    }
    return template_value;
  }

  if (template_kind == "qt_app") {
    template_value.description = component_id + " Qt application component.";
    template_value.artifact_kind = "qt_app";
    template_value.stack =
        json::object({{"qt", json::array({"Core", "Gui", "Widgets"})}});
    template_value.file_units.push_back(file_unit{"main", "source_only"});
    if (const std::optional<std::string> main_contents =
            render_mutation_template("mutation/qt_main.cpp.tpl", {}, errors);
        main_contents.has_value()) {
      template_value.scaffold_files.push_back(
          {project_root / component_id / "src/main.cpp", *main_contents});
    }
    return template_value;
  }

  if (template_kind == "tests") {
    template_value.description = component_id + " test component.";
    template_value.artifact_kind = "exe";
    template_value.tests = json::object({{"gtest", true}});
    template_value.file_units.push_back(
        file_unit{"tests/test_main", "source_only"});
    if (const std::optional<std::string> main_contents =
            render_mutation_template("mutation/gtest_main.cpp.tpl", {}, errors);
        main_contents.has_value()) {
      template_value.scaffold_files.push_back(
          {project_root / component_id / "tests/test_main.cpp",
           *main_contents});
    }
    return template_value;
  }

  if (template_kind == "benchmarks") {
    template_value.description = component_id + " benchmark component.";
    template_value.artifact_kind = "exe";
    template_value.benchmarks = json::object({{"google_benchmark", true}});
    template_value.file_units.push_back(
        file_unit{"benchmarks/bench_main", "source_only"});
    if (const std::optional<std::string> main_contents =
            render_mutation_template("mutation/benchmark_main.cpp.tpl", {},
                                     errors);
        main_contents.has_value()) {
      template_value.scaffold_files.push_back(
          {project_root / component_id / "benchmarks/bench_main.cpp",
           *main_contents});
    }
    return template_value;
  }

  return std::nullopt;
}

mutation_report
scaffold_component_files(const fs::path &project_root,
                         const component &component_value,
                         const component_template_value &template_value) {
  mutation_report report;
  const fs::path root = component_root_path(project_root, component_value);
  ensure_component_layout(root, &report);
  write_scaffold_files(template_value.scaffold_files, &report);
  return report;
}

} // namespace mutation_support

using namespace mutation_support;

mutation_report add_component(const fs::path &project_root, manifest *value,
                              const std::string &component_id) {
  return add_component(project_root, value, component_id,
                       add_component_options{});
}

mutation_report add_component(const fs::path &project_root, manifest *value,
                              const std::string &component_id,
                              const std::string &template_kind) {
  add_component_options options;
  options.template_kind = template_kind;
  return add_component(project_root, value, component_id, options);
}

mutation_report add_component(const fs::path &project_root, manifest *value,
                              const std::string &component_id,
                              const add_component_options &options) {
  mutation_report report;
  const std::string template_kind = options.template_kind.empty()
                                        ? std::string("static_lib")
                                        : options.template_kind;
  if (component_id.empty()) {
    report.errors.push_back("component id must not be empty");
    return report;
  }
  if (find_component(*value, component_id) != nullptr) {
    report.errors.push_back("component already exists: " + component_id);
    return report;
  }
  if (!is_supported_component_template_kind(template_kind)) {
    report.errors.push_back("unsupported component kind: " + template_kind);
    return report;
  }
  const string_list artifact_link_errors =
      validate_artifact_links(*value, options.artifact_links);
  report.errors.insert(report.errors.end(), artifact_link_errors.begin(),
                       artifact_link_errors.end());
  if (!report.errors.empty()) {
    return report;
  }

  const std::optional<component_template_value> template_value =
      make_component_template(project_root, component_id, template_kind,
                              &report.errors);
  if (!report.errors.empty()) {
    return report;
  }
  if (!template_value.has_value()) {
    report.errors.push_back("unsupported component kind: " + template_kind);
    return report;
  }

  component component_value;
  component_value.id = component_id;
  component_value.description = template_value->description;
  component_value.root = component_id;
  component_value.stack = template_value->stack;
  component_value.tests = template_value->tests;
  component_value.benchmarks = template_value->benchmarks;
  component_value.modules = template_value->modules;
  component_value.artifacts = {
      artifact{
          default_component_artifact_id(component_id, template_kind,
                                        options.artifact_id),
          template_value->artifact_kind,
          {},
          options.artifact_links.empty()
              ? default_component_artifact_links(*value, template_kind)
              : format_artifact_links(options.artifact_links),
      },
  };
  component_value.file_units = template_value->file_units;
  component_value.ownership.emplace();
  if (is_library_artifact_kind(component_value.artifacts.front().kind)) {
      component_value.ownership->scopes = { component_id };
  } else {
      component_value.ownership->entry
          = template_value->scaffold_files.front()
                .path
                .lexically_relative(
                    component_root_path(project_root, component_value)
                )
                .generic_string();
  }
  manifest candidate = *value;
  candidate.components.push_back(component_value);
  report.errors = validate_manifest_paths(candidate, project_root);
  if (report.errors.empty())
      report.errors = discover_owned_files(&candidate, project_root);
  if (!report.errors.empty()) {
    return report;
  }
  if (!preflight_scaffold(
          project_root, template_value->scaffold_files, &report
      ))
      return report;
  value->components.push_back(component_value);
  report.changed_manifest = true;
  append_report(&report, scaffold_component_files(project_root, component_value,
                                                  *template_value));
  refresh_owned_files(value, project_root, &report);
  return report;
}

mutation_report add_module(const fs::path &project_root, manifest *value,
                           const std::string &component_id,
                           const std::string &module_path) {
  mutation_report report;
  component *component_value = find_mutable_component(value, component_id);
  if (component_value == nullptr) {
    report.errors.push_back("unknown component: " + component_id);
    return report;
  }

  const std::string normalized_module_path = normalize_module_path(module_path);
  if (normalized_module_path.empty() || normalized_module_path == ".") {
    report.errors.push_back("module path must not be empty");
    return report;
  }
  if (contains_value(component_value->modules, normalized_module_path)) {
    report.errors.push_back("module already exists: " + normalized_module_path);
    return report;
  }

  const std::vector<scaffold_file> scaffold_files
      = prepare_module_scaffold_files(
          project_root, *component_value, normalized_module_path, &report.errors
      );
  if (!report.errors.empty()
      || !preflight_scaffold(project_root, scaffold_files, &report))
      return report;
  manifest candidate = *value;
  auto* candidate_owner = find_mutable_component(&candidate, component_id);
  candidate_owner->modules.push_back(normalized_module_path);
  if (candidate_owner->ownership)
      candidate_owner->ownership->scopes.push_back(normalized_module_path);
  report.errors = validate_manifest_paths(candidate, project_root);
  if (report.errors.empty())
      report.errors = discover_owned_files(&candidate, project_root);
  if (!report.errors.empty())
      return report;
  if (component_value->ownership)
      component_value->ownership->scopes.push_back(normalized_module_path);
  component_value->modules.push_back(normalized_module_path);
  report.changed_manifest = true;
  write_scaffold_files(scaffold_files, &report);
  refresh_owned_files(value, project_root, &report);
  return report;
}

mutation_report add_files(const fs::path &project_root, const manifest &value,
                          const std::string &component_id,
                          const std::string &module_path) {
  mutation_report report;
  report.errors = validate_manifest_paths(value, project_root);
  if (!report.errors.empty()) {
    return report;
  }
  const component *component_value = find_component(value, component_id);
  if (component_value == nullptr) {
    report.errors.push_back("unknown component: " + component_id);
    return report;
  }

  const std::string normalized_module_path = normalize_module_path(module_path);
  bool declared
      = contains_value(component_value->modules, normalized_module_path);
  if (component_value->ownership)
      for (const auto& scope : component_value->ownership->scopes) {
          declared = declared || scope == "." || scope == normalized_module_path
              || normalized_module_path.starts_with(scope + "/")
              || scope == "include/" + normalized_module_path + ".hpp"
              || scope == "src/" + normalized_module_path + ".cpp";
      }
  if (!declared) {
      report.errors.push_back(
          "module is not declared in manifest: " + normalized_module_path
      );
      return report;
  }

  append_report(&report, scaffold_module_files(project_root, *component_value,
                                               normalized_module_path));
  return report;
}

mutation_report add_file_unit(const fs::path &project_root, manifest *value,
                              const std::string &component_id,
                              const std::string &unit_id,
                              const std::string &kind) {
  mutation_report report;
  component *component_value = find_mutable_component(value, component_id);
  if (component_value == nullptr) {
    report.errors.push_back("unknown component: " + component_id);
    return report;
  }

  if (kind != "header_only" && kind != "header_only_h" &&
      kind != "header_template_impl" && kind != "source_only" &&
      kind != "source_pair_h") {
    report.errors.push_back("unsupported file-unit kind: " + kind);
    return report;
  }

  const std::string normalized_unit_id = normalize_module_path(unit_id);
  for (const file_unit &file_unit_value : component_value->file_units) {
    if (file_unit_value.id == normalized_unit_id) {
      report.errors.push_back("file-unit already exists: " +
                              normalized_unit_id);
      return report;
    }
  }

  const fs::path component_root =
      component_root_path(project_root, *component_value);
  std::vector<scaffold_file> scaffold_files;
  for (const fs::path &path :
       file_unit_paths(component_root, normalized_unit_id, kind)) {
    const std::optional<std::string> contents =
        file_unit_contents(normalized_unit_id, kind, path, &report.errors);
    if (!contents.has_value()) {
      return report;
    }
    scaffold_files.push_back(scaffold_file{path, *contents});
  }
  if (!preflight_scaffold(project_root, scaffold_files, &report))
      return report;
  manifest candidate = *value;
  auto* candidate_owner = find_mutable_component(&candidate, component_id);
  candidate_owner->file_units.push_back({ normalized_unit_id, kind });
  extend_owned_scope(candidate_owner, project_root, scaffold_files);
  report.errors = validate_manifest_paths(candidate, project_root);
  if (report.errors.empty())
      report.errors = discover_owned_files(&candidate, project_root);
  if (!report.errors.empty())
      return report;
  extend_owned_scope(component_value, project_root, scaffold_files);
  component_value->file_units.push_back(file_unit{normalized_unit_id, kind});
  report.changed_manifest = true;
  write_scaffold_files(scaffold_files, &report);
  refresh_owned_files(value, project_root, &report);
  return report;
}

mutation_report set_facade_entry(manifest *value,
                                 const artifact_ref &entry_ref) {
  mutation_report report;
  const component* component_value
      = find_component(*value, format_artifact_ref(entry_ref));
  if (component_value == nullptr) {
    report.errors.push_back("unknown component in facade entry: " +
                            entry_ref.component_id);
    return report;
  }
  const artifact *artifact_value =
      find_artifact(*component_value, entry_ref.artifact_id);
  if (artifact_value == nullptr) {
    report.errors.push_back("unknown artifact in facade entry: " +
                            entry_ref.artifact_id);
    return report;
  }
  if (artifact_value->kind != "static_lib" &&
      artifact_value->kind != "shared_lib" &&
      artifact_value->kind != "interface_lib" &&
      artifact_value->kind != "exe" && artifact_value->kind != "qt_app") {
    report.errors.push_back(
        "facade entry must reference a facade-eligible artifact");
    return report;
  }
  value->facade_entry_artifact = format_artifact_ref(entry_ref);
  report.changed_manifest = true;
  return report;
}

} // namespace ecosystem
