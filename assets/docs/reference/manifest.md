# Authored manifest

`manifest.json` describes project identity, artifacts, ownership and dependency
intent. Marx derives the file inventory and generated surfaces from it. This is
the current schema; `components`, `modules`, `file_units`, `relations`, `stack`
and `link` are not accepted authored fields. Only this schema is supported.

## Project fields

| Field | Meaning |
| --- | --- |
| `id` | Required project identifier, matching `[a-z][a-z0-9_]*` |
| `description` | Required nonempty project description |
| `facade` | Required artifact identity selected by the visitor facade |
| `artifacts` | Required nonempty array of artifact definitions |
| `version` | Optional project/package version, such as `1.2.3` |
| `cpp_standard` | Optional integer of at least 17; defaults to 20 |
| `install_artifacts` | Optional additional artifact identities to build/install with the facade |
| `install_assets` | Optional boolean enabling installation of the project's `assets/` tree |
| `android_application_id` | Optional stable Android application identity |
| `android` | Optional Android package metadata; see below |

`install_artifacts` expresses distribution intent without linking independent
executables together. Its entries must be unique and resolve. The self manifest
uses this list to distribute Marx and Engels together.

When `version` is set, compiled targets receive `ECOSYSTEM_PROJECT_VERSION` as a
private definition for their own sources. Interface libraries and installed usage
requirements do not export this generic macro, so linking packages with different
versions cannot overwrite the consumer's version. Public headers that expose a
library version need a provider-specific constant or name.

Machine-local SDK/NDK/toolchain paths and resolved release state do not belong in
this file. Keep local tool paths in the existing environment/cache configuration;
prerelease counters and generated data stay under `.ecosystem/`. Unknown root and
artifact fields are rejected, including obsolete fields mixed into a new manifest.

Each `namespace:artifact` identifies one canonical current artifact. There is no
internal version selector or family of historical facade definitions. Project
release/package metadata does not select an implementation. Current producers and
consumers must be updated together; historical authored schemas are rejected.

## Generated repository and local surfaces

The manifest drives canonical repository generation; the current schema has no
per-project `ignore` or `gitignore` field. Generated `.gitignore` uses the shared
policy, including root-anchored `/.clang-format` and `/.clang-tidy` rules. These
are optional generated developer configurations, not tracked repository inputs.
Their absence, partial presence or local content changes do not count as tracked
surface drift. Nested authored files with those names are not ignored by these
root rules.

`marx sync` retains their local generation capability. The generation inventory
includes both required repository outputs and optional local outputs;
`ignored_local_surface_paths()` identifies the latter. Doctor/repository checks
still require canonical CMake, `.gitignore` and applicable CI surfaces, and reject
local generated configurations actually committed to Git. They do not require
Git to compare the required surface inventory of a source distribution.

Existing local configuration is not added to an index by any generator. Updating
an existing consumer's required `.gitignore` is ordinary canonical-surface adoption;
missing local Clang files are intentional and never a migration failure.

Required tidy verifies the selected configuration before source analysis. When
the optional project-root `.clang-tidy` is absent, it explicitly selects the
canonical template bundle; unrelated ancestor/default settings cannot replace
the baseline. Invalid check names fail as configuration errors. Formatting uses
the nearest supported config within the project, preserving nested styles, or
the canonical bundle when no local config exists. Neither check recreates an
optional root file, and source diagnostics/format drift remain hard failures.

## Stable documentation

`assets/docs/` is the maintained final reference surface. Development plans,
research, audits and superseded evidence remain outside it. Repository validation
recognizes `docs` as an authored asset category, while default Qt embedding,
runtime staging and asset installation exclude it together with `showcase`.
Sphinx uses exactly one `assets/docs/index.rst` or `index.md` as its only authored
entry point. Optional `sphinx.json` belongs in that same unit. Root-level `docs/`
is never an authored publication source. Active dependencies must remain inside
the stable unit; generated API and verification pages are separate outputs.

## Runtime resource ownership

