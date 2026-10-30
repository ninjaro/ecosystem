# Core tests

`ecosystem_tests.cpp` retains the test registry, substring filter and opt-in
native acceptance commands. Test names and execution order are stable. Grouped
files live in `suite/`, apart from conventional per-component test scopes.

| File | Responsibility |
| --- | --- |
| `manifest_tests.cpp` | Authored model, ownership, paths, QML and file writes |
| `build_tests.cpp` | Facades, exported libraries, installation and build layouts |
| `source_dependencies_tests.cpp` | Independent source providers and consumer contracts |
| `source_facade_tests.cpp` | Visitor Git providers, package installation, native facade acceptance |
| `local_source_map_tests.cpp` | Ignored source maps, selection precedence and visitor isolation |
| `packages_tests.cpp` | Package descriptors, status, summaries and CMake rules |
| `generation_tests.cpp` | Tracked/developer surfaces, templates and repository policy |
| `ci/` | Selection, evidence, result gates, Pages, bootstrap and local checks |
| `mutation_tests.cpp` | Manifest mutations and scaffolding |
| `cli_workspace_tests.cpp` | Actors, CLI dispatch, workspace selection and execution |
| `android_tests.cpp` | Application identity, toolchains, APK deployment and readiness |
| `analysis_tests.cpp` | Clang analysis and advisory naming/style diagnostics |
| `checks_tests.cpp` | Tidy, formatting, leaks and Java checks |
| `documentation/` | Doxygen ownership/errors, Sphinx configuration/recovery and native integration |
| `reports_tests.cpp` | Doctor, matrix and toolchain reports |
| `reports/` | C++ and personal diagnostic reports, scopes and failures |
| `benchmarks/` | Generic benchmark execution, selection and optional reports |
| `release_tests.cpp` | Candidate publication, logs, versioning and signing |
| `workspace/release/` | Runtime closure, native packages and installed tooling |
| `workspace/assets/` | Asset topology, index integrity and controller behavior |
| `../../assets/dataset/test/suite/presentation/` | Python verification, diagnostics, benchmark, asset and composition fixtures |

Shared fixtures and assertions are declared in `test_support.hpp` and implemented
once in `test_support.cpp`. `test_cases.hpp` gathers grouped test declarations,
including companion headers such as `documentation.hpp`. Add a new test to its
topic file, declare it in the owning header, then register it in the runner.
The manifest owns `tests/suite` so generated builds discover every
translation unit; no hand-maintained CMake source list is required.

Executable Python and JavaScript fixtures live under
`assets/dataset/test/suite/` at the project root. The C++ test sources remain
under `core/tests/`; the manifest's `tests/suite` ownership is still valid.

`assets/dataset/test/suite/presentation_browser.py` runs explicitly with a
generated presentation module and Chromium. It checks carousel controls,
keyboard selection, video pause, resizing
and the script-free list. An optional theme argument builds a native Sphinx site
before the browser checks. Native media fixtures require FFmpeg and `pdftoppm`
(Poppler); ordinary Python presentation fixtures require no Sphinx environment.

The TeX bunch under `tests/workspace/tex/` groups selection/registration, ownership,
recovery and CLI/tooling scenarios. Ordinary `tex_` groups use native-tool fixtures.
`--tex-integration` runs real `latexmk`/`pdflatex` with supporting styles, nested
includes, bibliography and references, then verifies the registered PDF through
the native Sphinx/Poppler pipeline. It requires the documentation Python
environment and checks that real compilation failure preserves existing output.

`assets/dataset/test/suite/diagnostics_browser.py` runs with a generated
presentation module, Engels and Chromium. It compares a real failed analyzer
snapshot across JSON, CI Markdown and
Pages, then checks combined filters, failure visibility, reset and the script-free
view. Native Sphinx integration also checks selected snapshot downloads, invalid
input recovery and removal of retired diagnostic output.

`--sphinx-integration` also builds a real benchmark and checks its native SVG,
measurement table and exact raw downloads in a themed site. Stale sidecars,
generic reruns and failed runs must retire earlier charts while preserving the
appropriate raw view. Ordinary Python fixtures cover plot records, numeric data,
static SVG restrictions, limits and input aliases without native documentation tools.

