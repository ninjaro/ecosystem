#include "../documentation.hpp"
#include "../test_support.hpp"

namespace ecosystem_test_support {

void test_stable_documentation_self_contained() {
    const auto project = fs::path(ECOS_TEST_SOURCE_DIR);
    const auto validator
        = project / "assets/templates/tooling/documentation_boundary.py";
    const auto fixtures = ecosystem::capture_command_result(
        { "python3",
          (project / "assets/dataset/test/suite/documentation_boundary_cases.py"
          )
              .string(),
          validator.string() },
        project
    );
    require_true(fixtures.exit_code == 0, fixtures.output);
    const auto actual = ecosystem::capture_command_result(
        { "python3", validator.string(), (project / "assets/docs").string() },
        project
    );
    require_true(actual.exit_code == 0, actual.output);
}

void write_fake_sphinx(const fs::path& path) {
    write_executable_script(path, R"sh(#!/bin/sh
set -eu
if [ "$1" = --version ]; then echo 'sphinx-build 9.0'; exit 0; fi
printf 'theme=%s\n' "${ECOSYSTEM_SPHINX_THEME:-}" >> "$FAKE_SPHINX_LOG"
printf '%s\n' "$*" >> "$FAKE_SPHINX_LOG"
while [ $# -gt 0 ]; do
  case "$1" in
    -b) shift 2;;
    -c) conf_dir="$2"; shift 2;;
    -E|-a|-W|--keep-going) shift;;
    *) source_dir="$1"; build_dir="$2"; break;;
  esac
done
test -f "$conf_dir/conf.py"
test -f "$conf_dir/../doxygen/xml/index.xml"
case "${SPHINX_TEST_MODE:-}" in
  fail) echo 'fixture missing Breathe dependency' >&2; exit 9;;
  empty) exit 0;;
  empty-index) : > "$build_dir/index.html"; exit 0;;
esac
mkdir -p "$build_dir"
printf '<html>fresh Sphinx documentation</html>\n' > "$build_dir/index.html"
if [ "${SPHINX_TEST_MODE:-}" = index-only ]; then exit 0; fi
mkdir -p "$build_dir/_ecosystem/doxygen"
printf '<html>Project results</html>\n' > "$build_dir/_ecosystem/index.html"
if [ "${SPHINX_TEST_MODE:-}" = empty-results ]; then
  : > "$build_dir/_ecosystem/index.html"
fi
if [ "${SPHINX_TEST_MODE:-}" != missing-api ]; then
  printf '<html>C++ API</html>\n' > "$build_dir/_ecosystem/doxygen/index.html"
fi
if [ "${SPHINX_TEST_MODE:-}" = empty-api ]; then
  : > "$build_dir/_ecosystem/doxygen/index.html"
fi
echo 'fixture Sphinx finished'
)sh");
}

void test_sphinx_presentation_contract() {
    temp_dir root;
    write_sample_sphinx_project(root.path());
    std::string error;
    require_true(
        ecosystem::write_local_sphinx_conf(root.path(), "sample", &error), error
    );
    const auto result = ecosystem::capture_command_result(
        { "python3",
          (fs::path(ECOS_TEST_SOURCE_DIR)
           / "assets/dataset/test/suite/presentation_cases.py")
              .string(),
          (ecosystem::local_sphinx_dir(root.path())
           / "ecosystem_presentation.py")
              .string() },
        root.path()
    );
    require_true(result.exit_code == 0, result.output);
    const auto module = ecosystem::local_sphinx_dir(root.path())
        / "ecosystem_presentation.py";
    const auto retained = root.path() / "retained.py";
    fs::create_hard_link(module, retained);
    const auto original = read_text(retained);
    require_true(
        !ecosystem::write_local_sphinx_conf(root.path(), "changed", &error)
            && read_text(retained) == original,
        "generated Python aliases must fail before overwriting another file"
    );
}

