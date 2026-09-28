![Logo](assets/brand/logo.svg)

# MANIFESTO

MANIFESTO defines the structure and working conventions of managed C++ projects. Companion repositories built around other ecosystems are not required to reproduce this topology.

## 0. Tree and Blocks

This section fixes the structure of the project: from source code to artifact, peripheral, service, and auto-generated blocks. If a block is present in the repository, its place and role are considered fixed. File names should not scream at me: wherever a name is not fixed separately, `snake_case` is used.

### Project Seed

A project must have both structure and meaning. `manifest.json` defines its formal side and stores its meta-details. `readme.md` reveals its idea and soul and must not be reduced to a dry run guide.

The `core/` and `app/` blocks contain the project’s source code. The presence of at least one of them is required. Both blocks use the same internal structure: `include/`, `src/`, `tests/`, and, where appropriate, `benchmarks/`. These directories describe related views of the same source topology rather than four trees that must contain the same number of files. `include/` is the reference layer; the other layers mirror its paths and naming where the relationship is real and useful to a human reader. One declared responsibility may therefore have one implementation file or a whole bunch of them without becoming several responsibilities by bureaucratic magic.

* `include/` defines the reference layer of this structure and contains declarations and interface material. Implementations do not belong here merely because C++ occasionally makes that inconvenient. Conventional include-side template implementations may be tolerated where the inclusion model is deliberately preferred, but they are not the preferred shape.

* `src/` contains the block’s working implementation files, including template implementations and `entry-point` sources.

* `tests/` contains test files. Independently observable behaviour declared in `include/` belongs under test. A test is named and placed for the interface or behaviour it intends to validate, not for every dependency that happens to execute underneath it.

* `benchmarks/` is an optional directory for benchmark files and is introduced only where performance measurements are actually justified.

For files in `tests/` and `benchmarks/`, the same general principle is used: the referent name remains recognizable, while the layer type is expressed by the `_tests` or `_benchmarks` suffix. Several files belonging to the same referent prefer meaningful `snake_case` qualifiers such as `foo_errors_tests.cpp` or `foo_recovery_tests.cpp`; numbered batches remain acceptable where the parts are deliberately homogeneous and a semantic name would only pretend to know more than it does.

The distinction between `core/` and `app/` is semantic rather than structural. The `core/` block contains non-windowed project code. Code in `core/` may form a reusable library surface, including use outside the repository, while its `entry-point`s provide whatever runnable surface is appropriate to the project stage – for example a CLI, an executable example, diagnostics, or another compact interface. The `app/` block is intended for a windowed interface. Dependencies between these blocks are asymmetric: `app/` may depend on `core/`, whereas `core/` must remain independent of `app/`.

Qt Widgets is the ordinary and preferred Qt application path. When QML is genuinely useful, authored QML belongs under `app/qml/` as an optional interface surface of the same application; it does not create another top-level source block, another application kind, or replace the `include/`, `src/`, `tests/`, and `benchmarks/` structure. A Widgets-only application has no reason to carry QML machinery merely because the ecosystem knows how to support it.

The tree does not require block-local operational README files. Runnable surfaces expose usage through their own help or manual interface, while reusable APIs are documented from their declarations.

### Asset Topology

The `assets/` directory has been designed to serve as the project’s artifact layer. It is not intended for arbitrary files and must not be used as some rubbish dump: the contents of `assets/` are grouped by purpose through a fixed first-level directory layout.

Within the project’s working environment, this directory is also treated as a source for copying or linking into `build/`, `.ecosystem/`, and other service environments; `assets/showcase/` is excluded from this rule, as it is oriented primarily toward documentation and project presentation.

* The `assets/brand/` directory is intended for the project’s visual identity. The preferred format here is `svg`, since it preserves the possibility of meaningful manual editing while remaining readable as code. This directory is also the home of `favicon.ico`.

* The `assets/showcase/` directory is intended for stable project artifacts: screenshots, screen recordings, demo materials, PDF files, and other media. Such files may be used, for example, in documentation and `readme.md`.

The contents of `showcase/` are treated primarily as artifacts in their own right rather than as line-oriented files meant for manual editing.

* The `assets/reports/` directory is intended for reports and run results that are deliberately retained as project artifacts. Its contents are generated automatically and may remain informative not only on their own, but also when compared across versions. The presence of `assets/reports/` can be fixed conveniently via `assets/reports/.gitkeep`. Files inside `reports/` use a suffix-based naming scheme: the current version is marked with the `--latest` suffix, while versioned snapshots use a suffix of the form `--YYYYMMDD-HHMMSS`.

* The `assets/dataset/` directory is intended for input data sets used in tests, examples, and related validation scenarios. The naming at the first level of nesting is fixed: `test/`, `train/`, `stress/`, and `todo/`. These branches are optional in general, but if `dataset/` is not empty, `test/` must be present. Below that layer, the structure is left unconstrained: both a flat file layout and arbitrary further subdivision into subdirectories are permitted.

* The `assets/dumps/` directory is intended for auto-generated outputs, including results produced from runs over data sets in `dataset/`. The first level of nesting in `dumps/` must mirror the first level of `dataset/`; in other words, `dumps/test/`, `dumps/train/`, `dumps/stress/`, and `dumps/todo/` are only valid as reflections of the corresponding input branches. Below that layer, the structure remains unrestricted.

At the same time, an explicit escape hatch is reserved here: despite the strict structural rule, `assets/.gitignore` may still ignore any subset of `dumps/`, including first-level directories.

* The `assets/sets/` directory is intended for artifacts that the application uses and renders at runtime, together with the basic metadata and settings associated with them. These may include themes, portable serialized state representations, configuration sets, and other runtime artifacts that the program treats as ready-made input entities.