`ci_verification_evidence_contract` runs Node fixtures against freshly generated
adapters: `ci_change_cases.cjs` uses real Git histories for event deltas and
conservative fallbacks; `ci_evidence_cases.cjs` and `ci_publication_cases.cjs`
exercise immutable verification and publication with mocked GitHub APIs. These
fixtures require Node and Git, with no npm packages or network access.
`ci_change_policy_cases.cjs` drives the native Engels change report using those
event records and real manifests, including dependency closures, ownership aliases,
deleted inputs and altered/stale records.
`ci_changed_format_cases.cjs` records real clang-format invocations and checks
single-file selection, empty/presentation skips, native failures and full fallbacks.
The policy and formatting suites share `ci_change_fixtures.cjs`.
`ci_cheap_format_cases.cjs` runs generated cheap-Checks programs through real Git,
Marx and Engels, covering PR/push selection, docs, native failures, uncertain
inputs and repository drift without project configuration or compilation.
Evidence/publication fixtures reject legacy or narrower scopes before reuse or
Pages publication; presentation fixtures also validate explicit whole-tree scope.

For an opt-in local formatting comparison, run
`node assets/dataset/test/suite/ci_changed_format_benchmark.cjs .github/actions/select-ecosystem-ci/changes.cjs <engels-binary>`.
It compares full and changed-file verification over 205 owned C/C++ files with one
edit, using five alternating measured runs per mode after warmups. It measures
local formatting, including manifest/Git validation, not checkout/bootstrap or an
entire workflow. No timing threshold is imposed on ordinary tests.

Run the existing `core__core__tests` executable with an optional test-name
substring. `--workspace-conformance <root>` remains an opt-in mode.
The old `--prerelease-install` mode was removed with release ownership; native
package/install acceptance belongs downstream.

`--facade-packages <installed-prefix>` compiles and runs an independent consumer
of real `packing::library__core`, `monitor::watchdog_client__client` and
`monitor::watchdog_qt__qt` exports. It requires installed providers and Qt, performs
no downloads, and checks a consumer version distinct from both providers. Move the
install prefix before running it to verify relocation. The probe runs its built
target through CTest in Debug, including when `CMAKE_GENERATOR` selects Ninja
Multi-Config. The dated
[adoption evidence](../../docs/audits/facade-adoption-2026-09-30.md) records the
provider revisions and patches used for native acceptance.

`--facade-android <ndk-root>` cross-compiles synthetic static/shared providers and
consumers for Android ARM64 and x86-64, API 28. It verifies installation, package
relocation and ELF target architecture without running the binaries. Supply an
existing NDK; no SDK/device or network download is needed. Set `CMAKE_GENERATOR`
to exercise a particular generator. See the [platform acceptance record](../../docs/audits/facade-platform-acceptance-2026-10-01.md)
for the exact tested toolchain and limits.

## Facade acceptance

The [facade audit matrix](../../docs/audits/external-project-facade-2026-09-29.md#11-acceptance-matrix)
maps to these ordinary regression groups. All native evidence recorded so far is
Linux; Make and Ninja Multi-Config are exercised where specified by the tests.

| Contract | Regression |
| --- | --- |
| Outer source orchestration, inner installed targets, direct source conflicts | `external_project_generates_imported_library` |
| Visitor with no manifests, cache forwarding, Debug/Release, edits, offline rebuild, root entry and consumer failure | `source_facade_outer_preserves_installed_consumer` |
| Recursive library install, static provider into shared wrapper, package relocation without original build/source paths | `source_facade_outer_delegates_library_install` |
| Branch/tag/commit selection, no implicit updates, changed revision cannot reuse stale Config | `source_facade_pins_revisions_and_rejects_stale_packages` |
| Shared/interface usage and multiple artifacts sharing a provider | `source_facade_preserves_shared_and_interface_contracts` |
| Direct and three-repository acquisition/revision/configure/compile/install/Config/target/cycle failures; enclosing identities and blocked outer consumer configuration | `source_facade_reports_provider_failures` |
| Marx provider builds, mutable edits, pinned repository reuse, package exports and invalid provider contracts | `source_dependency_` (six groups, now using maps for local selection) |
| Exact root mapping, migration errors, malformed/missing paths, recursion and visitor isolation | `source_local_map_` (three groups) |
| Runtime preparation through a map, excluding local configuration from shipped files | `prerelease_prepares_installed_source_provider_runtime` |

The opt-in real API probe and [public adoption record](../../docs/audits/facade-public-adoption-2026-09-30.md)
supplement these synthetic local-Git fixtures. They do not turn routine regression
runs into network-dependent tests. The Android opt-in adds bounded cross-compilation
acceptance. Native Windows/macOS, other cross toolchains and broader transitive
identity/failure acceptance remain with the
[owning cards](../../docs/planning/near-term.md#dep-facade-06).
