"""Compose project results through Sphinx HTML events."""
import os
from pathlib import Path
import shutil

from sphinx_presentation.inputs import Inputs, link, require, safe_path
from sphinx_presentation.reports import verification
from sphinx_presentation.benchmarks import benchmarks
from sphinx_presentation.assets import asset_pages
from sphinx_presentation.diagnostics import diagnostics

PAGE = "_ecosystem/index"


def compose(root, evidence=None, diagnostic_snapshot=None):
    inputs = Inputs(root)
    body = '<h1>Project results</h1><h2>C++ API</h2><p>' + link('doxygen/index.html', 'Standalone Doxygen reference') + '</p>'
    doxygen = safe_path(root, '.ecosystem/doxygen/html')
    index = safe_path(root, '.ecosystem/doxygen/html/index.html')
    require(index.is_file() and index.stat().st_size > 0, 'Doxygen HTML is missing or empty')
    doxygen_bytes = 0
    for path in sorted(doxygen.rglob('*')):
        require(not path.is_symlink(), 'symlink in Doxygen output')
        if not path.is_dir():
            require(path.is_file(), 'invalid Doxygen output type')
            doxygen_bytes += path.stat().st_size
            require(doxygen_bytes <= 1024 * 1024 * 1024, 'Doxygen output exceeds 1 GiB')
            # Native output can be large; copy files without buffering the whole
            # API site together with downloaded report/media data in memory.
            inputs.files['doxygen/' + path.relative_to(doxygen).as_posix()] = path
    body += '<h2>Verification</h2>' + verification(inputs, evidence)
    body += '<h2>Diagnostics</h2>' + diagnostics(inputs, diagnostic_snapshot)
    body += '<h2>Benchmarks</h2>' + benchmarks(inputs)
    assets, pages = asset_pages(inputs)
    body += '<h2>Showcase</h2>' + assets
    return body, inputs.files, pages


def collect_pages(app):
    require(not any(n == '_ecosystem' or n.startswith('_ecosystem/') for n in app.env.found_docs), '_ecosystem/ in the selected documentation root is reserved for generated presentation')
    root = Path(app.confdir).parent.parent
    body, files, pages = compose(root, os.environ.get('ECOSYSTEM_SPHINX_EVIDENCE'),
                                os.environ.get('ECOSYSTEM_SPHINX_DIAGNOSTICS'))
    destination = Path(app.outdir) / '_ecosystem'
    require(not destination.exists(), 'generated presentation output already exists')
    destination.mkdir()
    for name, data in files.items():
        target = safe_path(destination, name)
        target.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(data, Path):
            require(not data.is_symlink() and data.is_file(), 'Doxygen output changed during composition')
            shutil.copyfile(data, target)
        else:
            target.write_bytes(data)
    return [(PAGE, {'title': 'Project results', 'body': body}, 'page.html'), *pages]


def page_context(app, pagename, templatename, context, doctree):
    if 'body' in context:
        uri = app.builder.get_relative_uri(pagename, PAGE)
        context['body'] = '<nav aria-label="Project results">' + link(uri, 'Project results') + '</nav>' + context['body']


def setup(app):
    app.connect('html-collect-pages', collect_pages)
    app.connect('html-page-context', page_context)
    return {'version': '1', 'parallel_read_safe': True, 'parallel_write_safe': False}