* The `assets/templates/` directory is intended for file-based templates owned and used by the project when generating data, text, or other derived artifacts. The general rule here is simple: long template strings live in dedicated files rather than directly in code. The internal structure of `templates/` is left unconstrained and may branch arbitrarily if that helps organize the templates themselves.

Within any first-level directory inside `assets/`, the meta-pair `index.tsv` and `readme.md` may be used whenever an ordered listing of media or runtime artifacts is needed. The `index.tsv` file defines the subset of files that participates in a carousel, listing, or any other derived representation; the row order in the table also defines the display order. The table uses the columns `id`, `path`, `type`, `description`, and `datetime`.

In the `path` column, the file is referenced by its relative path from the directory that contains `index.tsv`; for example, if the table is located at `assets/dataset/index.tsv` and the file is located at `assets/dataset/test/some_dir/example1.ex`, then the value stored in `path` is `test/some_dir/example1.ex`.

If `readme.md` is present next to `index.tsv`, it must contain a `<carousel/>` block. If `readme.md` is absent, this is treated as if a virtual `readme.md` existed next to `index.tsv` and contained nothing but that block. Carousels and similar auxiliary documentation pages are generated automatically from this pair of files. Supported artifact types include images, videos, and PDF files.

The `index.tsv` file must not be edited manually and is generated only through the controlling CLI tool.

TODO: project documents — decide whether temporary requirements, architecture notes, and other formal project documents receive a conventional `assets/docs/` home, and what should remain there once the corresponding requirements have been absorbed into code, tests, and generated API documentation.

### Generated and Service Blocks

The files `license`, `citation.cff`, and `CMakeLists.txt`, together with the `.github/` directory, form a related group of auto-generated blocks whose contents are derived from templates with data substituted from `manifest.json`. The overall structure of these blocks remains predictable, while their final contents are determined by the description of the particular project and may therefore differ from one repository to another.

These blocks are derived artifacts, not independent points of configuration, and are not edited manually. See the section on `manifest.json` for details.

A project may contain up to three `.gitignore` files. The root `.gitignore` is common to all projects and is generated automatically. A second `.gitignore` may appear in `bindings/`; it is auto-extended according to the languages and toolchains actually used there. The third may appear in `assets/`, and this is the only `.gitignore` that may be edited manually.

Even then, it is used exclusively for filtering subsets of `assets/dumps/`, so that the repository keeps the right balance: first and foremost, the project is a source-code repository, not a storage site for automatically generated dumps, however useful those dumps may sometimes be as examples or reference artifacts. This exception does not make `assets/` a rubbish dump.

The files `.clang-tidy` and `.clang-format` form a related pair of auto-generated root-level templates. Like the root `.gitignore`, they belong to the shared project layout rather than to the project-specific layer derived from `manifest.json`, so their contents remain the same across repositories. Together they define the common linting and formatting baseline used by the CI workflows described in `.github/`.

Both files are covered by `.gitignore` and are not committed, while remaining part of the generated project layout.

When present, the `build/` and `.ecosystem/` directories form a related pair of local service workspaces at the project root and remain covered by `.gitignore`. Unlike blocks generated directly from fixed templates, these directories are shaped primarily by the actual execution of build and control flows.

They contain intermediate states, caches, debug and diagnostic output, generated artifacts, and other transient by-products as required by active workflows. Tooling surfaces used only for development, analysis, documentation, automation, or CI – including fuller build descriptions and generated documentation configuration – are materialized there when needed. Such files belong to the active environment rather than the meaningful source tree; the tooling that creates them defines their concrete ownership and entry paths.

### Peripheral Blocks

These blocks are not mandatory, yet whenever they appear they become integral parts of the project and occupy their fixed places in the tree.

* The `bindings/` directory is intended for integrations and wrapper layers around the main project for other languages and external ecosystems. Its first level of nesting is organized by target environment or language; concrete subdirectory names are fixed by the support implemented by the project.

In this context, a `java/` directory inside `bindings/` denotes the JVM block broadly rather than Java code in the narrow sense alone, so Kotlin code inside `bindings/java/` is normal.

The `bindings/` block receives automatic `.gitignore` updates based on the languages and toolchain environments used in the project; in that sense, it forms a local ecosystem of its own, somewhat less dependent on the repository’s outer structure as a whole.

* The `shim/` directory is intended for thin external shims invoked by the main code only where such separation is genuinely justified. It is not a place for user-facing scripts, development utilities, build or run wrappers, or a substitute for the facade; for that reason, the names `scripts/` and `Makefile` are forbidden for this kind of block, since they blur distinct roles.

The contents of `shim/` are not invoked manually in the ordinary workflow. Main project logic remains in C++; `shim/` contains only external integration points that genuinely require a separate shim.

* The `tex/` directory is intended for TeX sources and their related materials. Automatic PDF generation is performed only for `.tex` files located directly in the root of `tex/`; `.tex` files inside nested subdirectories are not processed automatically by default. The generated PDF artifacts are placed into `assets/showcase/` and added to `assets/showcase/index.tsv` automatically.

The `tex/` directory may also contain any materials required for successful generation, including styles, bibliography sources, images, and other supporting files; its internal structure is otherwise left unrestricted.

## 1. Facade and Modes

A project must be able to present itself through a short and predictable build surface. The `facade` is the project’s promise to a `visitor`: after cloning the repository, the canonical three-command path produces a runnable `mvp` without requiring the visitor to learn the development machinery first.

The facade also clarifies the roles from which a project is approached. A `user` interacts with ready-made artifacts and does not enter the repository. A `visitor` clones the repository and follows the facade path, but does not enter development mode. A `developer` enters the fuller build, diagnostic, and control surface, whether or not they are currently modifying source code.

`readme.md` reveals the project’s idea, purpose, and soul; the facade verifies that the project is alive.

