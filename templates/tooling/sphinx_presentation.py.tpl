"""Compose project results using Sphinx's HTML events; never run report content."""
import csv
import hashlib
import html
import io
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
from urllib.parse import quote

LIMIT = 256 * 1024 * 1024
PAGE = '_manifesto/index'


def require(condition, message):
    if not condition:
        raise ValueError('Project presentation: ' + message)


def safe_path(root, name):
    require(isinstance(name, str) and name and '\\' not in name
            and all(p not in ('', '.', '..') for p in name.split('/')),
            'unsafe input path: ' + str(name))
    current = Path(root)
    for part in name.split('/'):
        current /= part
        require(not current.is_symlink(), 'symlink input: ' + name)
    return current


class Inputs:
    def __init__(self, root):
        self.root = Path(root)
        self.total = 0
        self.files = {}

    def read(self, name):
        path = safe_path(self.root, name)
        require(path.is_file() and stat.S_ISREG(path.stat().st_mode), 'missing file: ' + name)
        require(path.stat().st_size <= LIMIT - self.total, 'inputs exceed 256 MiB')
        data = path.read_bytes()
        self.total += len(data)
        require(self.total <= LIMIT, 'inputs exceed 256 MiB')
        return data

    def attach(self, name, destination):
        data = self.read(name)
        require(destination not in self.files, 'duplicate output: ' + destination)
        self.files[destination] = data
        return data


def pre(value):
    return '<pre>' + html.escape(str(value)) + '</pre>'


def link(destination, label):
    return '<a href="' + quote(destination, safe='/') + '">' + html.escape(label) + '</a>'


def verification(inputs, evidence):
    if not evidence:
        return '<p>No verification evidence was selected for this documentation build.</p>'
    # Selection is explicit. Local files are integrity checked, not a substitute
    # for the CI caller's workflow/job/artifact authorization checks.
    directory = Path(evidence).absolute().relative_to(inputs.root.absolute())
    prefix = directory.as_posix() + '/'
    receipt = json.loads(inputs.read(prefix + 'receipt.json'))
    require(receipt.get('schema') == 1 and receipt.get('kind') in ('checks', 'pages', 'publication'), 'unknown receipt')
    project = json.loads(inputs.read('manifest.json'))['id']
    require(receipt.get('policy', {}).get('project') == project, 'receipt project differs')
    def git(*args):
        return subprocess.check_output(['git', '-C', str(inputs.root), *args], text=True).strip()
    require(receipt.get('tested_tree') == git('rev-parse', 'HEAD^{tree}'), 'receipt tree differs')
    git('diff', '--quiet', '--ignore-submodules=none', 'HEAD')
    require(not git('ls-files', '--others', '--exclude-standard'), 'untracked authored inputs differ')
    require(re.fullmatch('[0-9a-f]{40}', receipt.get('tested_commit', '')), 'invalid tested revision')
    coverage = receipt.get('coverage')
    require(coverage in ('passed', 'skipped'), 'coverage is incomplete')
    stages = ['01-tracked-surface', '02-coverage-check'] if receipt['kind'] == 'pages' else ['01-required-checks', '02-coverage-check']
    stage_files = {stage: ('summary.md', 'output.log') for stage in stages}
    if receipt['kind'] == 'publication':
        source = receipt.get('verification', {})
        require(source.get('schema') == 1 and source.get('commit') == receipt['tested_commit']
                and source.get('tree') == receipt['tested_tree'] and source.get('pushes')
                and source.get('checks') and source.get('codeql'), 'missing publication provenance')
        stage_files['01-tracked-surface'] = ('summary.md', 'output.log')
        stage_files['02-codeql-build'] = ('summary.md', 'output.log')
        stage_files['03-codeql-analysis'] = ('summary.md',)
    expected = {f'github/reports/{stage}/{name}' for stage, names in stage_files.items() for name in names}
    if coverage == 'passed':
        expected.add('reports/coverage.json')
    require(isinstance(receipt.get('files'), dict) and set(receipt['files']) == expected, 'incomplete report set')
    reports = {}
    for name, record in sorted(receipt['files'].items()):
        data = inputs.read(prefix + name)
        require(len(data) == record.get('bytes') and hashlib.sha256(data).hexdigest() == record.get('sha256'), 'report digest differs: ' + name)
        require(data or name.endswith('/output.log'), 'empty report: ' + name)
        reports[name] = data
    source = {key: receipt.get(key) for key in ('kind', 'repository', 'run_id', 'run_attempt', 'tested_commit', 'tested_tree')}
    if receipt['kind'] == 'publication':
        source['verification'] = receipt['verification']
    body = '<h3>Source</h3>' + pre(json.dumps(source, indent=2))
    body += '<h3>Coverage</h3>'
    if coverage == 'skipped':
        body += '<p>Unsupported by this project; no coverage percentage is claimed.</p>'
    else:
        report = json.loads(reports['reports/coverage.json'])
        require(report.get('type') == 'llvm.coverage.json.export' and report.get('data'), 'invalid LLVM coverage export')
        totals = {}
        for entry in report['data']:
            require(entry.get('files') and isinstance(entry.get('totals'), dict), 'incomplete LLVM coverage export')
            for metric in ('lines', 'functions', 'regions', 'branches'):
                value = entry['totals'].get(metric)
                if value is None:
                    continue
                count, covered = value.get('count'), value.get('covered')
                require(type(count) is int and type(covered) is int and 0 <= covered <= count, 'invalid coverage count')
                old = totals.get(metric, (0, 0))
                totals[metric] = (old[0] + count, old[1] + covered)
        require(totals, 'coverage has no totals')
        body += '<table><tr><th>Metric</th><th>Covered / total</th><th>Percent</th></tr>'
        for metric, (count, covered) in totals.items():
            percent = f'{100 * covered / count:.1f}%' if count else 'Not applicable'
            body += f'<tr><td>{metric}</td><td>{covered} / {count}</td><td>{percent}</td></tr>'
        body += '</table>'
    for stage, names in stage_files.items():
        body += '<h3>' + html.escape(stage) + '</h3>'
        for name in names:
            # Markdown/logs are displayed as data, never interpreted as HTML.
            body += '<details><summary>' + name + '</summary>' + pre(reports[f'github/reports/{stage}/{name}'].decode('utf-8', errors='replace')) + '</details>'
    return body