Runtime resources use the fixed `assets/` categories `brand`, `reports`,
`dataset`, `dumps`, `sets` and `templates`. Root `showcase` media and root
metadata are excluded; a nested runtime directory named `showcase` is allowed.
With `install_assets`, these files are also embedded in Qt applications and
installed under `share/<project-id>/assets/`. Application staging uses the same
inventory, including when asset installation is disabled.

Directories named `.cache` or a hidden name ending in `_cache` (for example,
`.thumbnail_cache`) are reserved for transient output. Their descendants are
excluded from embedding, staging and installation at any depth, regardless of
extension or Git state. Standard standalone `.gitignore` includes matching
directory rules. A visible `card_set_cache/`, `.card_set_cache_reference/`,
`.card_set_cache.svg`, or a regular file named `.cache` is not excluded.
Previously staged reserved cache directories are removed without following
destination aliases. Installation selects explicit files; it does not uninstall
unrelated or previously installed files from an existing prefix. Use a fresh
prefix when verifying a complete package inventory.

Prerelease application assets retain their existing `usr/bin/assets/` package
layout. Tooling templates independently install/package under
`share/ecosystem/templates`, even with `install_assets: false`; both paths use
the same cache policy. Inventory units are files, not empty directories.

This is a narrow ownership policy, **not Git-ignore evaluation**. Git is not
required to configure a source distribution. Other authored or source-generated
files in runtime categories remain resources, including Git-ignored `dumps/`
output. The existing manifest has no per-file resource include/exclude override;
place required generated files outside the reserved cache names before configure.
Generated Qt Android overlays, mipmaps and QML module outputs retain their
separate build-directory/explicit-input contracts.

Selection is sorted and deterministic, without file-extension filtering.

Generated agent screenshots and simulation outputs can use
`assets/dumps/test/.agent_cache/<task>/`. The hidden `_cache` directory is reserved
generated output, excluded by the existing resource policy and canonical ignore
rules. Keep authored assets outside it, and retain decisions/specifications in
tracked documentation. A `dumps/test` branch requires a matching `dataset/test`
directory. Agent build/state/report data belongs under the project's `.ecosystem/`.
Safe nested in-project links retain their logical resource aliases and are
materialized as ordinary stage/install files. Escaping/dangling links, directory
cycles, aliased category roots and aliased output paths fail explicitly instead
of traversing another tree. Source/link changes require the ordinary configure/
build refresh; this is not a guarantee against concurrent filesystem mutation.
Staging remains a post-link step; invoking its generated script refreshes an
otherwise up-to-date output. Make/Ninja links depend on the staging policy script.

## Android packaging

An Android `qt_app` declares `packages.android: ["qt_android"]`. Project-level
`android_application_id` supplies its stable package ID (fallback:
`org.example.<id>`). The optional `android` object contains:

| Field | Meaning |
| --- | --- |
| `name` | Display name; defaults to the artifact output name |
| `version_code` | Positive Android integer, at most 2100000000; defaults to 1 |
| `min_sdk` / `target_sdk` | Positive API levels; minimum defaults to 28, target defaults to Qt's selection |
| `permissions` | Additional permission names |
| `excluded_permissions` | Names suppressed during Qt deployment and Android manifest merging |
| `features` | Objects with `name` and explicit boolean `required` |
| `allow_backup` / `full_backup_content` / `full_backup_only` | Boolean Android application backup policies; omitted values retain Qt defaults |
| `supports_rtl` | Optional boolean RTL support |
| `icon` | Existing project-relative `assets/brand/*.svg` launcher artwork |
| `icon_export_id` | Optional existing SVG element ID to render instead of its whole canvas |
| `round_icon` | Boolean using the same launcher resource for round launchers; requires `icon` |
| `activity_config_changes` | Optional array of Android configuration-change tokens; omitted retains Qt's template |

Android version name comes from the project `version` (fallback `0.1.0`).
Unknown metadata, duplicate permission/feature names, conflicting included and
excluded permissions, escaping icon paths and invalid types are rejected.
SDK requirements must fit the selected Qt Android kit.