It narrows what the generated build surface materializes, not what the repository contains. Source code, tests, benchmarks, assets, and other project material remain present, and the developer does not distort natural project architecture merely to make the facade smaller.

The canonical facade path is fixed as:

```bash
cmake -S . -B build
cmake --build build
./build/mvp
```

The `mvp` is the visitor-facing runnable artifact. It guarantees a meaningful run while hiding the project’s internal artifact kinds and canonical artifact names from the visitor contract.

A visitor following this path does not deal with `.clang-tidy`, `.clang-format`, `Doxyfile`, private toolchain decisions, CI scaffolding, extended profiles, or other service noise.

Facade minimality is measured by visitor burden rather than by the smallest possible number of compiled targets. Its generated `CMakeLists.txt` remains short and readable, and the facade never justifies making natural dependencies optional or introducing extra abstraction solely to exclude them.

The facade is generated project state and is regenerated rather than authored. If an unsupported environment requires a small local adaptation, a visitor may patch the generated facade locally; environment-specific preferences do not become committed project policy, while defects in the generated facade belong in MANIFESTO itself.

Development mode continues from the same project at greater depth. It materializes tests, benchmarks, extended configurations, diagnostics, reports, documentation tooling, additional checks, and other service surfaces as required. The development surface is tooling-owned, exists locally or temporarily in CI, and remains environment-bound rather than authoritative as editable project state.

Once materialized, it remains inspectable and usable through ordinary terminal, IDE, and build tools; it does not require project-specific handwritten wrappers or a second handwritten CMake truth.

Local verification and GitHub-side automation apply the same project policy rather than separate workflows with separate meaning. Their generated files and build trees remain discoverable inside the project’s service workspaces, while the exact internal layout of those workspaces remains an implementation detail rather than part of the facade contract.

## 2. Code and Styles

Style is not treated here as a cosmetic layer. It includes naming, source placement, decomposition, control flow, documentation, and the local shape of code because these choices determine how easily the project can be read, navigated, tested, and changed.

The rules in this section define the clean state of project code and the properties worth observing. They describe the model, not the implementation of a particular checker. A structurally ugly project may still compile perfectly well; compilers have never claimed to be literary critics. File splitting, new directories, helper extraction, or new abstractions are justified only when they clarify responsibility, locality, or behaviour – never merely because they make one metric smaller.

### 2.1 Referents and Modules

The primary structural unit of the source tree is the `referent`: a project-owned declaration header under `include/` that defines a named entity or a coherent group of entities and serves as the point of reference for related files.

Files in `src/`, tests in `tests/`, and benchmarks in `benchmarks/` are interpreted in relation to a `referent`. Template implementations belong to the implementation side of that relation and normally live under `src/`. Within `core/` and `app/`, every project-owned C/C++ source unit is therefore a `referent`, a companion of a `referent`, or an `entry-point` containing `main()`.

For a referent `r`, the files associated with it inside one non-reference layer form that layer’s `bunch` of `r`. A bunch may contain one file or many. Splitting an implementation, test, or benchmark file does not create another referent by itself. MANIFESTO is not Java with a linker attached: a new translation unit does not automatically deserve a new header and a ceremonial abstraction of its own.

Mirroring is intended to be obvious rather than bijective. Related files preserve the referent’s complete `snake_case` stem as their natural naming prefix and follow the same relative path where that path still expresses the same responsibility. Paths are allowed to do useful work; a flat heap of ever-longer prefixes is not a substitute for structure. Common prefixes between nearby referents are normal when the concepts are genuinely related, but a developer should not need semantic archaeology merely to guess which interface a file belongs to.

The meaningful source tree is treated as a `referent-tree`: its terminal nodes are `referent`s, while directories that organize them are `module`s. A directory qualifies as a `module` if and only if its subtree contains at least one descendant `referent`; a `module` may contain `referent`s directly, nested `module`s, or both. Subtrees that contain no `referent` are ignored when source structure is evaluated.

Directories express semantic grouping rather than file-count management. A flat heap of similarly named or prefixed files is undesirable for the same reason as an artificial chain of one-child directories: both increase navigation cost without clarifying responsibility.


### 2.2 Structural Shape

Let `T` denote the reduced `referent-tree`, obtained by excluding every subtree that contains no `referent`, and let `M(T)` denote the set of all `module`s in `T`.

For a node `v` in `T`, let `R(v)` denote the number of descendant `referent` nodes in the subtree rooted at `v`, let `B(v)` denote the number of direct children of `v` in `T`, and let `H(v)` denote the maximum depth from `v` to a descendant `referent`, measured in edges.

For a `module` `v` with direct children $c_1, \ldots, c_{B(v)}$, define

```math
p_i = \frac{R(c_i)}{R(v)}.
```

The local referent-count imbalance is zero for $B(v)\le1$:

```math
I_R(v)=0.
```

For $B(v)>1$, it is

```math
I_R(v)=1+\sum_{i=1}^{B(v)} p_i \log_{B(v)} p_i.
```

This is the complement of a normalized Shannon-entropy balance signal: it is minimal when descendant `referent`s are distributed evenly across the immediate children and increases as responsibility becomes concentrated in fewer branches [ref-entropy-balance]. The same general balance problem for multifurcating trees is also addressed by Colless-like indices [ref-colless-like].

The soft ideal depth is

```math
H_{\mathrm{ideal}}(v)=\max(1,\lceil \log_{\max(2,B(v))}R(v)\rceil).
```

The lower bound of one follows from measuring depth in edges: a `module` with one direct `referent` already has depth one.

For a measured value `x` and positive soft target `t`, define the relative excess

```math
E(x,t)=\max(0,\frac{x-t}{t}).
```

Depth excess is

```math
D(v)=E(H(v),H_{\mathrm{ideal}}(v)).
```

To detect excessive flatness, define

```math
B_{\mathrm{soft}}(v)=\lceil\sqrt{R(v)}\rceil,
```