def benchmarks(inputs):
    root = safe_path(inputs.root, '.ecosystem/reports/benchmark')
    body = '<p>Local benchmark snapshots are separate from the selected verification evidence.</p>'
    count = 0
    owned = {a['id'] for a in json.loads(inputs.read('manifest.json')).get('artifacts', [])}
    for path in sorted(root.glob('*/*/result.json')):
        relative = path.relative_to(inputs.root).as_posix()
        identity = path.parent.parent.name + ':' + path.parent.name
        if identity not in owned:
            continue
        result = json.loads(inputs.read(relative))
        if result.get('status') != 'passed' or result.get('exit_code') != 0:
            continue
        require(result.get('artifact') == identity and result.get('profile') == 'release' and result.get('log') == 'bench_log.txt', 'invalid benchmark identity: ' + relative)
        log = inputs.read(path.with_name('bench_log.txt').relative_to(inputs.root).as_posix())
        body += '<h3>' + html.escape(identity) + '</h3>' + pre(log.decode('utf-8', errors='replace'))
        count += 1
    return body if count else '<p>No successful local benchmark output is available.</p>'


def showcase(inputs):
    index = safe_path(inputs.root, 'assets/showcase/index.tsv')
    if not index.exists():
        return '<p>No indexed showcase media is available.</p>'
    rows = csv.DictReader(io.StringIO(inputs.read('assets/showcase/index.tsv').decode('utf-8')), delimiter='\t')
    require(rows.fieldnames == ['id', 'path', 'type', 'description', 'datetime'], 'invalid showcase columns')
    extensions = {'image': {'.png', '.jpg', '.jpeg', '.gif', '.webp', '.avif'}, 'video': {'.mp4', '.webm'}, 'pdf': {'.pdf'}}
    seen = set()
    body = '<ol>'
    for row in rows:
        require(None not in row and all(isinstance(v, str) for v in row.values()), 'invalid showcase row')
        require(row['id'] and row['id'] not in seen, 'duplicate or missing showcase ID')
        seen.add(row['id'])
        require(Path(row['path']).suffix.lower() in extensions.get(row['type'], set()), 'unsupported showcase media: ' + row['path'])
        destination = 'showcase/' + row['path']
        inputs.attach('assets/' + destination, destination)
        label = row['description'] or row['id']
        body += '<li>' + link(destination, label)
        if row['type'] == 'image':
            body += '<br><img loading="lazy" style="max-width:100%" src="' + quote(destination, safe='/') + '" alt="' + html.escape(label, quote=True) + '">'
        body += '<p>' + html.escape(row['datetime']) + '</p></li>'
    return body + '</ol>'


def compose(root, evidence=None):
    inputs = Inputs(root)
    body = '<h1>Project results</h1><h2>C++ API</h2><p>' + link('doxygen/index.html', 'Standalone Doxygen reference') + '</p>'
    doxygen = safe_path(root, '.ecosystem/doxygen/html')
    require(safe_path(root, '.ecosystem/doxygen/html/index.html').is_file(), 'Doxygen HTML is missing')
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
    body += '<h2>Benchmarks</h2>' + benchmarks(inputs)
    body += '<h2>Showcase</h2>' + showcase(inputs)
    return body, inputs.files


def collect_pages(app):
    require(not any(n == '_manifesto' or n.startswith('_manifesto/') for n in app.env.found_docs), 'docs/_manifesto is reserved for generated presentation')
    root = Path(app.confdir).parent.parent
    body, files = compose(root, os.environ.get('MANIFESTO_SPHINX_EVIDENCE'))
    destination = Path(app.outdir) / '_manifesto'
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
    return [(PAGE, {'title': 'Project results', 'body': body}, 'page.html')]


def page_context(app, pagename, templatename, context, doctree):
    if 'body' in context:
        uri = app.builder.get_relative_uri(pagename, PAGE)
        context['body'] = '<nav aria-label="Project results">' + link(uri, 'Project results') + '</nav>' + context['body']


def setup(app):
    app.connect('html-collect-pages', collect_pages)
    app.connect('html-page-context', page_context)
    return {'version': '1', 'parallel_read_safe': True, 'parallel_write_safe': False}
