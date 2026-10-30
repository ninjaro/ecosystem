# CLI Commands

This reference defines the current Marx/Engels interface.

## 1. Scope

This document defines the public command-line interface of the Ecosystem tool.
The public surface is split across two binaries:

* `engels` owns read-only, diagnostic, and interpretive flows:
  `list`, `check`, `doctor`, `report`
* `marx` owns write-capable and generative flows:
  `sync`, `mutate`, `format`, `build`, `benchmark`, `prerelease`, `run`

Together, the CLI **MUST** expose only these top-level commands:

* `list`
* `sync`
* `mutate`
* `format`
* `build`
* `benchmark`
* `prerelease`
* `run`
* `check`
* `doctor`
* `report`

No other top-level commands are part of CLI v1.

---

## 2. General rules

1. The CLI **MUST** be manifest-aware.
2. The CLI **MUST** read `manifest.json` before executing any structural or build-related task.
3. The CLI **MUST** reject requests that are invalid or unsupported by the manifest.
4. The CLI **MUST NOT** require manual editing of generated files.
5. The CLI **MUST NOT** require `Makefile` or `scripts/` inside the managed repository.
6. The CLI **MAY** generate local developer artifacts inside the repository working tree.
7. Local developer artifacts **MUST** remain ignored by Git.
8. Public CLI commands **MUST NOT** expose raw full-CMake generation as a standalone user-facing action.

---

## 3. Command grammar

Canonical forms:

```text
engels <command> [subcommand] [options] [arguments]
marx <command> [subcommand] [options] [arguments]
```

Both actors accept `--quiet` before the command and `--then` between requests
owned by that same actor. Batch ownership is checked before the first request.
Execution stops at the first failure. An application’s `--` argument separator
protects subsequent arguments from batch parsing. `--interactive` explicitly
starts an actor’s queued shell. No arguments prints usage and exits successfully,
so a visitor can launch the selected actor through `build/mvp`. Options such as
`--quiet` still require a command.

Current limits: quiet mode suppresses actor messages but does not capture all
child-tool output. The interactive tokenizer does not preserve quoted arguments;
use ordinary CLI invocations when arguments contain spaces. Exiting the shell
discards pending jobs.

Artifact reference form:

```text
<component>:<artifact>
```

Workspace project selector form:

```text
<project>
```

In workspace mode, `<project>` **MAY** be either the manifest project id or the workspace-relative project path. `--project <project>` **MAY** be repeated to select multiple managed projects. The optional workspace-root file `ecosystem.workspace.json` **MAY** also define named project groups for repeatable subsets.

Workspace artifact reference form:

```text
<project>/<component>:<artifact>
```

Examples:

```text
frontend:app
backend:cli
matrix:api
yodau/frontend:app
matrix-centipede/matrix_api:api
```

### 3.1 Workspace selector rules

1. `--project <project>` accepts either the manifest project id or the
   workspace-relative project directory.
2. `--project <project>` may be repeated to select multiple managed projects.
3. `--group <group>` expands a named subset from `ecosystem.workspace.json`
   and may be repeated.
4. `component:artifact` in workspace mode may be repeated, but each
   unqualified artifact selector requires exactly one selected project.
5. `project/component:artifact` is shorthand for selecting both the workspace
   project and the artifact in one argument and may also be repeated.
6. If an explicit project selector and a qualified artifact selector are both
   present, they **MUST** agree on the same workspace project.
7. Commands that do not support artifact filters **MUST** reject both
   `component:artifact` and `project/component:artifact`.
8. Workspace `format`, `build`, `benchmark`, `prerelease`, `doctor`, `report`, and
   artifact-scoped `check` flows **MAY** accept multiple workspace artifact
   filters in one invocation. `run` and project-scoped checks such as
   `check ci` **MUST** reject more than one workspace artifact filter.

Workspace group file form:

```json
{
  "groups": {
    "desktop": ["monitor", "yodau", "kcuckoounter"],
    "games": ["kcuckoounter", "matrix-centipede"]
  }
}
```

---

## 4. `list`

## 4.1 Purpose

`list` prints information derived from the manifest.

## 4.2 Allowed targets

`list` **MAY** show:

* components
* artifacts
* facade entry artifact
* supported profiles
* supported platforms
* workspace groups
* tests
* benchmarks

## 4.3 Rules

1. `list` **MUST NOT** modify the repository.
2. `list` **MUST NOT** generate tracked files.
3. `list` **MAY** resolve compatibility information from the manifest.

## 4.4 Canonical forms

```text
engels list
engels list components
engels list artifacts
engels list profiles
engels list platforms
engels list matrix
```

---

## 5. `sync`

## 5.1 Purpose

`sync` regenerates the tracked repository surface.

## 5.2 Rules

1. `sync` **MUST** regenerate the facade `CMakeLists.txt`.
2. The tracked facade `CMakeLists.txt` **MUST** stay limited to the `facade` closure rather than the full developer graph.
3. `sync` **MUST** regenerate other tracked thin repository entry files, including shared formatting, policy, and GitHub automation surfaces when applicable.
4. `sync` **MUST NOT** expose full developer CMake generation as a public output mode.
5. `sync` **MUST NOT** generate developer-only tracked files.
6. `sync` **MAY** validate generated tracked surfaces after regeneration.

## 5.3 Scope

`sync` is the only public generation command for tracked repository surfaces.

## 5.4 Canonical form

```text
marx sync
marx sync --project <project>
marx sync --project <project> --project <project>
marx sync --group <group>
```

Examples:

```text
marx sync
marx sync --project ecosystem
marx sync --project matrix-centipede
marx sync --project ecosystem --project monitor
marx sync --group desktop
```

`sync` owns the tracked `CMakeLists.txt`, `.gitignore`, `.github/SECURITY.md`, `.github/renovate.json`, and `.github/workflows/{tests,codeql,html}.yml` surfaces, plus `.github/actions/setup-ecosystem/action.yml` when the workflows use the local bootstrap path. It also generates optional local `.clang-format` and `.clang-tidy` configurations, ignored by root-anchored canonical rules; neither their presence nor their contents are required for tracked-surface verification. All generators use the running tool's matching template bundle. A build-tree actor
uses its source templates; an installed actor resolves `${CMAKE_INSTALL_DATADIR}/ecosystem/templates`
relative to its executable, including through a symlink launcher. Move the entire
install prefix when relocating the tool. Missing installed data or template members
fail with the requested name and searched location; parent workspaces and other
source versions do not fill gaps. `ECOSYSTEM_TEMPLATE_ROOT` supplies deliberate
partial overrides at canonical template paths before that bundle. There are no
old filename candidates or environment aliases.

The tracked input `ecosystem.github.vars.json` configures the GitHub adapter. This
tooling repository uses `{"ecosystem_bootstrap": "checkout"}`: setup builds the
checkout already selected by the workflow, including the pull-request revision,
without fetching a different tooling ref. Consumers use explicit compatible
repository/ref inputs, with a source directory relative to that repository:

```json
{
  "ecosystem_bootstrap": "repository",
  "ecosystem_repository": "your-owner/your-tooling-repository",
  "ecosystem_ref": "your-compatible-commit-or-tag",
  "ecosystem_source_path": ".",
  "ecosystem_build_parallelism": "2"
}
```

Use `ecosystem_source_path: "ecosystem"` for a parent repository with that tooling
subdirectory. The default mode is `repository`; there is no implicit repository or
ref. Local generation remains available without remote configuration, but CI setup
fails with configuration guidance until both values are supplied. `checkout` mode
rejects remote repository/ref inputs. Source paths must stay inside the selected
checkout, and the selected directory must contain `CMakeLists.txt`, `manifest.json`
and `assets/templates`. Bootstrap builds visitor Marx first, then uses
`marx build debug engels:engels` to generate and build the ignored developer
graph. It copies the resulting Engels binary to the advertised tooling build
root; stale actor outputs cannot satisfy bootstrap. Inputs are quoted literal values; parallelism must be a positive
integer string and also limits child CMake builds. Run `marx sync` after changes.

Ubuntu bootstrap installs LLVM development metadata and the declared JSON 3.12
prerequisite into a local prefix propagated to subsequent CMake operations;
Ubuntu 24.04's JSON package
is 3.11.3. The JSON checkout selects the upstream
v3.12.0 release.