and

```math
W(v)=E(B(v),B_{\mathrm{soft}}(v)).
```

The soft width of a module is defined by the square-root rule. It makes increasingly wide flat growth visible without replacing semantic decomposition with a fixed directory-size constant. Software-architecture literature independently treats decomposition and component balance as analyzability concerns [ref-analyzability].

The local structural hint is

```math
L(v)=\frac{1}{2}I_R(v)+\frac{1}{3}D(v)+\frac{1}{6}W(v).
```

The `3:2:1` weighting gives referent imbalance precedence over depth and width while keeping all three structural effects visible. `L(v)` is a structural model, not a normalized quality score and not a specification of one scanner algorithm. Implementations preserve the meaning and direction of the model; harmless rounding or extraction differences do not become philosophical crises over two lines or one file.

### 2.3 Volume and Navigation

Tree shape cannot reveal an oversized terminal `referent`, so textual volume is measured independently from topology.

For a source file `f`, let $P(f)$ denote its physical line count, including blank and comment-only lines, and let $S(f)$ denote its effective source line count: lines containing source tokens, including preprocessor directives, while excluding blank and comment-only lines. Let $C_{\mathrm{comment}}(f)$ denote comment-only lines and $B_{\mathrm{blank}}(f)$ blank lines. A line containing code and an inline comment belongs to $S(f)$ rather than being counted twice. Thus

```math
P(f)=S(f)+C_{\mathrm{comment}}(f)+B_{\mathrm{blank}}(f).
```

$P(f)$ measures navigation span: comments and whitespace still occupy places a reader moves through. $S(f)$ measures code concentration. The additional counts remain useful observations even where no mature model yet turns them into a judgement. Comments and whitespace are not waste merely because subtraction is easy. Navigation and spatial orientation are comprehension costs rather than cosmetic concerns [ref-navigation] [ref-spatial-navigation].

Modern editors can fold functions, comments, and other regions, so raw physical span is not a complete model of navigation cost. Folding reduces visible span without making the underlying structure disappear; outline or collapsed-span measurements are a legitimate research direction, but no such metric is fixed here.

For a referent `r` and layer $`\ell\in\{\mathrm{src},\mathrm{tests},\mathrm{benchmarks}\}`$, let $`\mathcal{G}_\ell(r)`$ denote the layer’s bunch of files associated with `r`. Define

```math
K_\ell(r)=|\mathcal{G}_\ell(r)|,
```

and

```math
P_\ell(r)=\sum_{f\in\mathcal{G}_\ell(r)}P(f), \qquad
S_\ell(r)=\sum_{f\in\mathcal{G}_\ell(r)}S(f).
```

The same aggregation may be retained for comment-only and blank lines. File count and total volume answer different questions: one referent implemented by eight small files is not the same shape as one implemented by a single file of the same total size.

When $S_\ell(r)>0$, define the effective-source share of a file

```math
w_f=\frac{S(f)}{S_\ell(r)}.
```

The within-bunch entropy and effective file count are

```math
H_\ell(r)=-\sum_{f\in\mathcal{G}_\ell(r)} w_f\ln w_f,
\qquad
K^{\mathrm{eff}}_\ell(r)=e^{H_\ell(r)}.
```

with the usual convention $0\ln 0=0$.

The largest-file share is

```math
\Delta_\ell(r)=\max_{f\in\mathcal{G}_\ell(r)} w_f.
```

$K_\ell(r)$ measures literal fragmentation, $K^{\mathrm{eff}}_\ell(r)$ measures how many materially substantial files the bunch behaves as if it had, and $\Delta_\ell(r)$ makes single-file dominance visible. These are observations and mathematical models, not an instruction to split or merge files. Effective-number interpretations of entropy provide the mathematical basis for this distinction [ref-hill-effective-count].

For a `referent` $r$, let $`\mathcal{P}(r)`$ denote its production files: its header and production companions, excluding tests and benchmarks. Define

```math
P(r)=\sum_{f\in\mathcal{P}(r)}P(f), \qquad S(r)=\sum_{f\in\mathcal{P}(r)}S(f).
```

For a `module` $v$, let $`\mathcal{R}(v)`$ denote its descendant `referent`s and define

```math
S(v)=\sum_{r\in\mathcal{R}(v)}S(r).
```

For the direct children $c_i$ of $v$, define

```math
q_i=\frac{S(c_i)}{S(v)}.
```

The corresponding implementation-volume imbalance is zero when $B(v)\le1$ or $S(v)=0$:

```math
I_S(v)=0.
```

Otherwise,

```math
I_S(v)=1+\sum_{i=1}^{B(v)} q_i \log_{B(v)}q_i,
```

with the usual convention $0\log 0=0$.

`I_R(v)` and `I_S(v)` remain independent. The first measures imbalance in the distribution of named responsibilities; the second measures implementation-volume concentration even when referent counts are balanced. Architectural analyzability work similarly separates decomposition from distribution of implementation volume [ref-analyzability].

File and bunch volume are better understood through ranges than through one universal magic cutoff. Source-file size distributions are strongly skewed, and different roles such as headers, implementation, tests, benchmarks, `core/`, and `app/` need not share the same useful ranges [ref-file-size] [ref-metric-thresholds]. Concrete boundaries, severity classes, corpus selection, and the combination of several measurements are calibration choices for tooling rather than constants of this model. Independent measurements remain visible even if a later heuristic combines them [ref-metric-combination].

Generated and service material is excluded from these source-structure measurements: the model describes authored project code, not the scaffolding produced to inspect or build it.

TODO: file-volume bounds — calibrate useful role-specific ranges and corpus construction for files and bunches without promoting one current threshold or severity scheme into permanent MANIFESTO law.

### 2.4 Functions and Complexity

Function span, control-flow complexity, and nesting describe different reading costs and remain independently visible.

