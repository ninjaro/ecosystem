"""Compose generated references, coverage and media through Sphinx HTML events."""
import os
import posixpath
import re
from pathlib import Path
import shutil

from sphinx_presentation.inputs import Inputs, link, require, safe_path
from sphinx_presentation.reports import verified_coverage
from sphinx_presentation.coverage import coverage_files
from sphinx_presentation.benchmarks import benchmarks
from sphinx_presentation.assets import asset_pages
from sphinx_presentation.diagnostics import diagnostics

PAGE = "_ecosystem/index"


def compose(root, evidence=None, diagnostic_snapshot=None):
    inputs = Inputs(root)
    body = '<h1>Media and benchmarks</h1>'
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
    coverage_files(inputs, verified_coverage(inputs, evidence))
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
            if name.startswith('doxygen/') and name.endswith('.html'):
                text = target.read_text(encoding='utf-8')
                links = [('Documentation', '../index.html'), ('C++ API (Doxygen)', 'doxygen/index.html'),
                         ('Code Coverage', 'coverage/index.html')]
                nav = '<nav aria-label="Project documentation">' + ' · '.join(
                    link(posixpath.relpath(url, posixpath.dirname(name)), label) for label, url in links) + '</nav>'
                text = re.sub(r'(<body\b[^>]*>)', lambda match: match[0] + nav, text, count=1, flags=re.IGNORECASE)
                target.write_text(text, encoding='utf-8')
        else:
            target.write_bytes(data)
    return [(PAGE, {'title': 'Media and benchmarks', 'body': body}, 'page.html'), *pages]


def page_context(app, pagename, templatename, context, doctree):
    links = [('Documentation', 'index'), ('C++ API (Doxygen)', '_ecosystem/doxygen/index'),
             ('Code Coverage', '_ecosystem/coverage/index')]
    context['ecosystem_navigation'] = '<ul>' + ''.join(
        '<li>' + link(app.builder.get_relative_uri(pagename, destination), label) + '</li>'
        for label, destination in links) + '</ul>'


def setup(app):
    app.connect('html-collect-pages', collect_pages)
    app.connect('html-page-context', page_context)
    return {'version': '1', 'parallel_read_safe': True, 'parallel_write_safe': False}