Other optional settings include `ecosystem_setup_action`, action references,
`sphinx_theme` and `sphinx_theme_package`. The setup action defaults to
`./.github/actions/setup-ecosystem`; selecting a compatible remote action removes
the obsolete local setup file on sync. Remote actions must accept the generated
bootstrap/source/repository/ref and `install-project-deps` inputs and expose
`marx-binary` and `engels-binary`.

Generated Checks and CodeQL share event selection without source-path filters,
including arbitrary manifest-owned source roots and template changes. Ordinary
branch pushes and PRs into non-default branches run `engels check repo` and
`engels check format --changes`. They skip project builds/tests, CodeQL and coverage.
Cheap setup still bootstraps Marx/Engels, but skips Qt and project test dependencies.
The cheap verification job fetches full history and resolves the event with the
shared adapter against its own checkout before setup. Engels revalidates that
record and manifest ownership: known C/C++ changes format only those files,
known presentation-only/empty changes invoke no formatter, and incomplete,
unknown or changed inputs fall back to all owned files. Repository checks always
run first and their failures stop formatting. The verification job includes its
own `changes.json` in diagnostic check reports; selector artifacts are not trusted
as verification inputs. Cheap results cannot create reusable full receipts.

Both selection jobs check out full Git history and resolve one event change record:
PR merge-base to head, or push `before` to current commit. The selector's `changes`
output contains JSON with the event and checkout commits, base/head/comparison
revisions, changed entries, all affected path names, `complete` and `reason`.
Rename entries retain both names; deletions remain present. The identical record
is saved as `.ecosystem/github/changes.json` and uploaded in
`ci-changes-<project>-checks` or `ci-changes-<project>-codeql` for inspection.

A missing/zero base, incomplete or ambiguous history, rewritten push, mismatched
or dirty checkout, invalid filename encoding or excessive output gives
`complete: false` with no partial paths. Manual and scheduled events explicitly
have no trusted delta. Records are limited to 2,000 paths and 128 KiB of JSON;
filenames retain whitespace, newlines and Unicode. Change acquisition alone does
not attest verification or permit a skip; cheap formatting additionally applies
the local manifest-aware policy. CodeQL also consumes an explicit native execution
decision. Checks retain full verification unless matching evidence permits reuse.

PRs into the default branch, default-branch pushes, manual and scheduled Checks
runs select `engels check ci` and `engels check coverage`. The local `ci` aggregate
checks repository policy/drift, builds the production facade (and declared tests),
then runs applicable Java checks, tidy and formatting. A separate preceding
`marx build debug` would duplicate the aggregate's production build. Required stage failures, incomplete results and coverage
failures fail GitHub verification; only explicit unsupported coverage (exit 3) may
skip. Report upload/publication is advisory. CodeQL runs full analysis except for
validated non-code/empty PR deltas: these require successful repository checks and
explicitly omit database initialization, project build and analysis. Tooling setup
still runs to obtain native policy. Unknown or graph-sensitive inputs, pushes and
manual/scheduled baselines retain full analysis. The independent scheduled scan is
every Monday at 04:23 UTC. Unexpected missing or failed operations fail verification.

Default pushes reuse successful PR verification only after validating both Checks
and CodeQL evidence against the merged tree; otherwise they verify freshly.
Tags do not start these build jobs;
release/package work retains its separate lifecycle. No selected operation repairs
tracked surfaces. Project check jobs have read-only repository credentials, with
report publishing isolated in a separate job. Personal naming/style findings remain
advisory; leaks and presentation are outside ordinary verification. Sphinx/Pages
(`html.yml`) publishes after verified default-branch push completion or manual
default-branch dispatch; ordinary CI does not install its Python presentation
dependencies.

Successful full verification can retain an attempt-specific evidence artifact in
addition to diagnostic reports. It contains the tested Git commit/tree, PR head/base,
repository/run/workflow identity, configuration and tooling revision, and hashes of
its report files. The local setup action with checkout bootstrap or a full immutable
external tooling commit is required for reusable receipts. Mutable tooling refs keep
fresh verification. Receipt/artifact creation is required when eligible; diagnostic
report publication remains advisory. The tooling repository's native test suite uses
Node (without npm dependencies) for its generated GitHub evidence adapter fixtures.

Full Checks and CodeQL receipts have one current, versionless layout and bind whole-project scope
for each verified stage to the tested Git tree and manifest blob. The producer
requires full verification scope. Reuse and automatic Pages reject legacy receipts,
missing scope, changed-file scope, skipped analysis and mismatched selection inputs;
they cannot promote narrower results to full verification. Reuse summaries and
publication provenance retain these scopes. Full scope means every applicable
input; the separate coverage result still records explicit unsupported coverage.
Hashed snapshots bind event and native-classification inputs to the tested revision
and manifest. Repository-only PR CodeQL uses a distinct `codeql-delta` receipt kind in `ecosystem-delta-codeql-<run>-<attempt>`, with build/analysis scope `none`.
These artifacts do not authorize full-verification reuse or automatic Pages; a
subsequent default-branch push establishes fresh full evidence.
Manual Pages prerequisites retain their distinct receipt kind and meaning. Numeric
internal schema tags are not emitted or accepted. Required fields, verification
scope, input digests and provenance determine validity; historical layouts are
rejected rather than converted. Package/release versions, dependency pins, Git
revisions and external tool/protocol versions remain independent requirements.

Evidence is retained for 30 days. The consumer searches up to 100 recent PR runs per
workflow and requires the latest matching run's successful selection and required
job, including its result gate and receipt upload. It verifies the tested synthetic
merge's parents/tree, repository and PR identity, workflow/configuration, run attempt,
artifact metadata/digest and each report's size/hash. Checks and CodeQL must attest
the same tested commit. This permits merge, squash and rebase when the resulting
tree matches. Direct pushes, changed trees, missing/expired artifacts, incomplete
jobs, mutable tooling and unavailable APIs select fresh verification. Run attempts
are checked again before reuse to detect concurrent reruns.

Only allowlisted report files are extracted, using Python's standard ZIP reader
on the hosted Ubuntu runner. Symlinks, duplicate/traversal paths, unexpected files
and oversized archives are rejected. Reused report artifacts and job summaries
record the pushed commit/tree and both source runs, attempts, artifact IDs and
digests. Reuse skips native verification/bootstrap and creates no new receipt;
scheduled Checks and CodeQL always run freshly.

Schema 4 additionally records the installed runner image, tools, SDK/package identity
and optional local Clang configuration. Whole-workflow reuse requires a known,
identical environment. Selection normally runs before SDK installation, so unknown
environment identity conservatively chooses execution. A diagnostic receipt with
unknown environment may describe a real full run, but cannot authorize reuse.

For validated affected native changes only, `ECOSYSTEM_REUSE_VERIFICATION=1` permits
clean per-translation-unit AST/Tidy evidence under `.ecosystem/cache/verification`.
Keys include the actor and compiler/tool runtimes, configuration, canonical
templates, manifest, every configured compilation variant and complete compiler
header closures (including system/external headers). Changed or uncertain inputs,
corrupt evidence, failures and unsupported response/module/plugin contexts execute.
Before/after fingerprints reject concurrent input changes. High-impact, manual
and scheduled runs force fresh analysis and do not seed this analysis cache.
Two independent analysis workers preserve deterministic result order. Only native
Debug evidence is cache-eligible; other profile paths execute.

Compiler caching uses content-checked, non-sloppy preprocessor mode; only non-PR
runs publish shared caches. CodeQL disables compiler caching for traced builds.
No test, coverage or CodeQL result is replaced by the AST/Tidy cache. Dynamic
self-test fixtures and the single test owner still require the complete authored
test runner; no unsupported method-level narrowing is claimed.

With `ECOSYSTEM_CI_PROGRESS`, CTest output and per-source progress stream to the
hosted log. `ci.json`, `ci-commands.log` and `tidy.json` retain phase/command timings,
execution/reuse dispositions and reasons; the workflow uploads their copies under
`.ecosystem/github/reports/native/`. Missing required native diagnostics fail the
report gate. Cache statistics are retained separately.

Pages responds to completion of either Checks or CodeQL, admitting successful
default-branch pushes for the current tip. Both latest matching push attempts must
succeed and provide fresh receipts or revalidated original PR evidence. Missing,
expired or changed evidence defers publication; mutable tooling refs cannot supply
these receipts. Automatic Pages verifies repository state and builds docs without
rerunning tests, coverage or CodeQL. Evidence is revalidated after docs and before
deployment, and tip checks reject superseded revisions. Deployment alone receives
Pages write/OIDC permissions and serializes with other deployments.