For a function `g`, let $P(g)$ denote the physical line span of its definition.

Cyclomatic complexity follows McCabe. For a control-flow graph `G` with `e` edges, `n` nodes, and `p` connected components,

```math
\mu(G)=e-n+2p.
```

For the connected control-flow graph of one function,

```math
\mu(g)=e_g-n_g+2.
```

Let $N(g)$ denote the maximum nested control-flow depth of `g`. A long linear function, a short branch-heavy function, and a deeply nested function are different reading problems; none should disappear merely because another metric happens to look comfortable. Combinations of distinct metrics are more informative than one measurement alone [ref-mccabe] [ref-cpp-guidelines] [ref-metric-combination].

Useful ranges for $P(g)$, $\mu(g)$, and $N(g)$ are calibration questions rather than fixed universal numbers. The mathematical quantities belong here; exact boundaries and severity mapping may evolve with evidence. Cognitive-complexity style models are a related research direction, but no additional complexity formula is fixed here.

TODO: function nesting — validate whether $N(g)$ adds useful signal beyond cyclomatic complexity rather than merely duplicating it, and calibrate its useful ranges together with the other function measurements.

### 2.5 Naming

Project-owned file and directory names remain `snake_case`. Ordinary project-owned C++ identifiers use `snake_case`; leading or trailing underscores and scope prefixes such as `m_` remain undesirable rather than defining a second naming system. External declarations owned by dependencies are outside this policy.

Names are concise as well as meaningful. MANIFESTO does not reward verbosity and imposes no minimum word count: conventional short names such as `lhs`, `rhs`, `id`, `x`, or `y` are not expanded for style machinery, and no natural-language corpus is needed to decide that a coordinate called `x` is not suffering from insufficient prose.

For a `snake_case` identifier $n$, let $W(n)$ denote the number of underscore-separated segments and $C(n)$ its character count. Together they describe naming verbosity without pretending to measure meaning. Test names naturally encode the subject together with scenario or expectation and may therefore occupy a broader useful range than ordinary identifiers [ref-identifiers] [ref-identifier-guidelines].

Concrete verbosity ranges are calibration rather than vocabulary and need not be frozen into MANIFESTO.

TODO: naming exceptions — decide the sparse exception model for code symbols such as macros and generated include guards; keep file and directory naming independent of those code-level exceptions.

### 2.6 Locality and Namespaces

Ordinary project logic has explicit ownership in the `referent` structure and does not accumulate as hidden translation-unit-local mass. A useful helper may belong to an existing referent and does not require its own file or abstraction, but substantial named behaviour has an explicit declaration and a deliberate place in the source tree.

Anonymous namespaces work against that preference by hiding ownership inside a translation unit. Their declaration count and effective line count are useful measurements because a tiny local helper and a private miniature subsystem are not the same structural event. LLVM documents the same locality-of-reference problem: a reader may need to search far above a declaration to discover that it is hidden inside an anonymous namespace [ref-llvm-style].

A diagnostically clean project stage contains no anonymous namespaces. An anonymous namespace is diagnostic debt rather than a second accepted storage model for project logic.

### 2.7 Control Flow and Lambdas

All control-flow bodies use braces, including single-statement bodies. The goal is stable syntactic shape under later edits rather than compactness for its own sake [ref-cert-braces].

Lambdas are held to tighter expectations than ordinary named functions because their main value is short, local behaviour. For a lambda `l`, reuse its physical span `P(l)` and cyclomatic complexity $`\mu(l)`$, and let $N(l)$ denote maximum nested control-flow depth inside the lambda body.

These measurements remain separate. An unnamed block that is long, branch-heavy, or deeply nested is expensive for different reasons, and useful ranges for lambdas should remain tighter than for ordinary named functions without pretending that one permanent set of integer cutoffs fits every codebase [ref-cpp-guidelines].

### 2.8 Conditional Compilation

Platform, backend, provider, and environment differences are expressed structurally rather than scattered through shared source files as deep conditional-compilation trees. Compact local conditional branches remain acceptable where introducing a separate abstraction would reduce locality and clarity.

For a source file `f`, let $`N_{\mathrm{pp}}(f)`$ denote the maximum nesting depth of preprocessor conditionals. Increasing depth increases the amount of mutually conditional structure a reader must keep in mind. Developer studies of the C preprocessor report broad discomfort with deeper nesting and comprehension problems around conditional-compilation structure [ref-preprocessor]. The measurement constrains the shape worth watching, not the refactoring strategy; an abstraction introduced only to improve a number can still be a worse structural result.

Concrete range boundaries belong to calibration rather than to the definition of $N_{\mathrm{pp}}(f)$.

### 2.9 Documentation

Documentation belongs to a mature interface and follows interface stability. Before the interface stabilizes, requirements, architecture, naming, and tests take precedence over prematurely polished API prose.

For mature project-owned code, named declarations in headers are the primary documentation surface and support Doxygen extraction. Documentation describes contracts, intent, assumptions, and non-obvious behaviour rather than restating syntax. Implementation comments remain secondary and explain only information that cannot be expressed clearly through structure, naming, declarations, or tests.

Generated documentation configuration is tooling material rather than handwritten project policy; `Doxyfile` and related surfaces are materialized in the service workspace when needed.

TODO: documentation maturity — define when missing Doxygen coverage begins to generate diagnostics; decide the lifecycle of temporary project documents and the boundary between API documentation, retained `assets/` artifacts, and generated Pages output.

TODO: text hygiene — reconsider the older ASCII-only source/Markdown and English-only comment conventions before promoting either into current policy.

### 2.10 Diagnostic Cleanliness

Let $`\mathcal{W}(P)`$ denote the active set of style and structural deviations reported for a project snapshot $P$, and let

```math
N(P)=|\mathcal{W}(P)|.
```

