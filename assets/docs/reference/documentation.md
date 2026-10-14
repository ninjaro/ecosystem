# Building documentation

This maintained reference unit has only internal document dependencies. Its API
contracts are authored text: reading or rendering them does not require source
headers, generated API XML, planning records or verification receipts. The project
presentation command described below additionally composes implementation-derived
API and results; that output is separate from this unit's authored specifications.

Install Doxygen and Graphviz, then install Sphinx, Breathe, MyST and the chosen
theme in the Python environment that provides `sphinx-build`. For example:

```sh
python3 -m venv .ecosystem/venv/docs
.ecosystem/venv/docs/bin/pip install sphinx breathe myst-parser sphinx-rtd-theme
export PATH=".ecosystem/venv/docs/bin:$PATH"
engels doctor sphinx
engels check sphinx
```

Author exactly one `assets/docs/index.rst` or `assets/docs/index.md`. This is
Sphinx's only authored source. Root-level `docs/` holds working material and is
never searched, rendered or copied into the site. Missing or competing stable
entry points produce a documentation-source error. Markdown uses MyST; an
RST-only unit needs no Markdown parser.

All active document links, anchors, includes and embedded inputs must resolve
inside `assets/docs/`, including nested dependencies. The canonical boundary
validator runs before rendering and in the generated Sphinx configuration.
Source imports, executable authored configuration and outward references are
rejected. Generated configuration and HTML live under `.ecosystem/sphinx/`,
never in the authored unit.

No `sphinx.json` is needed by default. When source selection actually requires
configuration, author it only inside `assets/docs/`. The sole supported field is
`exclude_patterns`, an array of nonempty root-relative glob strings. Malformed
settings fail before rendering. Exclusions do not exempt documents from the
self-containment contract; plans, research and history belong outside the unit.

`check sphinx` first runs the same project-wide Doxygen operation as `check doxy`.
Doxygen generates fresh HTML and XML from manifest-owned implementation inputs.
The generated project-results section publishes the standalone API reference
separately from the authored specifications. Authored pages describe stable API
contracts directly; they do not import source declarations through Breathe or
autodoc. The generated configuration retains Breathe for the publication tooling.

Sphinx runs with a fresh environment and treats warnings as failures, including
unresolved document references. `--theme alabaster` selects Sphinx's built-in
theme; otherwise `sphinx_rtd_theme` is the default. Missing Python extensions and
themes are reported with native diagnostics in `.ecosystem/sphinx/sphinx.log`.
Doxygen diagnostics remain in `.ecosystem/doxygen/doxygen.log` and `warnings.log`.

Successful builds publish `.ecosystem/sphinx/html/index.html` and its accompanying
pages. The builder writes a fresh `html.pending/` candidate first, so an old index
cannot stand in for missing output. A failed build keeps the last successful
`html/` tree; consult the command status and current log before publishing it.
Generated path aliases and incompatible types fail before cleanup. A retained
`html.previous/` directory indicates interrupted publication recovery and must be
restored or moved before retrying.

Every public page links to **Project results** at `_ecosystem/index.html`, using
the selected Sphinx theme. This section includes the fresh standalone Doxygen
reference, selected verification reports, available successful benchmark output,
and media listed in `assets/showcase/index.tsv`. The `_ecosystem/` documentation
namespace is reserved for this generated section. Composition completes inside
the pending site, so a failed composition preserves the
previous documentation and reports together.

Local docs builds do not automatically consume old coverage or check reports.
To include a selected Checks, combined publication or manual Pages receipt, set
`ECOSYSTEM_SPHINX_EVIDENCE` to its generated report directory within the project when invoking
`engels check sphinx`. The renderer checks the project, current Git tree, clean
tracked inputs, complete report set, sizes and SHA-256 hashes. It displays source
revision/run facts and computes coverage percentages from LLVM covered/total
counts; unsupported coverage is explicit. The caller remains responsible for
authorizing the source workflow/jobs/artifact before selecting downloaded evidence.
Rendering a local receipt does not authenticate GitHub verification.
Selected evidence and diagnostics cannot come from root-level `docs/`; working
documents never become publication inputs indirectly.