Standard generated CMake embeds the shared package helper and a snapshot of
manifest metadata. Android configuration patches the **installed Qt kit's**
`src/android/templates/AndroidManifest.xml`, retaining Qt's deployment markers,
activity metadata, providers and other template content. Only a generated
manifest and launcher resources are placed in
`<CMake binary directory>/ecosystem/android/<target>/package/`, then connected
through `QT_ANDROID_PACKAGE_SOURCE_DIR` before Qt finalization. No project-local
template override or authored Android source directory is needed. Separate
Android build directories are required. Native configuration does not invoke
Python or the SVG renderer. Run `marx sync` after metadata changes; artwork
changes trigger CMake reconfiguration.

Python 3.8+ is required for Android package preparation. When `icon` is set,
`rsvg-convert` (librsvg) must be available on the host, or its executable path
must be supplied as `-DECOSYSTEM_ANDROID_SVG_CONVERTER=/path/to/rsvg-convert`.
A missing or failing renderer stops configuration with a diagnostic while
retaining the previous generated overlay. Ordinary SVGs are not Android
launcher resources: the helper renders PNG mipmaps at mdpi/hdpi/xhdpi/xxhdpi/
xxxhdpi (48/72/96/144/192 pixels), preserving the artwork's aspect ratio and
transparency. `icon_export_id` selects an existing artwork group, useful when an
animated SVG has an oversized canvas; the renderer produces a static launcher.
Display names are escaped for Qt's XML attribute insertion. These are legacy launcher mipmaps, not newly designed adaptive
icons. Qt supplies Gradle, runtime Java, `libs.xml` and provider resources at
deployment; none are copied into ecosystem source.

The old `android_package_source_dir` field is rejected. Migrate application
settings to `android`, select existing brand SVG artwork, regenerate CMake,
and validate the generated package before removing an old authored overlay.

Qt package overlays
retain the standard deployment pipeline. Explicit
optional hardware features
prevent camera permission from restricting applications to camera-equipped devices.

## Artifact fields

