C++ integration entry points
============================

The local command-line actors and authored JSON schema are the primary public
interfaces. The following C++ entry points describe the shared identity and
loading contracts used by both actors. The larger internal model is not a public binary ABI; this page documents
current entry points rather than promising compatibility with historical releases.

Artifact identity
-----------------

.. doxygenstruct:: ecosystem::artifact_ref
   :members:

.. doxygenfunction:: ecosystem::parse_artifact_ref

.. doxygenfunction:: ecosystem::format_artifact_ref

Manifest loading
----------------

.. doxygenstruct:: ecosystem::manifest_report
   :members:

.. doxygenfunction:: ecosystem::load_manifest(const std::filesystem::path&)

See :doc:`manifest` for the authored schema and :doc:`migration` for the previous
schema cutover. The declarations above are read by Doxygen and rendered by Breathe;
there is no separate C++ documentation parser.