Manual default-branch dispatch uses the same exact-commit Checks/CodeQL evidence
and revalidation as automatic publication. Neither path reruns tests or coverage.
Sphinx navigation always links Documentation, the native C++ API and Code Coverage.
Coverage uses the prebuilt LLVM HTML and four-metric summary sealed by Checks;
raw logs and receipts remain internal. Media and benchmark snapshots remain
separate. Both publication paths require a complete selected snapshot and a
successful current composed site.

---

## 6. `mutate`

## 6.1 Purpose

`mutate` performs structural project changes.

## 6.2 Rules

1. `mutate` **MUST** modify project structure through the manifest, code templates, or controlled asset indexes.
2. `mutate` **MUST NOT** require manual edits of generated files.
3. `mutate` **MUST** preserve ecosystem defaults.
4. `mutate` **SHOULD** update source scaffolding when required.
5. `mutate` **MUST NOT** silently rewrite unrelated project structure.

## 6.3 Supported operations

`mutate` **MUST** support at least:

* `add component`
* `add module`
* `add files`
* `add file-unit`
* `set facade-entry`

## 6.4 Canonical forms

```text
marx mutate add component <component-id> [--kind <kind>] [--artifact-id <artifact-id>] [--link <component:artifact>]...
marx mutate add module <component-id> <module-path>
marx mutate add files <component-id> <module-path>
marx mutate add file-unit <component-id> <module-id> --kind <kind>
marx mutate set facade-entry <component:artifact>
marx mutate assets put <category> <id> <path> <type> [--description <text>] [--datetime <text>] [--before <id>]
marx mutate assets remove <category> <id>
```

Asset paths are relative to their category under `assets/` and must already exist.
`put` replaces an existing ID in place or appends a new entry; `--before` inserts
or moves it before another ID. Omitted description/datetime fields are empty.
`remove` releases index ownership while retaining the media file. These operations
generate `index.tsv`; `engels check repo` validates it without repairing it.

## 6.5 Expected behavior

`marx mutate add component <component-id>` defaults to `--kind static_lib`.

Supported component kinds:

* `static_lib`
* `shared_lib`
* `interface_lib`
* `exe`
* `qt_app`
* `tests`
* `benchmarks`

Default artifact ids:

* library kinds use the component id
* `exe` and `qt_app` use `app`
* `tests` use `tests`
* `benchmarks` use `bench`

`--artifact-id` overrides the default artifact id.

`--link` is repeatable and appends explicit artifact links in `component:artifact` form.

If `--link` is omitted for `exe`, `qt_app`, `tests`, or `benchmarks`, `mutate` **MAY** infer default internal library links from the reusable facade-adjacent library surface. It prefers the facade-library closure, then libraries declared beside a runnable facade plus their transitive library links, then direct library links from the runnable facade entry, and finally the single-library fallback.

After `mutate`, the user **SHOULD** run:

```text
marx sync
```

## 6.6 File-unit selection rules

`mutate add module` scaffolds the ordinary `include/<path>.hpp` plus
`src/<path>.cpp` pair and saves an ownership scope. The manifest does not contain
`modules` or `file_units` inventories. Use a full `namespace:artifact` owner when
a namespace contains several artifacts.

The file-kind arguments select these scaffold shapes:

* `header_only`: `include/<id>.hpp`
* `header_only_h`: `include/<id>.h`
* `header_template_impl`: `include/<id>.hpp` plus `include/<id>.tpp`
* `source_only`: `src/<id>.cpp`, or `tests/<id>.cpp` / `benchmarks/<id>.cpp`
  when the id starts with `tests/` or `benchmarks/`
* `source_pair_h`: `include/<id>.h` plus the matching `.cpp` path

Examples from `matrix-centipede`:

* `matrix_core` uses `header_template_impl` for `core/dense_matrix` because the
  dense matrix implementation stays header-first.
* `matrix_api` and `matrix_jni` use `source_pair_h` for the native C and JNI
  bridge entry points.
* `matrix_tests` and `matrix_benchmarks` use `source_only` for concrete test
  and benchmark translation units under `tests/` and `benchmarks/`.

Scaffolding saves owned scopes or sparse file paths. Subsequent manifest loads
rediscover companions; the scaffold-kind names are command arguments, not authored
file-inventory fields.

---

## `format`

`marx format` applies the mechanical formatting verified by `engels check format`.
The daily-operation guide in the repository README maps the other common
tasks to their owning actor.

Optional local configuration is not required to exist. Formatter and verifier
use the same nearest config within each selected project (including nested
styles), falling back to the canonical bundle rather than an unrelated parent.
Both visit the complete selected file/configuration groups; failures remain hard.

```text
marx format
marx format core:lib
engels check format core:lib
engels check format --changes
marx format --project alpha
marx format --group core
marx format alpha/core:lib beta/app:app
```

In a project directory, one optional `component:artifact` limits formatting to
that artifact's owned files. With no filter, all owned C/C++ headers, sources,
tests and benchmarks are selected. Unowned files and generated build outputs are
excluded. Workspace selectors may select multiple projects or artifacts; selected
manifest validity is checked before source writes.

In a project root, `engels check format --changes` reads the event record at
`.ecosystem/github/changes.json` and uses the same validated manifest-aware policy
as `engels report changes`. A known changed C/C++ set checks only its existing
owned files; presentation-only or established empty changes invoke no formatter,
including no version probe. Missing, incomplete, altered or stale records and
global/unknown changes fall back to all owned files. Native formatting failures
still fail the command and never repair source contents. This option cannot be
combined with artifact/workspace selectors or other check profiles. Ordinary
`check format`, `check ci` and `marx format` retain their existing selections.
Generated cheap Checks uses this option. Reuse requires verified evidence
covering the selected inputs; unknown evidence requires execution.

Formatting and verification share file selection, `clang-format` lookup,
`-style=file` configuration resolution and working directory. Marx invokes `-i`;
Engels invokes `--dry-run --Werror` and preserves source contents. Existing root
and nested format configurations are used; generate the shared root policy with
`marx sync`. Formatting does not regenerate tracked settings or change the manifest.
Unknown scopes, missing tooling, invalid configuration and formatter failures
return errors. An empty selection reports `format: no files`.

---

## 7. `build`

## 7.1 Purpose

`build` performs local developer or platform-specific builds.

## 7.2 Rules

1. `build` **MUST** validate the requested profile against the manifest before building.
2. `build` **MUST NOT** start work for a manifest-incompatible request.
3. `build` **MAY** generate local ignored developer artifacts.
4. `build` **MUST NOT** modify tracked facade files unless explicitly combined with `sync`.
5. `build` **MAY** materialize a full developer CMake surface internally under `.ecosystem/source/`.

## 7.3 Supported profiles

CLI v1 **MUST** support at least:

* `debug`
* `release`
* `android`
* `kde`

Additional profiles **MAY** be added later.

## 7.4 Canonical forms

```text
marx build <profile>
marx build <profile> <component:artifact>
marx build <profile> --project <project>
marx build <profile> --project <project> --project <project>
marx build <profile> <project/component:artifact>
marx build tex [--project <project> ...] [--group <group> ...]
```

Examples:

```text
marx build debug
marx build release
marx build android frontend:app
marx build kde frontend:app
marx build debug --project ecosystem
marx build debug --project ecosystem --project monitor
marx build release --project ecosystem
marx build debug yodau/frontend:app
```

`build tex` is a project presentation build. It selects only root-level
`tex/*.tex` documents and uses `latexmk -pdf` with `pdflatex`; it does not support
artifact filters or configure C++. Supporting files resolve from `tex/`, while
auxiliary output and the native command log stay in `.ecosystem/tex/`.
PDFs are written to `assets/showcase/tex/<stem>.pdf` and automatically registered
as `tex:<stem>`. New entries append in source-name order; rebuilds preserve
controller-edited captions, dates and positions. Compilation or registration
failure preserves previous PDFs and index ownership. Conflicting IDs/paths,
unowned output files and retained recovery state fail without overwriting them.