| Field | Meaning |
| --- | --- |
| `id` | Required globally unique `namespace:artifact` identity |
| `kind` | Required `static_lib`, `shared_lib`, `interface_lib`, `exe` or `qt_app` |
| `owns` | Required array of owned scopes; may be empty for an entry-only executable or an imported library |
| `root` | Artifact's project-relative source root; defaults to `.` |
| `entry` | Required root-relative C++ entry file for `exe` and `qt_app`; omitted for libraries |
| `dependencies` | Optional array of explicitly linked library artifact identities |
| `packages` | Optional package requirements for this artifact |
| `tests` | Optional test support, such as `{"selftest": true}` or `{"gtest": true}` |
| `benchmarks` | Optional benchmark support, such as `{"google_benchmark": true}` |
| `name` | Optional output filename stem |
| `description` | Optional description; defaults to the project description |
| `qml` | Optional QML module for `qt_app`; see [optional QML](#optional-qml) |

Both parts of an identity match `[a-z][a-z0-9_]*`. Several artifacts may share a
namespace or source root. Each gets an independent source owner and package scope
in the derived build model. An adjacent library is included only through an
explicit dependency. Library links must resolve, be unique and contain no cycles.

Output names must be safe filenames. Distinct artifact identities must not
produce the same output or encoded CMake target. Automatically generated test
targets and executable filenames are reserved too. Automatic test targets
use `namespace__artifact__tests`; explicit test-only artifacts use their ordinary
`namespace__artifact` targets. CTest and artifact-scoped test/coverage/leak commands
use these same identities.

## Ownership and discovery

A logical scope such as `math/vector` selects conventional companions under the
artifact's `root`:

```text
include/math/vector.hpp       src/math/vector.cpp
include/math/vector.h         src/math/vector.cc
include/math/vector.tpp       src/math/vector.cxx
tests/math/vector_tests.cpp
benchmarks/math/vector_benchmarks.cpp
```

Directories at those locations are scanned recursively. Header discovery supports
`.h`, `.hpp`, `.hh`, `.hxx` and `.tpp`; source discovery supports `.cpp`, `.cc` and
`.cxx`. A scope can therefore select a referent and its subtree without listing
every file. Missing companion forms are not invented or required.

Scopes beginning with `include`, `src`, `tests` or `benchmarks` select that
conventional tree directly. For example, `include/api` selects that header tree,
`tests/integration.cpp` selects one test file, and `tests` owns the entire test
tree. `.` owns all four conventional trees under the artifact root. Use broad
scopes only when that ownership is intended.

`entry` is an explicitly owned source even when `owns` is empty. Its filename
need not be `main.cpp`; `.cc` and `.cxx` entries are supported. Runnable test or
benchmark artifacts can select entries under `tests/` or `benchmarks/`.

Scopes must not overlap between artifacts, including through filesystem aliases.
Potential overlapping scopes are rejected before their files exist. Owned paths
must stay inside the selected project through symlinks; dangling links are
rejected. Ordinary missing destinations and safe internal links are supported.
Shared implementation belongs in a library dependency with its own scope.

Discovery runs when a manifest is loaded. After adding companions, run `marx sync`
to refresh tracked CMake, or the relevant Marx build/check operation to refresh
its developer input. The saved manifest retains scopes and entry intent; discovery
does not write a file inventory back into JSON.

Developer test generation uses the owned test files, and library installation
selects owned public headers and `.tpp` companions under `include/`. Unowned
sources, tests and headers are excluded from those surfaces.

## Runnable tests and benchmarks

An explicit `exe` or `qt_app` whose root-relative `entry` begins with `tests/`
establishes test capability, even without a framework flag. An entry under
`benchmarks/` establishes benchmark capability. These artifacts use their
ordinary target identities. Developer CMake registers runnable test artifacts
with CTest; Engels uses the same capability and target model. Marx's benchmark
command selects runnable benchmark artifacts. Library/framework metadata alone
does not declare a benchmark executable.

Runnable check targets compile their entry and all companion translation units
owned by that artifact, including explicitly owned helpers in another conventional
source category. They do not acquire undeclared files or another owner's tests.
A source directory containing multiple
independent `main()` functions must be split into appropriate artifact scopes.
Qt package automation, declared headers, QtTest's offscreen environment and
runtime asset staging remain available to explicitly declared test artifacts.

For a production artifact, an enabled `tests` flag such as `selftest`, `gtest`
or `qttest` requests an automatic test executable from its owned test sources.
With the authored ownership schema, that source inventory must be nonempty.
Empty/all-false flags leave those sources inactive. Merely placing files in a
test directory does not activate or reassign them. Automatic test target names
remain reserved during manifest validation, before source discovery.

Automatic test targets recompile their owner's production implementation. They
receive the same applicable optional-package/profile definitions (OpenCV only
when found, KDE only when its profile is enabled), project version, includes,
dependency links and compiler defaults. Feature definitions on these test
targets are private; test frameworks remain absent from production exports.
Shared package support targets can be encountered again during test discovery.
Their local/imported identity is preserved, discovery still runs, and repeated
usage requirements are deduplicated while later profile requirements are kept.

The `optional_support_targets` and `automatic_tests_preserve_optional_features`
regressions exercise real OpenCV discovery, an explicitly disabled OpenCV
configuration, executable/library facades and multiple automatic owners in
Debug/Release. Their positive branch requires an installed OpenCV package.

`ECOSYSTEM_BUILD_TESTS` and `ECOSYSTEM_BUILD_BENCHMARKS` enable the corresponding
developer targets at configuration time; both default to `OFF`. Framework flags
configure the required framework, rather than disabling an explicitly declared
runnable artifact. For example, `tests: {"qttest": false}` on an explicit
framework-free test executable still declares a runnable test.

Test/benchmark metadata supplies dependencies only to check targets. Framework
discovery in developer CMake is gated by the corresponding build option.
Production targets and installed production package configurations retain their
declared `packages` requirements without inheriting test-only GoogleTest,
QtTest or Google Benchmark requirements. Production-only consumers therefore
can configure with `CMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE`.

`tests: {"gtest": true}` links `GTest::gtest` and the default
`GTest::gtest_main`. A test artifact supplying its own `main()` can select
`tests: {"gtest": true, "gtest_main": false}`. The entry may instead be an
ordinary test translation unit when GoogleTest supplies the main function.
`gtest_main` alone does not enable an automatic test executable.

Generator regression gates `owned_check_artifacts`, `check_capabilities` and
`framework_dependencies` in `core__core__tests` cover multi-source execution,
disabled/unowned checks, framework entry points and relocated package
consumers. The framework integration fixture requires installed GoogleTest and
Qt6 Core/Test development packages. Its production-only configurations disable
both packages explicitly; they are not production dependencies of ecosystem.

## Core-only example

With implementation under `core/include/math/` and `core/src/math/`:

```json
{
  "id": "numbers",
  "description": "Reusable numeric library",
  "facade": "core:lib",
  "artifacts": [
    {"id": "core:lib", "kind": "static_lib", "root": "core", "owns": ["math"]}
  ]
}
```

Use `interface_lib` for a header-only library. A library facade builds and installs
its selected library and public headers without requiring an executable entry.

## Visitor facade

For an executable `facade`, the standard visitor path is:

```bash
cmake -S . -B build
cmake --build build
./build/mvp
```

`facade` selects one real artifact. Its artifact ID, CMake target and output name
remain unchanged; the generated `mvp` file points to its actual target file.
Unix builds use a relative symlink, while native Windows builds use `mvp.exe`
with a hard-link/copy fallback. Arguments and exit status belong to the selected
executable. Default builds repair a removed facade and refresh its selection even
without relinking; multi-config builds select the most recently built configuration.

An unrelated artifact cannot claim the reserved `mvp`/`mvp.exe` output names when
the facade is runnable. The selected artifact may itself already use that name.
Reconfiguration removes a previously generated facade link/copy before applying
the new selection. Library facades keep their build/install contract and do not
synthesize a runnable application; Android packages use the Android run path.
Installs continue to contain canonical artifacts, not an additional facade binary.

## App-only example

With an entry file at `app/src/launch.cpp`:

```json
{
  "id": "hello",
  "description": "Standalone application",
  "facade": "app:app",
  "artifacts": [
    {"id": "app:app", "kind": "exe", "root": "app", "owns": [], "entry": "src/launch.cpp"}
  ]
}
```

## Core and app example

The app owns its UI tree and entry. Its reusable code is a declared dependency:

```json
{
  "id": "numbers_app",
  "description": "Application using the numeric library",
  "facade": "app:app",
  "artifacts": [
    {"id": "core:lib", "kind": "static_lib", "root": "core", "owns": ["math"]},
    {
      "id": "app:app",
      "kind": "exe",
      "root": "app",
      "owns": ["ui"],
      "entry": "src/launch.cpp",
      "dependencies": ["core:lib"]
    }
  ]
}
```

Several artifacts may share a `core/` root while owning separate engine,
CLI-library and entry-point scopes. Ownership is declared per artifact; sharing
a root does not merge their targets or automatically duplicate their sources.

## Installed CMake libraries

A local library selected as the facade or listed in `install_artifacts` installs
its library dependency closure and a CMake package under
`${CMAKE_INSTALL_LIBDIR}/cmake/<project-id>/`. Header-only libraries export their
interface target. Installed targets retain public include paths, C++ standard,
internal library links and registered package requirements.

Generated internal package version files use `ExactVersion`: an explicitly
requested version must match the current package version. Unversioned discovery
still selects the supplied installed package and does not pin its source commit.
This removes the former blanket same-major compatibility promise while retaining
package/target identities and real external dependency version constraints.

For project `numbers` and artifact `core:lib`, a separate consumer can use:

```cmake
find_package(numbers CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE numbers::core__lib)
```

Supply the install prefix through `CMAKE_PREFIX_PATH` or `numbers_DIR`. Exported
target names combine the project namespace with the artifact's encoded CMake
identity. The package can be relocated; it does not need the provider's source or
build directory. `CMAKE_INSTALL_INCLUDEDIR` and `CMAKE_INSTALL_LIBDIR` are honored.
Library roots listed alongside an executable facade carry their own dependencies.

Exports through `external_project` use the provider's installed CMake package.
A consumer library's exported configuration calls `find_dependency` for its
providers. Supply those installed prefixes through standard CMake discovery when
using the consumer package elsewhere; source and private build trees are not
part of the exported contract. Registry dependencies keep their shared package
rules.

## Optional QML

Widgets-only applications need no `qml` block or QML package discovery. A
`qt_app` may declare one embedded module alongside its ordinary C++ ownership:

```json
{
  "id": "app:desktop",
  "kind": "qt_app",
  "root": "app",
  "owns": [],
  "entry": "src/main.cpp",
  "packages": {"qt": ["Core", "Gui", "Qml", "Quick"]},
  "qml": {
    "uri": "Example.Ui",
    "version": "1.0",
    "files": ["qml/Main.qml", "qml/screens/Home.qml"]
  }
}
```

The current representation requires `root: "app"`, a module identifier, a
`major.minor` version and a nonempty explicit list of safe root-relative
`qml/*.qml` files. `packages.qt` must include `Qml`; declare `Quick` when the
application uses Qt Quick. Files must exist, remain inside the project after
symlink resolution, and belong to one artifact. This list is QML ownership;
the files need not be repeated in `owns`.

Generation delegates compilation, resource embedding and module metadata to
`qt_add_qml_module`.
Aliases retain paths beneath `qml/`; the module uses the `/qt/qml` resource
prefix and a separate output directory for each artifact. With Qt 6.5+, the
example module can be loaded using `engine.loadFromModule("Example.Ui", "Main")`.
Qt 6.8+ supplies the nested-directory module behavior through
`QTP0004`; older kits are not
asked to enable a policy they do not support. Native acceptance uses Qt 6.8.2.

The current QML representation uses a fixed root, explicit file inventory and
a mandatory module version.

## Packages and external libraries

Package requirements remain artifact-local. For example:

```json
{
  "id": "app:desktop",
  "kind": "qt_app",
  "root": "app",
  "owns": ["window"],
  "entry": "src/launch.cpp",
  "packages": {"qt": ["Core", "Gui", "Widgets"]}
}
```

Existing package integrations retain their registered capability data. A library
provided by another Ecosystem-managed project declares its source and installed identity:

```json
{
  "id": "packing:core",
  "kind": "static_lib",
  "owns": [],
  "packages": {
    "external_project": {
      "repository": "https://github.com/your-org/packing.git",
      "revision": "compatible-commit-or-tag",
      "package": "packing",
      "artifact": "library:core"
    }
  }
}
```

Replace repository/revision with a compatible provider selection. `package` is
its manifest project ID; `artifact` names the provider's exported library. The old
`component` field is rejected. Provider and consumer artifact IDs may differ.
The provider owns its files; consumers link `packing:core` through `dependencies`
without redeclaring its source scopes. The requested provider library must be in
its exported facade/install-artifact closure and match the imported artifact kind;
preparation verifies both before consumer configure. Static, shared and interface
libraries retain the provider target's complete usage requirements. Unknown source
metadata fields are rejected rather than silently ignored.

`marx build` prepares independent providers before configuring the consumer. It
uses each provider's generated development surface, installs standard packages,
and passes their package locations to consumer CMake. The consumer graph uses
`find_package(packing CONFIG REQUIRED)` and `packing::library__core`.

The tracked visitor facade supplies an outer CMake build that acquires each selected
Git repository/revision, configures its committed facade independently, builds and
installs it, then configures the inner consumer against the installed package.
Neither Marx nor preinstalled providers are required:

```sh
cmake -S . -B build
cmake --build build
./build/mvp
```

The last command applies to executable facades. Library facades support
`cmake --install build --prefix /chosen/prefix`, including recursive provider
installation. Providers must commit functional synchronized CMake facades; the
visitor does not parse their manifests or regenerate them. Static, shared and
interface targets retain the provider's installed usage requirements. Both visitor
CMake and Marx configure providers with position-independent code enabled so static
archives can be linked into shared consumers. Provider targets must honor that
CMake setting.

Ordinary rebuilds compile edited consumer sources without fetching or advancing
the selected provider checkout. Changing authored repository/revision intent and
running `marx sync` selects a separate build/install state; an old installed package
cannot satisfy that new selection. A fresh build can resolve a mutable branch/tag
differently. Local selection state is not a portable dependency lock.
Repeated direct requests for one package/repository/revision share one provider;
conflicting direct selections for the same package fail generation. Multi-config
builds separate provider and consumer state per configuration; use matching
`--config` values for build and install.

Providers sharing an install prefix must use distinct installed file paths.
An install prefix is not an isolation mechanism for conflicting provider files.
For coordinated development, create `ecosystem.local.json` in the consumer's
project root:

```json
{
  "sources": {
    "https://github.com/ninjaro/rothko.git": "/absolute/path/to/packing"
  }
}
```

Keys match the authored repository string exactly; values are absolute directory
paths. Marx reads this one map for the entire provider dependency closure. It does
not search parent directories or load providers' own local maps. Local checkouts
may contain uncommitted edits or different branches. The file is gitignored and
excluded from install/release payloads. `marx sync` and visitor CMake do not read it;
tracked CMake remains byte-for-byte portable across local selections.

Selection is an exact map entry, otherwise authored repository/revision. An
invalid selected path fails; it never falls back to a lower-priority selection.
Malformed JSON, duplicate/unknown fields, non-string or relative paths also fail.
Unused entries must have valid syntax but need not point to existing directories.
Use `{"sources": {}}` for an empty map, or remove the file.

Repository-basename `<NAME>_SOURCE_DIR` environment variables no longer select
provider checkouts. If one is nonempty and the repository has no exact map entry,
Marx fails with migration instructions before preparing that provider. Add its
absolute checkout path to the root source map, or unset the variable to use
repository/revision. A matching map entry takes precedence over a leftover legacy
variable; an empty variable has no effect.

Local selections rebuild/install on every operation so edits reach consumers.
Generated provider sources/builds/install prefixes live under the consumer's
`.ecosystem/dependencies`, leaving local checkout sources untouched. Replace old
CMake source-cache variables and legacy environment overrides with this map.
Visitor CMake always uses authored Git intent, even if a local map or legacy
environment variable exists.

Remote selections record a resolved commit in local `selection.json` state.
Ordinary rebuilds reuse it without fetching, even when a branch advances. Use an
immutable commit in authored intent for reproducibility across clean machines;
this local state is not a portable lockfile. Changing repository/revision creates
a separate selection. Managed checkout edits or moved HEAD fail with override
guidance. After an interrupted initial clone, remove the named incomplete generated
state directory before retrying. Removing a map entry and unsetting any legacy
variable restores the recorded remote selection. Provider build/install failures
stop consumer configuration.

Source dependencies currently use desktop profiles; Android preparation fails
explicitly until cross-toolchain validation. A successful selected native build
does not establish every transitive, cyclic or multiple-selection configuration.

## Mutations and generated state

The existing mutation commands scaffold conventional files and save scope intent:

```text
marx mutate add module core:lib math/extra
marx mutate add file-unit core:lib include/detail --kind header_template_impl
marx sync
engels check repo
```

Use a full artifact identity when a namespace contains several artifacts. An
unqualified namespace selector is usable when it identifies one owner.

Mutation preflight validates candidate semantics and paths before authored writes.
Sync checks all current and obsolete destinations before its first tracked write.
These checks do not provide rollback for later I/O failures or protection against
concurrent filesystem replacement; those remain transaction hardening.