void test_sphinx_default_config() {
    temp_dir root;
    write_sample_sphinx_project(root.path());
    write_text(
        root.path() / "README.md",
        "# Development overview\n[plan](docs/index.md)\n"
    );

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path sphinx_log = fake_root / "sphinx.log";
    fs::create_directories(fake_bin);

    write_fake_sphinx(fake_bin / "sphinx-build");

    const std::string original_path = []() {
        const char* value = std::getenv("PATH");
        return value == nullptr ? std::string() : std::string(value);
    }();
    scoped_env path_env("PATH", fake_bin.string() + ":" + original_path);
    scoped_env sphinx_log_env("FAKE_SPHINX_LOG", sphinx_log.string());

    const cli_result result = run_cli(root.path(), "check sphinx");
    require_true(
        result.exit_code == 0,
        "ecos check sphinx must build the stable assets/docs tree"
    );
    require_contains(
        result.output, "sphinx generated in .ecosystem/sphinx/html",
        "ecos check sphinx must report the generated html output path"
    );
    require_contains(
        read_text(root.path() / ".ecosystem/sphinx/conf.py"),
        "ECOSYSTEM_SPHINX_THEME",
        "ecos check sphinx must default to the Read the Docs theme"
    );
    require_contains(
        read_text(root.path() / ".ecosystem/sphinx/conf.py"), "'myst_parser'",
        "ecos check sphinx must enable MyST when markdown docs are present"
    );
    require_contains(
        read_text(sphinx_log), "-b html",
        "ecos check sphinx must invoke sphinx-build in html mode"
    );
    require_not_contains(
        read_text(root.path() / ".ecosystem/doxygen/Doxyfile"), "README.md",
        "Sphinx API composition must not add the root README as another "
        "authored source"
    );
    write_text(
        root.path() / "assets/docs/sphinx.json",
        R"({"exclude_patterns":["readme.md"]})"
    );
    const auto stable = run_cli(root.path(), "check sphinx");
    require_true(
        stable.exit_code == 0,
        "stable assets/docs must build through the same Sphinx command:\n"
            + stable.output
    );
    require_contains(
        read_text(sphinx_log), (root.path() / "assets/docs").string(),
        "Sphinx must receive the actual stable documentation root"
    );
    require_contains(
        read_text(root.path() / ".ecosystem/sphinx/conf.py"), "readme.md",
        "source selection settings belong to that root"
    );
    const auto configuration
        = read_text(root.path() / ".ecosystem/sphinx/conf.py");
    write_text(
        root.path() / "docs/index.md",
        "# Conflicting entry\n[outside](missing.md)\n"
    );
    write_text(root.path() / "docs/index.rst", ".. absent-directive::\n");
    write_text(root.path() / "docs/sphinx.json", "{malformed");
    write_text(
        root.path() / "docs/conf.py", "raise RuntimeError('must not run')\n"
    );
    const auto conflict = run_cli(root.path(), "check sphinx");
    require_true(
        conflict.exit_code == 0,
        "root docs must not influence Sphinx: " + conflict.output
    );
    require_true(
        read_text(root.path() / ".ecosystem/sphinx/conf.py") == configuration,
        "conflicting root entry/configuration must not affect generated "
        "configuration"
    );
    fs::remove_all(root.path() / "docs");
    const auto product = run_cli(root.path(), "check sphinx");
    require_true(
        product.exit_code == 0,
        "product checkout without root docs must work: " + product.output
    );
    fs::remove(root.path() / "assets/docs/index.md");
    write_text(root.path() / "docs/index.md", "# Cannot be a fallback\n");
    const auto missing = run_cli(root.path(), "check sphinx");
    require_true(
        missing.exit_code == 3,
        "a root docs entry cannot establish Sphinx capability"
    );
    require_contains(
        missing.output, "documentation-source error",
        "missing stable entry must be explicit"
    );
    const auto doctor = run_cli(root.path(), "doctor sphinx");
    require_true(
        doctor.exit_code == 3, "doctor must reject a missing stable entry"
    );
    write_text(
        root.path() / "assets/docs/index.md",
        "# Invalid dependency\n[history](../../docs/index.md)\n"
    );
    const auto escaping = run_cli(root.path(), "check sphinx");
    require_true(
        escaping.exit_code == 5, "actor must enforce the stable unit boundary"
    );
    require_contains(
        escaping.output, "leaves documentation unit",
        "identify the forbidden dependency"
    );
    require_true(
        read_text(root.path() / ".ecosystem/sphinx/conf.py") == configuration,
        "a boundary failure must not replace the last generated configuration"
    );
    write_text(root.path() / "assets/docs/index.md", "# Stable entry\n");
    write_text(root.path() / "assets/docs/index.rst", "Duplicate\n=========\n");
    const auto duplicate = run_cli(root.path(), "check sphinx");
    require_true(duplicate.exit_code == 5, "two stable entries are ambiguous");
    require_contains(
        duplicate.output, "exactly one", "explain the valid entry contract"
    );
}

void test_sphinx_theme_override() {
    temp_dir root;
    write_sample_sphinx_project(root.path());

    const fs::path fake_root = root.path() / "fake-tools";
    const fs::path fake_bin = fake_root / "bin";
    const fs::path sphinx_log = fake_root / "sphinx.log";
    fs::create_directories(fake_bin);

    write_fake_sphinx(fake_bin / "sphinx-build");

    const std::string original_path = []() {
        const char* value = std::getenv("PATH");
        return value == nullptr ? std::string() : std::string(value);
    }();
    scoped_env path_env("PATH", fake_bin.string() + ":" + original_path);
    scoped_env sphinx_log_env("FAKE_SPHINX_LOG", sphinx_log.string());

    const cli_result result = run_cli(root.path(), "check sphinx --theme furo");
    require_true(
        result.exit_code == 0,
        "ecos check sphinx --theme must accept an explicit theme override"
    );
    require_contains(
        read_text(sphinx_log), "theme=furo",
        "ecos check sphinx --theme must forward the theme override "
        "into the Sphinx environment"
    );
}

} // namespace ecosystem_test_support