Use `engels doctor --profile tex` to check the same required native tools without
generation. A project with no root-level TeX documents reports unsupported;
unfiltered workspace builds skip those projects. Removing a source retains its
showcase artifact until ownership is explicitly removed through
`marx mutate assets remove showcase <id>`; that operation retains the media file.

### Android environment selection

For a `qt_app`, `marx build android <artifact>` builds the selected application's
APK and prints its path; it does not need a deployment device or ADB/aapt overrides.
The output is
`.ecosystem/build/project/android/debug/default/android-build-<target>/<target>.apk`,
where `<target>` is the CMake artifact target (for example, `app__main`).
`run android` builds and installs that same file. A failed package build or a
missing/empty expected APK fails without searching for another package.
Android packaging requires Qt 6.7 or newer and enables Qt's cached
`QT_USE_TARGET_ANDROID_BUILD_DIR`
option so applications have separate payload directories.

Android configuration uses an explicitly selected or uniquely installed toolchain.
It does not choose a Qt/NDK version or ABI merely because it was previously a default.

| Selection | Rule |
| --- | --- |
| SDK | `ANDROID_HOME` or `ANDROID_SDK_ROOT`; if both are set they must resolve to the same directory. Otherwise inspect conventional SDK locations and SDK-layout tools on PATH; multiple roots require an explicit choice. |
| NDK | `ANDROID_NDK_ROOT`, or `ANDROID_NDK_VERSION` beneath the selected SDK, or its single installed NDK. The toolchain file and `source.properties` revision must exist; an explicit version must match. |
| Qt Android | `ANDROID_CMAKE_BIN`, or a unique kit beneath `QT_DIR` (default `~/Qt`), filtered by `QT_VER` and `ANDROID_ABI`/`ANDROID_QT_ARCH`. Multiple matching kits require a choice. |
| ABI | Inferred from a conventional Qt Android kit or selected by `ANDROID_ABI`; contradictory architecture/ABI choices fail. A custom wrapper needs an explicit ABI. |
| Host Qt | `QT_HOST_PATH`, or one host kit beside the selected Android kit. Custom layouts require the explicit path. Qt validates host/target compatibility during configuration. |
| Minimum API | Qt's selected toolchain default; `ANDROID_PLATFORM` deliberately overrides it. |
| Deployment tools | `ADB_BIN`, `AAPT_BIN` and `ANDROID_EMULATOR_BIN` override discovery without falling back from invalid paths. Multiple SDK `aapt` versions require `ANDROID_BUILD_TOOLS_VERSION` or `AAPT_BIN`. Missing deployment-only tools do not prevent compilation. |

For example, select a version and ABI installed on your machine:

```text
QT_VER=6.8.2 ANDROID_ABI=arm64-v8a marx build android app:main
QT_VER=6.8.2 ANDROID_ABI=arm64-v8a engels doctor --profile android app:main
```

Configuration records selected paths, ABI and selection errors in
`.ecosystem/android/environment.json`, including failed selection attempts.
`doctor --profile android` prints the same environment fields. Its artifact-scoped
probe uses the same Android toolchain while retaining its selected package scope.
An existing cache with a different ABI, NDK, host Qt or Qt toolchain is rejected;
use a fresh Android build directory when changing those selections.
These are local environment settings, not authored manifest paths.

---

