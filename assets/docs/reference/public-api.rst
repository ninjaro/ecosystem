C++ integration entry points
============================

The command-line actors and authored JSON schema are the primary public
interfaces. These current C++ entry points share artifact identity and manifest
loading behavior between the actors; they do not promise a stable binary ABI.

Artifact identity
-----------------

``ecosystem::artifact_ref`` contains two strings: ``component_id`` (the namespace
before the colon) and ``artifact_id`` (the identifier after it).

``parse_artifact_ref(const std::string&)`` returns ``std::optional<artifact_ref>``.
It splits exactly one colon with nonempty text on both sides; missing, repeated,
leading or trailing colons produce ``std::nullopt``. This syntactic split does not
validate project identifier policy. ``format_artifact_ref(const artifact_ref&)``
returns ``component_id + ":" + artifact_id``.

Manifest loading
----------------

``load_manifest(const std::filesystem::path&)`` reads authored JSON, validates
its model and paths, and discovers manifest-owned source files. Its
``ecosystem::manifest_report`` contains:

* ``path``: the supplied ``std::filesystem::path``.
* ``has_manifest``: whether a manifest path was observed.
* ``value``: ``std::optional<manifest>`` containing the parsed model when available.
* ``errors``: ``std::vector<std::string>`` containing loading, validation and
  owned-file discovery diagnostics.

Inspect ``errors`` before using ``value``: a parsed model can still be invalid.
Missing or unreadable input does not produce a usable successful model. Internal
model layout is not an authored JSON schema or a public binary ABI.

See :doc:`manifest` for the supported authored fields and ownership rules and
:doc:`cli` for actor commands and selectors.
