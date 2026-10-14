# Manifesto and Ecosystem interfaces

**Manifesto** is the [principles and architecture document](../manifesto.md).
**Ecosystem** is the software implementing it: Marx, Engels, generators and
integrations. `manifest.json` describes source/artifact ownership; it is not the
Manifesto. Diagnostic `authority=manifesto` identifies a policy authority.

| Current software interface | Meaning |
| --- | --- |
| Project/package ID `ecosystem`, engine archive `ecosystem_core`, frontend archive `ecosystem_frontend_support` | Ecosystem product identity; `core:core`, other artifact IDs and the `marx`/`engels` executable names retain their roles |
| `ECOSYSTEM_*` environment/build definitions | Current tooling configuration, including template selection, CI progress/reuse, Sphinx and release settings |
| `ecosystem.github.vars.json`, `ecosystem_*` keys | Reviewed GitHub tooling repository/ref/bootstrap/action configuration |
| `ecosystem.local.json`, `ecosystem.workspace.json` | Local provider source selection and workspace groups |
| `setup-ecosystem`, `run-ecosystem-stage`, `select-ecosystem-ci`, `publish-ecosystem-report` | Canonical generated CI actions |
| `share/ecosystem/templates` | Installed matching template bundle, resolved relative to the actors |
| `_ecosystem/` and `ecosystem_*`/`ecosystem-*` presentation identifiers | Generated project results, modules, CSS and CI artifact/cache namespaces |

Only the current internal APIs, schemas and formats are supported. Older internal
versions and historical outputs are not compatibility contracts; exceptions
require explicit developer approval. Required third-party dependency versions
and current interoperability remain separate contracts.

Actors and their installed template bundle must come from the same implementation.
CI must select a matching tooling revision and the current action/input/output
contract. Current software names have no old-name aliases. Fresh build trees avoid
mixing generated state from different implementations.