Declared [managed source dependencies](manifest.md#packages-and-external-libraries)
are built and installed independently before consumer CMake configuration.
`marx build debug imported:library` also completes this preparation for an imported
artifact. Providers must export the requested package target; compiler, install
and metadata failures stop the operation. The consumer's ignored
[`ecosystem.local.json` source map](manifest.md#packages-and-external-libraries)
selects mutable local checkouts across the provider closure, without generated
writes in provider source trees. Exact repository map entries select local work;
otherwise authored repository/revision applies. A nonempty legacy
`<NAME>_SOURCE_DIR` variable without a matching map entry fails. Add an exact
repository-to-absolute-path entry to `ecosystem.local.json`, or unset that variable
to use the authored repository/revision. There is no silent remote fallback.
Invalid explicit mappings fail instead of falling back. Sync and visitor CMake
ignore these local settings. Repository selections reuse a locally recorded commit;
changing authored repository/revision intent selects a new checkout.

## 7.5 `benchmark`

### Purpose

`benchmark` builds one selected benchmark artifact through the release profile
and captures local benchmark reports without repository-local wrapper scripts.

### Rules

1. `benchmark` **MUST** resolve exactly one benchmark-component artifact.
2. `benchmark` **MUST** build through the `release` build profile with benchmark components enabled.
3. `benchmark` **MUST** write the raw benchmark log and a generic `result.json` under `.ecosystem/reports/benchmark/<component>/<artifact>/`.
4. `benchmark` **MUST** preserve arguments after `--` without injecting framework-specific flags.
5. `benchmark` **MAY** write a summary JSON plus an SVG plot when the captured output exposes `FLOPs=...G/s` counters.

### Canonical forms

```text
marx benchmark
marx benchmark <component:artifact>
marx benchmark --project <project>
marx benchmark --project <project> --project <project>
marx benchmark <project/component:artifact>
marx benchmark <component:artifact> -- <arg> ...
marx benchmark <project/component:artifact> -- <arg> ...
```

Examples:

```text
marx benchmark
marx benchmark benchmarks:bench
marx benchmark benchmarks:bench -- --benchmark_filter=transpose
marx benchmark --project matrix-centipede
marx benchmark matrix-centipede/matrix_benchmarks:bench -- --benchmark_min_time=0.2
```

`benchmark` keeps its generated outputs in local ignored state. `result.json`
records the artifact, release profile, exact command, working directory, exit code,
status and captured log filename. Failed executions retain their log and result;
the terminal error identifies the artifact and exit code. Report persistence errors
fail the command. Component/artifact directories prevent underscore-name collisions;
old `<component>_<artifact>` directories are no longer updated.

Generic output needs no parser. Recognized `FLOPs=...G/s` counters may also produce
`summary.json` and `bench_plot.svg`. Malformed counters are ignored, optional
report errors are warnings, and old optional reports are retired before another
execution. If an optional report cannot be retired, the command warns and skips
plotting; the raw log and `result.json` remain the current execution record.

Successful specialized reports also write an optional `presentation.json`, which
records the artifact and byte counts/change checksums for the generic result,
raw log, summary and SVG. This connects local presentation inputs to one saved run;
it is separate from verified CI evidence. Its failures follow the same optional
report policy, and it is retired before the next execution.

Sphinx presents successful manifest-owned local runs with their captured command,
raw log and result downloads. A matching specialized snapshot adds the native SVG,
series JSON and a table of `n` and GFLOPs/s measurements. FLOPs and generic runs
retain the raw view. Missing, stale or unsupported optional plots explain their
omission without failing documentation; plot inputs are limited to 8 MiB per file
within the presentation's total input budget. These local snapshots do not claim
verified CI status.

---

## 7.6 `prerelease`

### Purpose and ownership

`marx prerelease` configures/builds the selected complete product through the
Release build profile and exports its version-1 prepared-product handoff.
`--prepare-only` is a compatible spelling of the same operation; it is not a
second mode. Marx never creates native archives/repository indexes, signs,
publishes repositories or advances a release counter.

```sh
marx prerelease
marx prerelease tool:cli
marx prerelease --project sample
marx prerelease --prepare-only
marx prerelease --version-base 1.2.3
```

Workspace project/artifact selection and declared install companions retain the
existing distribution policy. An explicit non-facade artifact selects only its
own product closure. Facade/developer CMake graphs are not payload substitutes.
Marx builds all required outputs and prepares resources, project/provider ELF
runtime files and materialized SONAME aliases. Existing asset opt-in, cache
exclusions, template-bundle ownership and runtime normalization remain unchanged.
Missing artifacts, unsafe paths, collisions and incomplete runtime preparation
fail upstream; Release must not build or repair them. Native package ownership,
versions and backend dependency metadata belong to Ecosystem-Release.

Every invocation retains a new attempt beneath
`.ecosystem/prerelease/attempts/<time>-<pid>-<suffix>/`. The CLI prints
`commands.log` and `summary.json` before building. Transcripts contain actual
native arguments/output/status, including provider builds. Failed or interrupted
attempts retain diagnostics; a failed final summary write leaves the initial
`running` marker so downstream discovery fails closed. Preparation success exits
0 and reports stage `prepared`, not publication or release readiness.

### Prepared full-product handoff

The exact exported path is
`.ecosystem/prerelease/attempts/<attempt>/prepared/input.json`. The existing v1
contract contains project/product/facade/package identity, primary/companion
artifacts, all staged data/runtime files, install-relative destinations, sizes
and SHA256 digests. Files reference retained regular copies in `payload/`.
The existing `manifest.version`, when present, supplies `version_base`;
`--version-base` cannot contradict it. When absent, an explicit override may be
exported; otherwise Release selects its existing persisted/default base. Marx
never reads or writes release counters to prepare this handoff.

Provenance binds exact manifest bytes and records a full Git revision only for
clean tracked source when available. `sha256sum` and every declared output must
be available. Manifest and prepared-file observations are rechecked before
export. An attempt summary is descriptive, not authenticated Engels evidence.
No producer authentication, package/installation acceptance or publication
permission follows from preparation success. Existing published files and
`.ecosystem/prerelease/state.json` remain untouched on success and failure.

### Downstream operation

From a correctly prepared project root:

```sh
ecosystem-release package
# Optional, still an unpublished local candidate:
ecosystem-release package --sign --sign-key KEY
```

Release owns package/index creation, signing, version-state interpretation and
publication mechanisms. Production publication remains disabled pending trusted
upstream evidence and final shipped-representation gates. Marx does not invoke
Release. Obsolete Marx `--sign`/`--sign-key` arguments fail with a migration
diagnostic; they are never silently accepted. Explicit Release input/output
paths remain available for testing and advanced use.

---

## 7.7 `run`

### Purpose

`run` builds and executes one runnable artifact through the ecosystem-owned
developer surface.

### Rules

1. `run` **MUST** validate the requested profile against the manifest before execution.
2. `run` **MUST** resolve exactly one runnable artifact.
3. `run` **MAY** build the artifact on demand through the local developer surface.
4. `run` **MUST NOT** require repository-local shell wrappers.
5. Additional child-process arguments **MAY** be forwarded after `--`.
6. Android execution **MUST** package and deploy selected `qt_app` artifacts through the Ecosystem tool when the `android` profile is requested.

### Supported profiles

`run` uses the same build profiles as `build`.

### Canonical forms

```text
marx run <profile>
marx run <profile> <component:artifact>
marx run <profile> --project <project>
marx run <profile> <project/component:artifact>
marx run <profile> <component:artifact> --android-mode <auto|emulator|device>
marx run <profile> <component:artifact> -- <arg> ...
```

Examples:

```text
marx run debug
marx run debug backend:cli
marx run kde frontend:app
marx run debug --project monitor
marx run debug yodau/backend_cli:cli -- --help
marx run android kcuckoounter/trainer_app:app --android-mode emulator
```

`run` uses build profiles, not a separate runtime-profile matrix. When the
profile is `android`, `run` packages the selected `qt_app` artifact, resolves
an Android deployment target, installs the generated debug APK with `adb`, and
launches it. `--android-mode auto` prefers a ready emulator, then a ready connected
device, and starts an emulator when no matching targets are listed. `emulator`
and `device` restrict selection to that target kind.

Set `ANDROID_SERIAL` to select a specific listed target. It must have ADB state
`device` and match the requested mode; missing, offline or unauthorized explicit
targets fail without fallback. Multiple ready candidates in the preferred target
kind fail with their serials and selection instructions. Unavailable listed targets
are reported when no ready candidate exists. A failed `adb devices -l` query fails
the operation instead of starting another emulator. These selection failures occur
before device wait/install/launch; APK preparation currently happens first.

```text
ANDROID_SERIAL=emulator-5556 marx run android app:main --android-mode emulator
```

`ANDROID_EMULATOR_BOOT_TIMEOUT` sets one readiness budget in positive seconds
(default 300), starting before device selection. It includes emulator discovery,
`adb wait-for-device` and `sys.boot_completed` polling for both emulators and
physical devices. Each ADB readiness command is bounded by the remaining time;
failure stops before installation or launch. APK compilation, installation and
launch are outside this readiness budget.

This uses GNU Coreutils `timeout`
(or `gtimeout` on PATH) to terminate a stuck readiness command and its children.
If neither is installed, Android run reports missing tooling before contacting
ADB or starting an emulator. APK-only builds do not need this utility.

---

## 8. `check`

## 8.1 Purpose

`check` runs quality and verification tasks.

## 8.2 Rules

1. `check` **MUST** operate through the Ecosystem tool only.
2. `check` **MAY** generate local ignored developer artifacts required for the task.
3. `check` **MUST NOT** commit or track those artifacts.
4. `check` **MUST** support aggregate execution through a CI-like profile.
5. Required external violations and tool execution failures **MUST** fail their
   operation; personal MANIFESTO findings do not relax that contract.

## 8.3 Supported profiles

CLI v1 **MUST** support at least:

* `tests`
* `coverage`
* `leaks`
* `java`
* `tidy`
* `format`
* `naming`
* `style`
* `repo`
* `doxy`
* `sphinx`
* `ci`

## 8.4 Canonical forms

The input-specific operation below is separate from ordinary configuration
profiles and their aggregate CI execution:

```sh
engels check prepared --input .ecosystem/prerelease/attempts/ATTEMPT/prepared/input.json
engels check prepared tool:cli --input .ecosystem/prerelease/attempts/ATTEMPT/prepared/input.json
```

Run it in the selected project; workspace selectors, `--theme` and `--changes`
do not apply. The product defaults to the manifest facade; an explicit artifact
must match the prepared product. `--input` is exclusive to this operation.

It checks version-1 Marx handoff structure, exact current manifest bytes, project,
artifact/facade/package identity and the existing manifest-derived distribution
selection. Recorded clean revisions must still be reliably clean and identical;
an absent revision remains explicitly unavailable. Artifact, companion, resource,
template and applicable ELF runtime membership is independently derived from
the current project rules. Every prepared size/digest is recomputed. Changing a
payload and rewriting its claimed digest cannot substitute different bytes for
the current reference. Prepared permissions must also match the reference. Unsafe
paths, symlinks, unlisted files, missing members,
collisions and inconsistent declarations fail.

The prepared payload remains read-only. ELF comparison uses private diagnostic
copies of current Release outputs, normalized with the same RPATH-removal rule
used by Marx. Native dependency scans use CMake script mode, not configuration or
compilation. Non-ELF target-runtime acceptance is not established. Diagnostic
work belongs under `.ecosystem/reports/prepared/`.

Actual project gates are repository conformance and formatting, strict Tidy of
the selected production closure using the existing Release compilation database,
and existing manifest-declared Release native tests. The existing Release cache
and developer graph must match the current canonical project configuration;
their presence does not prove compilation freshness. Engels does not generate a
build tree, build missing tests, acquire dependencies or import asserted success
reports. Missing databases/tools/test executables and incomplete test registration
are reported as `not_run`; skipped required native tests cannot become a pass.
Counted native tests require CTest's `--output-junit` capability (3.21+) and Python
3's standard XML reader. A project without declared native tests reports them as
`not_applicable`. Source build provenance is not certified by matching current
output bytes or executing existing tests.

JSON is written to stdout, including project/product identity, manifest/input
digests, measured prepared/reference digests, actual gate outcomes, commands and
limitations. Statuses distinguish `passed`, `failed`, `not_run`, `not_applicable`
and package/install/environment gates `deferred` until their representation exists.
Exit 0 means only `prepared_checks_passed`; exit 3 means incomplete applicable
checks, exit 5 a failed validation/check, and exit 2 a CLI usage error. The report
always has `release_ready: false`, `publication_authorized: false` and
`all_release_gates_passed: false`. Caller-supplied evidence is not used to bypass
checks. Neither this unsigned local report nor a `passed` field authenticates a
producer, proves an uncompromised build, or authorizes publication. REL-01 package
contents, clean installation and target-native released-representation checks
remain deferred. The independent release planner remains provisional even when
given this result as an evidence reference.

```text
engels check <profile> [--theme <theme>]
engels check <profile> <component:artifact> [--theme <theme>]
engels check <profile> --project <project> [--theme <theme>]
engels check <profile> --project <project> --project <project> [--theme <theme>]
engels check <profile> <project/component:artifact> [--theme <theme>]
```

Examples:

```text
engels check tests
engels check coverage
engels check leaks
engels check java
engels check tidy
engels check format
engels check naming
engels check style
engels check repo
engels check doxy
engels check sphinx
engels check sphinx --theme furo
engels check ci
engels check tidy --project ecosystem
engels check tidy --project ecosystem --project monitor
engels check tidy monitor/monitor_viewer:app
```

`doxy` derives an explicit input list from the selected manifest-owned C/C++ files,
including headers and companions. Project-wide runs also include existing root
`README.md`/`readme.md`; artifact selections exclude that project context and do not
scan unrelated source/docs/assets directories. Missing inputs fail before config
replacement. Project identity and version come from the manifest.

Project config is `.ecosystem/doxygen/Doxyfile`, with HTML in
`.ecosystem/doxygen/html/` and Breathe-compatible XML in `.ecosystem/doxygen/xml/`.
Artifact config/output is isolated under
`.ecosystem/doxygen/artifacts/<component>/<artifact>/`. Workspace project/group and
multiple qualified artifact selectors are supported. Root Doxyfiles are neither
read nor written. Basic docs use Doxygen's built-in parser without a prior build;
template overrides can use the current compilation-database binding when one
exists. Input values containing control characters or Doxygen environment
expansion syntax are rejected rather than reinterpreted.

The shared graph policy requires both Doxygen and Graphviz `dot`; `doctor doxy`
reports both. Missing tooling returns `missing_local_tooling` before generation.
Each run recreates only its selected `html/` and `xml/` directories, then requires
nonempty `html/index.html` and `xml/index.xml` files. A failed run can leave
incomplete generated output; it never counts old HTML/XML as fresh documentation.
Both output trees reject aliases before cleanup. Project and sibling artifact
output directories remain separate.

`doxygen.log` retains combined tool output and `warnings.log` retains Doxygen's
diagnostics. Terminal output names the config and both logs. Nonzero tool exits,
explicit error records in the shared diagnostic format, unreadable/unwritable logs,
and missing fresh HTML/XML return `task_failed`. Explicit errors include Graphviz
execution failures that Doxygen can report while exiting zero. Ordinary documentation
warnings remain nonfatal under the shared `WARN_AS_ERROR=NO` policy.

Configuration, HTML and log paths must not use symlinks, including aliases to
other files inside the project. Existing HTML descendants are checked before
cleanup; invalid path shapes fail before configuration replacement or execution.
The graph and parser worker counts are bounded by the shared template.


`coverage` runs instrumented tests in its separate coverage build tree. Each
test-enabled invocation clears old raw/merged profiles, the previous
`.ecosystem/reports/coverage.json`, `coverage.zip` and generated `.ecosystem/coverage/`
site before running tools. Failing tests, missing profiles, failed/empty merge,
nonzero LLVM export/show or malformed/incomplete output return `task_failed`.
The merge uses an input-file list; its exact exit status and diagnostics survive.
All selected test binaries contribute to LLVM export and detailed HTML. Only
manifest-owned production code, including instrumented headers/templates, appears
in coverage. The four measured metrics are lines, functions, branches and regions.
`coverage.json` retains structured LLVM data; `coverage.zip` contains the portable
summary and native HTML source pages. Commands, durations and native diagnostics
remain internal. Service aliases and incompatible types are rejected before
cleanup. Deploy consumes verified artifacts and never recomputes coverage.

`leaks` builds the selected test targets and their dependencies in the separate
`.ecosystem/build/project/desktop/debug/leaks/` tree. It does not build an unrelated
facade. Use `engels check leaks core:library` to select one artifact's
tests; project/workspace selectors retain their normal scope. The check requires
CTest, CMake and Clang with AddressSanitizer, UndefinedBehaviorSanitizer and
LeakSanitizer runtime support. An empty test selection or missing CTest registration
fails rather than claiming coverage.

The command enables leak detection and fail-on-error settings for its child test
run, overriding inherited disabling `ASAN_OPTIONS`, `UBSAN_OPTIONS` and
`LSAN_OPTIONS`. Detected leaks, address/undefined-behavior findings and ordinary
test failures return `task_failed`. Missing tools and recognized LeakSanitizer
inspection failures return `missing_local_tooling` with diagnostics. Run outside
debuggers, ptrace and tracing test wrappers when the sanitizer cannot inspect the
process; ordinary nested CTest execution is not itself rejected. Configuration
failures retain the compiler/CMake output and name the sanitizer prerequisites.

Success means **the selected sanitizer-backed test run reported no detected
leaks**. It covers executed paths only, not unrun tests or uninstrumented code;
program-supplied sanitizer suppressions/disabling hooks still limit detection.
CTest retains its output under the leak build's
`Testing/Temporary/LastTest.log`. Leak checks remain outside default `check ci`;
a successful selected leak check does not establish unexecuted runtime coverage.

`java` is a project-level binding check. It requires manifest-declared JNI support plus a conventional `java/` Gradle surface, builds the project's shared-library artifacts through the debug developer build tree, and runs `gradle test` with `-PnativeLibraryPath=<debug-build-dir>`.

`sphinx` is a project-level documentation build. Its only authored source is
`assets/docs/`, with exactly one `index.md` or `index.rst` entry point. Missing or
competing stable entries fail explicitly; there is no root `docs/` fallback.
All active links/includes must stay within the stable unit. It runs
fresh Doxygen HTML/XML generation and loads Breathe in its
service-owned Sphinx configuration. Markdown uses MyST. `--theme <theme>` overrides
the default `sphinx_rtd_theme`. Missing tools/modules, warnings, native failures
and absent/empty HTML indexes fail. Successful candidates replace
`.ecosystem/sphinx/html/`; failures retain the prior site and the current
`.ecosystem/sphinx/sphinx.log`. Optional `assets/docs/sphinx.json` contains only an `exclude_patterns` array;
excluded documents must still satisfy the stable unit boundary. See
[building documentation](documentation.md) for setup and generated API publication.

`repo` is the repository-boundary profile. It rejects committed legacy `Makefile`, `scripts/`, shell-wrapper, committed `Doxyfile`, generated coverage and benchmark report trees, generated Java report trees, extra CMake-helper scaffolding, and stale sync-owned tracked surfaces so managed repositories keep only project code, assets, `manifest.json`, and the slim tracked repository surface, including gitlink-backed managed projects whose parent Git metadata only exposes a project entry. `ci` includes `repo` before external-tool checks.


`naming` reports personal policy findings as successful advisory diagnostics.
It checks declarations in owned C/C++ files, including owned headers, plus owned
file and directory names. Artifact filters use full identity, and workspace
project/group/artifact selectors preserve that scope. References, comments,
strings and declarations in unowned dependency headers do not become project
symbol findings.

The bounds are more than four words for ordinary names, more than seven for test
symbols, and more than 25 characters for ordinary names. The test-symbol character
bound remains uncalibrated and disabled. Leading/trailing underscores, `m_` scope
prefixes and departures from snake_case are also reported. Existing
`docs/naming_allowlist.txt` or `naming_allowlist.txt` entries are sparse symbol
exceptions; they do not waive file or directory policy.

`.ecosystem/reports/naming.json` preserves the same findings shown in the terminal,
with artifact, file, line/column, rule, authority/enforcement and measurements.
Parser/input/report-write failures return errors and are distinct from findings.
Analysis uses the existing debug compilation database when available, otherwise
manifest include paths and the project language standard. For dependency-specific
flags, configure/build the project first. Declarations follow the active Clang
parse, so macro conventions, inactive branches and full semantic coverage remain
later hardening. Personal checks do not change compiler/tidy/format/test contracts.

`style` combines naming with missing control-flow braces, anonymous-namespace
locality and function/lambda measurements. Each anonymous namespace produces one
finding with its declaration count and effective source volume. Lambda bounds are
span >5, estimated complexity >2 and nesting >1; ordinary functions use span >60
and estimated complexity >10. Complexity is an AST branch estimate covering
conditionals, loops, case/catch branches and logical operators, not a full CFG.
Nested lambdas receive independent measurements. Macro-generated control bodies
with differing spelling/expansion locations are excluded from structural findings.
The ordinary-function nesting threshold remains undefined and disabled.

The style report indexes selected headers under `include/` as referents and joins
matching source/template/test/benchmark companions. Tests, benchmarks and explicit
entry points are excluded from production referent volume. Files without a matching
selected referent retain their file measurements. Tree modules contain descendant
referents; other subtrees do not inflate the tree. Depth and breadth hints use the
the Manifesto's logarithmic depth and square-root breadth targets, with count/volume
imbalance and the weighted local hint reported independently.

`.ecosystem/reports/style.json` contains findings, files, scopes, referents and
modules. Physical lines include comments and blanks; effective lines contain
non-comment tokens, including multiline string literals and preprocessor directives.
The terminal shows findings and the five largest selected files by effective lines.
Volume is descriptive: corpus-derived size thresholds have not been calibrated.
Artifact and workspace selectors, advisory success, input failure and report-write
failure follow the same policy as `naming`. Neither profile is added to the required
external-check aggregate by this change.


`tidy` requires `clang-tidy` and a valid `compile_commands.json` in the debug build
tree, with an entry for every selected source. Missing tools, missing or incomplete
configured inputs, and an empty source selection fail the required check. It runs
both the shared libclang analysis and `clang-tidy --warnings-as-errors=*` over the
selected artifact's owned sources, including tests; benchmark-only artifacts stay
excluded unless the requested or default artifact is benchmark-only. A component
namespace shared by multiple artifacts does not widen an artifact filter. Project-wide checks retain all
eligible owners.

`.ecosystem/reports/tidy.json` records execution or the reason
the analyzer could not run. Diagnostic failures preserve the report and print the
source/check messages in the terminal. Both compiler analysis warnings and tidy
failures remain errors. Tidy uses the configured compilation flags and existing
`.clang-tidy` policy; it does not apply semantic fixes.

Generated visitor and developer builds promote their selected compiler warnings
to errors (`-Werror` for GNU/Clang and `/W4 /WX` for MSVC), including generated test
targets. Owned `.c` sources and their companions participate in discovery and
compilation; C++-only warning options stay off C translation units. Required CTest
runs (`tests`, `coverage`, `leaks`) pass `--no-tests=error`, so missing test
registration cannot yield success. A selected artifact with no declared tests
already fails as unsupported.

Explicit native build/run selections generate their mandatory developer closure.
Generated test-directory definitions refer to an actual target in that graph,
so a selected nonvisual library can also build/test independently with CMake.
The test/coverage check profiles retain their existing project integration setup,
including runtime actors needed by CLI integration tests; their artifact filter
selects test ownership, not a new test-runtime dependency model.

`engels check ci` runs repository validation, the production facade and supported
required tests, applicable Java checks, tidy, then format. It stops when a required stage fails. A useful local
sequence is `marx format` followed by `engels check ci`; tests and semantic tidy
diagnostics still need their own corrections. Local command success does not
establish hosted workflow enforcement.

Coverage, leaks, documentation, benchmarks, packages and Android operations are
selected explicitly; their external tools are not prerequisites of `check ci`
unless the selected project's own tests need them. Existing optional reports and
authored documentation are preserved by the aggregate. Sphinx, Pages and specialized
benchmark presentation retain their own acceptance and publication policies.
An explicitly requested secondary operation still reports execution failure.
Sphinx rejects symlinks and incompatible file types in its generated service
paths before replacing configuration or invoking the builder.

---

## 9. `doctor`

## 9.1 Purpose

`doctor` provides manifest-aware and environment-aware validation before heavy tasks.

## 9.2 Rules

1. `doctor` **MUST** be user-friendly.
2. `doctor` **MUST** explain why a request is invalid or unsupported.
3. `doctor` **MUST** list valid alternatives when possible.
4. `doctor` **MUST** reject impossible requests before build execution.
5. `doctor` **MUST** inspect:

   * manifest compatibility
   * artifact kind
   * requested profile
   * local tool availability
   * tracked facade sync state
   * configure cache freshness for dependency-aware reports
   * manifest-declared package guidance for the selected project or artifact closure

## 9.3 Canonical forms

```text
engels doctor
engels doctor <profile>
engels doctor <component:artifact> --profile <profile>
engels doctor --project <project>
engels doctor --project <project> --project <project>
engels doctor <component:artifact> --project <project> --profile <profile>
engels doctor <project/component:artifact> --profile <profile>
```

Examples:

```text
engels doctor
engels doctor android
engels doctor frontend:app --profile android
engels doctor --project monitor
engels doctor debug --project ecosystem --project monitor
engels doctor ecosystem/marx:marx --profile debug
```

Doctor and `report toolchains` share one tool inventory. Doctor lists every tool
for the selected profile with required/optional status, its resolved absolute
executable path, version response and version-probe exit code. Missing required
tools and failed required version probes return `missing_local_tooling`; errors
from a failed version command are retained instead of presented as a version.
Without a profile, Doctor checks the desktop build tools. A separate `clang` is
optional because the existing CMake setup falls back to `clang++` for C.

The inventory includes clang-tidy, clang-format, CMake, Clang, CTest, Doxygen,
Graphviz, LLVM coverage tools, Sphinx and Java/Gradle tooling. `doctor ci` follows
its conditional test and Java stages. `doctor java` uses executable `java/gradlew`
before PATH Gradle, just like `check java`, and requires the same JNI/Gradle/shared
library surface. Java and Sphinx reject artifact filters; test profiles reject an
artifact with no tests. `doctor android` uses the selected Qt/SDK/NDK environment
and does not require an unrelated host Clang installation.

Format, repository, Doxygen, Sphinx and personal-check preflights do not configure
a build merely to refresh package diagnostics. They may read an existing package
cache; absent/stale cache data is reported as unavailable. Build-backed profiles
retain the configure refresh described below. Leak preflight states its sanitizer
runtime/tracing requirements; it does not execute a sanitizer test. Sphinx
preflight names its Python theme/Markdown requirements. Tool availability and
version responses are diagnostics, not a guarantee that a later build/run works
or a compiler/version compatibility certification.

`doctor` reports declared dependency guidance by scope. Without an artifact filter it summarizes the selected project scope; with `component:artifact` or `project/component:artifact` it summarizes only that artifact's linked component closure. Current guidance covers manifest-declared Qt, KDE, JSON, LLVM/Clang, JNI, OpenCV, Eigen, `cxxopts`, GTest, and Google Benchmark packages. Build profiles use their matching nested build tree under `.ecosystem/build/project/<platform>/<configuration>/<variant>/`, so compatible artifacts can keep reusing the same configured project cache instead of fragmenting into one directory per command. When that `CMakeCache.txt` is missing or stale relative to `manifest.json` and the generated local developer surface under `.ecosystem/source/`, `doctor` refreshes that configure data before reporting configured package state. For artifact-scoped refresh where the full project cache is unavailable or stale, `doctor` may instead materialize a derived probe cache under `.ecosystem/build/probes/<component>__<artifact>/<platform>/<configuration>/<variant>/` so unrelated project dependencies do not block the selected closure. Both paths now use the same manifest-derived package surface as tracked facade generation. `doctor` rejects sync-owned tracked-surface drift first, so generated `CMakeLists.txt`, `.gitignore`, and applicable `.github/` surfaces must match the current manifest-derived output before cache refresh is trusted.

---

## 10. `report`

## 10.1 Purpose

`report` runs heavy local analysis tasks and produces local reports.

## 10.2 Rules

1. `report` **MUST** be local-only.
2. `report` **MUST NOT** be required in cloud CI.
3. `report` **MAY** take a long time.
4. `report` **MUST** generate machine-readable output.
5. Additional human-readable formats **MAY** be rendered from the canonical report output.

## 10.3 Supported report kinds

CLI v1 **MUST** support at least:

* `cxx`
* `naming`
* `style`
* `toolchains`
* `matrix`

The optional `changes` report inspects CI relevance locally:

```sh
engels report changes --json
```

It reads `.ecosystem/github/changes.json` produced by the event adapter, verifies
the checkout, history and complete path/status list against Git, and emits JSON
without configuring, building or modifying the project. It accepts a project
root, with no artifact/workspace filter. Missing, malformed, dirty, stale or
incomplete inputs return a conservative `full` plan with a reason; a successful
report command means the plan was produced, not that verification passed.
Git validation proves consistency of the selected comparison; it does not
authenticate a GitHub event. Event provenance remains the adapter's responsibility.

The plan identifies each path's kind and owners, affected project/artifact IDs,
the transitive dependent closure and existing changed formatting inputs. Manifest,
build/workflow policy, assets, platform inputs, unknown files and historical
deletions/type changes retain full checks. Authored ownership and physical C++
aliases take precedence over documentation names. Known presentation-only inputs
need no C++ build/format/CodeQL in the plan; repository and coverage policy remains
full. For code changes CodeQL still requires a coherent full database. This report
is also inspectable locally; cheap Checks formatting consumes the same policy
directly. Its `execution.codeql` decision is `repository` only for validated
non-code/empty PR deltas and `full` otherwise. The CodeQL workflow consumes it and
retains separate delta evidence. Full Checks remain required unless verified
evidence satisfies their reuse contract.

The toolchain JSON inventory preserves each tool's `available`, `path` and
`version` fields and adds `label`, applicable `profiles`, `required`,
`version_exit_code` and `version_error`. Missing tools have empty paths/versions
and probe exit `-1`. The inventory is informational and includes dormant tools;
only Doctor's selected profile decides which missing tools block that request.
Workspace reports describe the host inventory; project-specific Gradle wrapper
selection is inspected by Doctor.

## 10.4 Canonical forms

```text
engels report <kind> [--json]
engels report <kind> <component:artifact>
engels report <kind> --project <project>
engels report <kind> --project <project> --project <project>
engels report <kind> <project/component:artifact>
```

Examples:

```text
engels report cxx
engels report cxx frontend:app --json
engels report style frontend:app
engels report naming --group apps --json
engels report toolchains
engels report matrix --project ecosystem
engels report matrix --project ecosystem --project monitor
engels report matrix ecosystem/marx:marx
```

## 10.5 `report cxx`

1. `report cxx` **MUST** analyze only manifest-declared C++ source files.
2. Undeclared generated or stray source files **MUST NOT** be pulled into analysis implicitly.
3. Benchmark-only components **MUST** stay excluded from project-wide `report cxx` and `check tidy` runs unless the selected artifact is benchmark-only.
4. The canonical JSON output **MUST** expose file-category totals and per-component summaries in addition to raw diagnostics.

5. Artifact filters select the complete `component:artifact` owner, including when
   several owners share a component namespace. Each source records its artifact;
   component totals aggregate that namespace intentionally.
6. Configure, missing/non-file input, invalid compilation database and parse errors
   fail the operation. A report is persisted with `status=failed`, including when
   configuration prevents analysis. A completed analysis containing warnings has
   `status=findings`; required `check tidy` and build enforcement remain separate.
7. C++ analysis configures the shared project build tree. Its findings attribute
   the actual diagnostic location and the translation unit's selected artifact.
   Naming/style cover header-only owned scopes without requiring a translation unit.

## 10.6 Personal reports and shared output

`report naming` and `report style` run the same advisory analyzers as their check
profiles. They accept project, group and qualified artifact selection. Style also
includes naming, file/scope measurements, referent companions and reduced module
trees. Undefined thresholds stay absent.

C++ and personal findings share `rule`, `category`, `severity`, `authority`,
`enforcement`, `artifact`, `entity_kind`, `entity`, `referent`, `file`, `line`,
`column`, `message`, `measurements` and `thresholds`. An unavailable referent is
`null`. Personal authority/enforcement is `manifesto`/`advisory`; Clang diagnostics
retain `external`/`required` and their original severity. `errors` records incomplete
analysis independently of findings. Status is `clean`, `findings`, or `failed`.

Diagnostic reports default to terminal rendering grouped by category and severity.
`--category <name>` and `--severity <name>` select displayed findings; the saved
JSON, overall status, analysis failures and scope measurements remain complete.
Empty matches show zero displayed findings without implying a clean analysis.
`--markdown` renders the same snapshot for CI, including entity/referent details,
raw measurements and thresholds. It can be appended to a GitHub step summary:

```bash
engels report style core:core --markdown >> "$GITHUB_STEP_SUMMARY"
```

`--json`
emits that JSON alone on stdout, with configure output captured inside failure
reports. Matrix/toolchains keep their JSON output, and accept `--json` explicitly.
Toolchain inventory does not accept artifact filters. Display options apply only
to `cxx`, `naming` and `style`; `--json` rejects display filters and `--markdown`.

Pages renders an explicitly selected project snapshot with category/severity
controls, entity/referent details and a complete JSON download:

```bash
ECOSYSTEM_SPHINX_DIAGNOSTICS=.ecosystem/reports/style.json engels check sphinx
```

The selected snapshot must match the manifest's project, use the canonical
diagnostic fields, and stay within the project and the 8 MiB snapshot limit.
It is labeled a local snapshot; verified CI results still require the separately
selected, validated receipt. Without scripts, all finding groups remain readable.
Invalid snapshots preserve the previously published site.

Project reports are saved as `.ecosystem/reports/<kind>.json`. Workspace reports
are saved as `.ecosystem/reports/workspace_<kind>.json`; their `projects` array
retains each selected artifact request separately, including multiple requests in
one project, with project/root/artifact identity and independent status. A failed
peer fails the workspace operation while retaining successful peers. Invalid
analysis selections fail before writing; matrix inspection continues to expose
invalid projects. Report-write failures fail the operation.

---

## 11. Report format

1. The canonical report format **MUST** be `json`.
2. Other formats such as `md` or `csv` **MAY** be rendered from canonical `json`.
3. The tool **MUST NOT** fully rerun the same analysis separately for each output format.

---

## 12. Local developer artifacts

The CLI **MAY** generate local developer artifacts inside the project working tree.

These artifacts **MAY** include:

* full developer CMake surface
* internal Doxygen config under `.ecosystem/doxygen/`
* local Sphinx config
* local reports
* local build trees
* IDE support files

Rules:

1. These artifacts **MUST** be generated by the Ecosystem tool.
2. These artifacts **MUST NOT** be edited manually.
3. These artifacts **MUST** remain ignored by Git.
4. These artifacts **MUST NOT** be committed.
5. Shared generated text surfaces **SHOULD** come from repo-owned template assets rather than long embedded string literals in the implementation.
6. Sync-owned settings, including optional ignored `.clang-format`/`.clang-tidy` and tracked generated `.github/` workflows **MUST NOT** be rewritten through local-artifact materialization.
7. `check doxy` **MUST** fail on template/configuration-write errors before invoking Doxygen. Render failure preserves the existing service-owned `Doxyfile`; config paths escaping the project through a symlink are rejected.

Commands that **MAY** materialize such artifacts:

* `build`
* `check`
* `doctor`
* `report`

`sync` **MUST NOT** use this mechanism to expand tracked facade surfaces.

---

## 13. Forbidden public commands

The CLI **MUST NOT** expose the following as public commands in v1:

* `generate full-cmake`
* `generate dev-cmake`
* `ci` as a top-level command

Rationale:

* tracked generation is handled by `sync`
* CI-like execution is handled by `check ci`

---

## 14. Error classes

The CLI **MUST** distinguish at least these error classes:

* `invalid_request`
* `unsupported_by_manifest`
* `missing_local_tooling`
* `task_failed`

These error classes **SHOULD** map to stable exit codes.

---

## 15. Minimal public interface

CLI v1 is defined by the following interface:

```text
engels list
marx sync
marx mutate ...
marx format [artifact] [--project <project> ...] [--group <group> ...]
marx build <profile> [artifact] [--project <project> ...] [--group <group> ...]
marx run <profile> [artifact] [--project <project> ...] [--group <group> ...] [--android-mode <auto|emulator|device>] [-- <arg> ...]
marx benchmark [artifact] [--project <project> ...] [--group <group> ...] [-- <arg> ...]
engels check <profile> [artifact] [--project <project> ...] [--group <group> ...]
engels doctor [profile] [artifact] [--project <project> ...] [--group <group> ...]
engels report <kind> [artifact] [--project <project> ...] [--group <group> ...] [--json]
```

In workspace mode, `artifact` **MAY** use the qualified form `<project>/<component>:<artifact>`, and `--group <group>` **MAY** expand named subsets from `ecosystem.workspace.json`. Unqualified artifact filters still resolve to exactly one selected project.

---

## 16. Command ownership model

* `sync` owns tracked facade generation
* `mutate` owns structural changes
* `build` owns local developer/platform builds
* `run` owns runnable-artifact execution
* `check` owns quality tasks
* `doctor` owns preflight validation and friendly diagnostics
* `report` owns heavy local analytical runs
* `list` owns manifest-derived discovery