A project stage with $N(P)=0$ is `diagnostically clean`. Every active deviation is diagnostic debt. Diagnostic cleanliness is orthogonal to compilability: a project may produce a perfectly real artifact while still carrying structural debt.

A useful report preserves independent causes and the raw measurements behind them instead of collapsing everything into one mystical quality number. The exact vocabulary of severities, calibrated ranges, exit statuses, and CI gates belongs to the concrete tooling policy rather than to this mathematical and structural model.

Policy precedes tooling. Incomplete automatic detection does not weaken a rule, and the existence of a checker does not define the rule.

## 3. `manifest.json`

`manifest.json` is the compact authored description of project intent. It names the project, the real artifacts it is meant to produce, the source each artifact owns, the dependencies between those artifacts, and the artifact selected for the facade.

It does not keep a second inventory of information that can be derived safely from the source tree, and it does not record machine-local or generated state. JSON is a particularly miserable place to rewrite a repository by hand.

A small manifest should remain small. Optional fields are written when they carry intent, not because an empty field looked lonely.

### 3.1 Project Form

The minimal project form is:

```json
{
  "id": "project_name",
  "description": "A short description of the project",
  "facade": "core:lib",
  "artifacts": [
    {
      "id": "core:lib",
      "kind": "static_lib",
      "root": "core",
      "owns": ["math"]
    }
  ]
}
```

The project fields are:

| Field | Meaning |
| --- | --- |
| `id` | Required stable project identifier in `snake_case`. |
| `description` | Required short description of what the project is for. |
| `facade` | Required identity of one real artifact that fulfils the visitor-facing facade contract. |
| `artifacts` | Required non-empty array of artifact definitions. |
| `version` | Optional project/package version. |
| `cpp_standard` | Optional C++ standard when the shared default is not sufficient. |
| `install_artifacts` | Optional additional artifact identities distributed alongside the facade. |
| `install_assets` | Optional declaration that the project’s runtime asset tree is installed. |
| `android_application_id` | Optional stable Android application identity. |
| `android_package_source_dir` | Optional project-relative Android package/resource directory. |

The `facade` does not create another canonical artifact called `mvp`; it selects an existing artifact to fulfil the facade described in Section 1.

Additional distribution does not imply dependency. For example:

```json
{
  "id": "toolset",
  "description": "Two complementary project tools",
  "facade": "viewer:viewer",
  "install_artifacts": ["worker:worker"],
  "artifacts": [
    {
      "id": "viewer:viewer",
      "kind": "exe",
      "root": "core",
      "owns": [],
      "entry": "src/viewer_main.cpp"
    },
    {
      "id": "worker:worker",
      "kind": "exe",
      "root": "core",
      "owns": [],
      "entry": "src/worker_main.cpp"
    }
  ]
}
```

Two executables do not become linked merely because they are shipped together.

### 3.2 Artifact Form

An artifact has a stable identity of the form `namespace:artifact`, for example `core:lib`, `app:app`, or `tools:converter`. The namespace groups related artifacts; it does not create a dependency between them.

The ordinary artifact form is:

```json
{
  "id": "core:lib",
  "kind": "static_lib",
  "root": "core",
  "owns": ["parser"]
}
```

Artifact fields are:

| Field | Meaning |
| --- | --- |
| `id` | Required globally unique `namespace:artifact` identity. |
| `kind` | Required artifact kind: `static_lib`, `shared_lib`, `interface_lib`, `exe`, or `qt_app`. |
| `owns` | Required array of logical source scopes; it may be empty where the artifact owns only an entry point or represents an imported library. |
| `root` | Project-relative source root in which ownership is interpreted; ordinary project artifacts normally use `core` or `app`. |
| `entry` | Explicit entry-point source for `exe` and `qt_app`. |
| `dependencies` | Artifact identities linked by this artifact. |
| `packages` | Artifact-local external package and integration requirements. |
| `tests` | Artifact-level test support. |
| `benchmarks` | Artifact-level benchmark support. |
| `name` | Optional output filename stem when it should differ from the artifact identity. |
| `description` | Optional artifact-specific description. |
| `qml` | Optional QML module description for a `qt_app`. |

Artifact identities, generated build identities, and output names must remain unambiguous.

### 3.3 Ownership and Discovery

`owns` describes logical scopes, not a handwritten file list.

With

```json
{
  "id": "core:lib",
  "kind": "static_lib",
  "root": "core",
  "owns": ["parser", "network/http"]
}
```

the artifact owns those responsibilities beneath `core/` and their conventional companions under `include/`, `src/`, `tests/`, and `benchmarks/`. Discovery follows the referent and bunch structure defined in Section 2; missing companion forms are not invented or required.

A scope may identify one referent, a subtree, or, when deliberately broad, one of the conventional source layers. Broad scopes are used only when broad ownership is actually intended.

Ownership does not overlap. Two artifacts do not quietly claim the same code and wait for the linker to negotiate custody.

Shared implementation belongs to a shared artifact and enters consumers through an explicit dependency. Physical proximity is not dependency.

Adding another ordinary implementation, test, or benchmark companion should therefore not require appending another filename to `manifest.json`. The manifest retains ownership intent; the project supplies the file inventory.

An `entry` is explicitly owned even when `owns` is empty:

```json
{
  "id": "tools:inspect",
  "kind": "exe",
  "root": "core",
  "owns": [],
  "entry": "src/inspect.cpp"
}
```

The entry file need not be called `main.cpp`.

### 3.4 Libraries

A reusable library is expressed directly:

```json
{
  "id": "core:lib",
  "kind": "static_lib",
  "root": "core",
  "owns": ["math", "parser"]
}
```

A shared library changes its kind:

```json
{
  "id": "core:lib",
  "kind": "shared_lib",
  "root": "core",
  "owns": ["math", "parser"]
}
```