Benchmark snapshots come from successful `marx benchmark` results belonging to
currently declared artifacts. They are labeled separately from verification and
do not claim to describe the current revision. Showcase entries preserve TSV row
order and descriptions, with image previews, explicit video playback and PDF links
with first-page previews. Video posters use the first frame. Selected PDFs require
`pdftoppm` (Poppler); selected videos require FFmpeg. Missing tools or failed
conversion stop the build and preserve the previous site. Originals remain unchanged.
Previous/next buttons and keyboard arrows/Home/End select entries; videos pause
when leaving their slide. Without scripts, the ordered list remains available.
Only indexed files are copied. Logs/descriptions are escaped as text. Invalid selected
inputs, path traversal, symlinks and report/media inputs exceeding 256 MiB fail
composition. Fresh native Doxygen HTML is streamed separately, with a 1 GiB limit.
Removed media/reports disappear on the next successful fresh build.

Project results links each indexed asset category at
`_ecosystem/catalog/<category>/index.html`. Its optional `readme.md` renders with
MyST around a `<carousel/>` block outside code; absent readmes provide the carousel
alone. Relative links resolve to the category's selected files. Register and reorder entries with
[`marx mutate assets`](cli.md); the presentation build preserves sources
and index order.

For authored root-level `tex/*.tex` documents, run `marx build tex` before
`engels check sphinx`. The TeX build requires `latexmk` and `pdflatex`, generates
PDFs under `assets/showcase/tex/` and registers them through the asset controller.
The documentation build then renders their first-page previews through Poppler.
TeX generation is a separate presentation operation; ordinary C++ builds and
Engels checks do not regenerate authored showcase files. See the
[`build` reference](cli.md#7-build) for ownership and rebuild behavior.

The generated documentation setup action installs Breathe with the other Python
documentation dependencies in an isolated virtual environment only when
`install-docs` is enabled; it does not modify the runner's system Python.

Generated Pages runs when either Checks or CodeQL completes for a successful push
to the current default-branch tip. It requires both workflows' latest matching
attempts to succeed and validates their fresh receipts or original PR evidence.
Reports, coverage and both source identities are combined into a hashed
presentation snapshot. Full verification receipts also include hashed event-delta
and native-classification inputs bound to the tested revision and manifest. These
inputs describe relevance; they do not replace successful execution of the required
full stages. Receipts without these inputs require fresh verification.
Missing, expired or changed evidence defers publication;
the other workflow's completion can retry selection. Reusable receipts require
local setup with checkout bootstrap or an external tooling ref pinned to a full
commit SHA. Mutable tooling refs keep fresh verification but cannot enable
automatic publication.

Automatic Pages checks repository state without repairing it, then builds
Doxygen/Sphinx using the selected evidence. It does not repeat tests, coverage or
CodeQL. The source runs, attempts, artifact identities and current default-branch
tip are checked again after docs and before deployment. A final build-job tip
check also rejects revisions superseded during artifact upload. Only the deployment
job receives Pages write/OIDC permissions; its concurrency group serializes
deployments. Checkouts are pinned to the selected revision without persisted
credentials. Scheduled CodeQL does not trigger publication.

Manual default-branch dispatch remains available. It checks repository state,
runs coverage when the manifest supports tests and freezes those successful
results before Sphinx runs. The page identifies these narrower prerequisites;
it does not claim that a manual dispatch performed full Checks/CodeQL verification.
Both paths require the selected snapshot and a successful current Sphinx build
with a composed results page. Missing docs, failed stages, incomplete result
records and retained publication recovery state block deployment, even if old
HTML exists. Diagnostic reports and native logs are retained on failure.

The native integration regression is opt-in so ordinary C++ checks need no Python
documentation environment. After building the test target, run:

```sh
.ecosystem/build/project/desktop/debug/default/core__core__tests --sphinx-integration
```

It exercises native Doxygen HTML/XML, separately published API symbols/comments,
Markdown and RST pages, exclusive stable-source selection, theme selection,
unresolved references, and preservation of a prior successful site on failure.
Its media fixtures also require FFmpeg and `pdftoppm` (Poppler).
