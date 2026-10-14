Ecosystem
=========

Ecosystem provides two local C++ workflow actors: Marx changes project state;
Engels checks and reports it. Start with the authored manifest and CLI contracts.

.. toctree::
   :maxdepth: 2

   reference/manifest
   reference/cli
   manifesto
   reference/documentation
   reference/public-api
   reference/terminology

The public API page is rendered from the same manifest-owned C++ declarations
as the standalone Doxygen output. It deliberately selects the artifact identity
and manifest-loading entry points; internal declarations are outside this selected public reference.