A library whose implementation is intentionally header-only uses `interface_lib`:

```json
{
  "id": "core:api",
  "kind": "interface_lib",
  "root": "core",
  "owns": ["api"]
}
```

A library artifact has no executable `entry`.

### 3.5 Executables and Applications

A runnable artifact has an explicit `entry`:

```json
{
  "id": "app:app",
  "kind": "exe",
  "root": "app",
  "owns": ["ui"],
  "entry": "src/main.cpp"
}
```

A project with a reusable core and an application declares the relationship rather than relying on directory proximity:

```json
{
  "id": "numbers_app",
  "description": "Application using the numeric library",
  "facade": "app:app",
  "artifacts": [
    {
      "id": "core:lib",
      "kind": "static_lib",
      "root": "core",
      "owns": ["math"]
    },
    {
      "id": "app:app",
      "kind": "exe",
      "root": "app",
      "owns": ["ui"],
      "entry": "src/main.cpp",
      "dependencies": ["core:lib"]
    }
  ]
}
```

The dependency points to a real library artifact. Sharing a source root or namespace is not enough.

### 3.6 Packages

Package requirements belong to the smallest artifact that needs them.

A Qt Widgets application may declare:

```json
{
  "id": "app:desktop",
  "kind": "qt_app",
  "root": "app",
  "owns": ["window"],
  "entry": "src/main.cpp",
  "packages": {
    "qt": ["Core", "Gui", "Widgets"]
  }
}
```

A package used only by `app:desktop` remains a requirement of `app:desktop`; it does not become project-global state merely because promoting it upward was convenient.

The concrete vocabulary inside `packages` is defined by supported integrations. Unknown package configuration is rejected rather than silently converted into wishful thinking.

### 3.7 Qt and QML

Qt Widgets is the ordinary Qt application form. QML is optional and does not introduce another artifact kind.

A QML-enabled application remains a `qt_app` and adds a `qml` block:

```json
{
  "id": "app:desktop",
  "kind": "qt_app",
  "root": "app",
  "owns": [],
  "entry": "src/main.cpp",
  "packages": {
    "qt": ["Core", "Gui", "Qml", "Quick"]
  },
  "qml": {
    "uri": "Example.Ui",
    "version": "1.0",
    "files": [
      "qml/Main.qml",
      "qml/screens/Home.qml"
    ]
  }
}
```

Authored QML belongs under `app/qml/`. The `qml` block states module identity and QML ownership; its files are not repeated in `owns`. A Widgets-only application has no `qml` block and no QML requirements merely because support exists.

The explicit QML file list is a deliberate exception to ordinary C++ scope discovery while module membership cannot yet be derived with equal confidence.

TODO: QML ownership — reconsider whether explicit `qml.files` remains the right long-term ownership form once QML topology and module discovery are mature enough to derive safely.

### 3.8 Tests and Benchmarks

Ordinary tests and benchmarks belong to the artifact whose behaviour they exercise.

For example:

```json
{
  "id": "core:lib",
  "kind": "static_lib",
  "root": "core",
  "owns": ["parser"],
  "tests": {
    "gtest": true
  },
  "benchmarks": {
    "google_benchmark": true
  }
}
```

The manifest states the support and intent. The actual files remain in `tests/` and `benchmarks/` and are discovered through ownership; they are not copied into JSON as another inventory.

A separately runnable test or benchmark may be its own artifact when it is genuinely independent rather than an ordinary companion of a referent.

### 3.9 External MANIFESTO Projects

Another MANIFESTO-managed repository remains another project. It is built and installed independently and enters the consumer through its exported library surface rather than by joining the consumer’s source tree or CMake graph.

An imported provider library may be described as:

```json
{
  "id": "packing:core",
  "kind": "static_lib",
  "owns": [],
  "packages": {
    "external_project": {
      "repository": "https://github.com/example/packing.git",
      "revision": "compatible-revision",
      "package": "packing",
      "artifact": "library:core"
    }
  }
}
```

A local artifact then consumes that imported identity normally:

```json
{
  "id": "app:app",
  "kind": "exe",
  "root": "app",
  "owns": ["ui"],
  "entry": "src/main.cpp",
  "dependencies": ["packing:core"]
}
```

The provider owns its own files. The consumer does not reproduce the provider’s scopes.

`revision` expresses source-selection intent. Exact resolved state is a different concern and must not be disguised as another handwritten dependency graph.

TODO: dependency lock — define the portable lock surface and its scope. Local resolution state is useful implementation state, but it is not yet the final cross-machine lock contract.

### 3.10 Platform and Distribution Intent

Project-specific platform metadata is written only when the generated result genuinely needs it. For example, an Android-capable application may add:

```json
{
  "android_application_id": "org.example.application",
  "android_package_source_dir": "app/android"
}
```

Likewise, `install_assets` and `install_artifacts` describe distribution intent. They do not redefine the asset topology or invent dependencies between independently distributed artifacts.

The manifest records the exception or selection that cannot be inferred; it does not copy fixed MANIFESTO policy back into every repository.

TODO: policy exceptions — define a sparse manifest form for genuine project-specific code-style exceptions only when concrete cases justify one; exceptions should describe deviations rather than restate defaults.

### 3.11 Validation and Boundaries

Paths authored in `manifest.json` are project-relative and remain inside the selected project after normalization and filesystem resolution. `..`, unsafe aliases, and symlink tricks do not turn one project into the accidental owner of its neighbour.

The manifest is validated as one project state, not as a collection of individually plausible fields. Project identity, artifact identities, ownership, entries, dependency references, package requirements, paths, and the resulting dependency graph must agree before the manifest is used as the basis of state-changing work.

A valid `entry` inside an impossible dependency graph does not make the manifest valid.

Validation therefore precedes materialization. An impossible request should fail while it is still only a request rather than after half of the intended world has already been created.

### 3.12 What Does Not Belong Here

`manifest.json` does not contain generated file inventories, build directories, procedural CMake, machine-local SDK paths, caches, diagnostics, temporary counters, generated reports, or copies of fixed MANIFESTO defaults.

Generated surfaces remain derived from authored intent. They may be inspected and used, but they do not become independent authoring surfaces merely because text files are editable.

The manifest describes **intent**. Everything that can be derived safely should be derived.

## 4. Marx and Engels

MANIFESTO is kept in practice by `marx` and `engels`. They complement one another as two halves of the same system.

Friedrich Engels **studies** the project. Karl Marx **changes** it.

Engels **observes**, **measures**, **models**, **compares**, and **explains**. Marx **validates**, **materializes**, **transforms**, **builds**, and **runs**. Engels may conclude that the project ought to change; Marx may carry the chosen change through.

Neither half is complete on its own. Analysis without action leaves the project as it was. Action without analysis too easily becomes activity for its own sake.

### 4.1 Engels

`engels` deals with the project **as it is**.

He reads its structure and code, builds representations of them, collects measurements, and looks for relations between those observations. An individual fact may be simple; a conclusion drawn from several facts need not be.

Engels distinguishes between what the project **claims about itself**, what its structure **shows**, and what the code itself **does**. When these views agree, the evidence becomes stronger. When they disagree, the disagreement is itself worth reporting.

Friedrich Engels need not reduce every file or branch to a single verdict. He may examine the tree as a whole: its `referent`s and `bunch`es, the distribution of implementation, tests and benchmarks, heavy branches, poorly attached files, concentrated code, and other properties that become meaningful only in relation to the surrounding structure.

An `engels check` may therefore report a concrete deviation without touching it, while an `engels report` may go further and place the same observation in a wider structural context. Read-only means that authored and tracked project state remains untouched; indexes, caches, reports, and other ignored analytical material may still be created when analysis requires them.

Engels does not merely **find faults**. He may **model alternatives**.

Where the evidence supports more than one reasonable interpretation, he may present more than one. A large implementation may remain one `bunch`, be split internally, or justify a new `referent`. A misplaced test may have one obvious destination or several plausible ones. A tree may admit more than one defensible rebalance.

The purpose is not to manufacture certainty where none exists. Engels may rank alternatives, explain their consequences, and state the evidence behind them. Ambiguity remains ambiguity until enough evidence exists to remove it.

When a change appears justified, Engels may **propose** it. He may suggest another home for a file, a split of an oversized implementation, a redistribution within a `bunch`, or a broader **rebalance** of the tree.

A proposal does not hide its grounds. If Friedrich Engels proposes to restructure the project, the developer should be able to see both **why** he arrived at that proposal and **what** the proposed change would alter.

But Engels does not apply his own proposals.

His task is to **understand the conditions**.

### 4.2 Marx

`marx` deals with **changing** the project.

Karl Marx materializes what is meant to exist, synchronizes derived surfaces, formats code, builds and runs artifacts, and applies chosen structural transformations. A `marx sync` may bring derived surfaces back into agreement with authored intent; a `marx build` may materialize the development world required to produce an artifact; a chosen structural plan belongs on Marx’s side of the boundary.

Marx does not act blindly. Every transformation begins from a known state and aims at a known result. Before applying a structural plan, he **validates** that the project still matches the conditions under which that plan was formed.

A proposal that was sound yesterday may be stale today. Files may have moved, declarations may have changed, dependencies may have shifted, and a once-valid rebalance may no longer describe the project in front of him. Marx does not force an obsolete plan onto a different state merely because the plan once existed.

If the preconditions no longer hold, the transformation is not silently approximated into something else. It must be reconsidered.

Where several reasonable outcomes remain, Karl Marx does not choose architecture on the developer’s behalf. Engels may expose the alternatives; the developer chooses among them; Marx **turns the chosen direction into a new project state**.

If the transformation affects several related files or branches of the tree, those changes belong to one coherent operation rather than to a loose sequence of unrelated edits. Marx should know the intended resulting state before the first meaningful part of that state becomes authoritative, and preserve the previous valid state where practical if the transformation cannot be completed.

A `marx mutate` may therefore apply a transformation that Engels proposed and the developer accepted. If a rebalance is chosen, Marx performs the rebalance. If an implementation split is chosen, Marx performs the split. If a move is approved, Marx moves the material and restores the surrounding structure to consistency.

Karl Marx does not stop at interpreting the project.

### 4.3 Together

Their ordinary cycle begins with Engels and ends with Engels.

Friedrich Engels **examines** the existing state, exposes its structure, and, where useful, **proposes** one or more changes. The developer accepts, rejects, or adjusts the proposal. Karl Marx **validates** the selected transformation against the current project and **applies** it. Engels then **examines the result**.

The result is not assumed correct merely because Marx completed the operation. It becomes another project state and is subject to the same observation as the one before it.

This separation matters most where no single mechanical answer exists. Decomposition, rebalance, referent boundaries, test placement, and similar structural decisions may admit several defensible outcomes. Engels may gather enough evidence to make one alternative clearly preferable, but preference is not ownership of the final choice.

Engels therefore preserves the distinction between **evidence**, **interpretation**, and **proposal**.

Marx preserves the distinction between **intent**, **validation**, and **change**.

If a request plainly belongs to the other half, the actor should point to its counterpart rather than pretend the request is meaningless. The boundary should be visible to the developer without requiring a study of internal service machinery.

Together they allow the project to evolve without confusing understanding with mutation.

Friedrich Engels **reveals the contradictions** of the project.

Karl Marx **turns a chosen resolution into its new material state**.

TODO: public grammar — command names and selectors may be refined as the interface matures, but changes to grammar must preserve this actor boundary rather than quietly move responsibility from one half to the other.
