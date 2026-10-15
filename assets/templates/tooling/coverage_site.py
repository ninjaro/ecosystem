"""Generate measured LLVM coverage and a portable public site during Checks only."""
import html
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
from urllib.parse import quote
import zipfile

METRICS = ('lines', 'functions', 'branches', 'regions')


def require(value, message):
    if not value:
        raise ValueError(message)


def metric(value):
    require(isinstance(value, dict), 'missing LLVM coverage metric')
    count, covered = value.get('count'), value.get('covered')
    require(type(count) is int and type(covered) is int and 0 <= covered <= count,
            'invalid LLVM coverage counts')
    return {'count': count, 'covered': covered,
            'percent': 100 * covered / count if count else None}


def percentage(value):
    return f"{value['percent']:.1f}%" if value['percent'] is not None else 'Not applicable'


def page(body):
    return ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            '<title>Code Coverage</title><link rel="stylesheet" href="coverage.css">'
            '<script defer src="coverage.js"></script></head><body>'
            '<nav aria-label="Project documentation"><a href="../../index.html">Documentation</a> · '
            '<a href="../doxygen/index.html">C++ API (Doxygen)</a> · '
            '<a href="index.html">Code Coverage</a></nav><main>' + body + '</main></body></html>')


def render(summary):
    if summary['status'] != 'available':
        return page('<h1>Coverage unavailable</h1><p>' + html.escape(summary['reason']) + '</p>')
    body = '<h1>Code Coverage</h1><p>Measured project-owned production code, including instrumented headers and templates.</p>'
    body += '<table><caption>Overall coverage</caption><thead><tr><th>Metric</th><th>Covered / total</th><th>Coverage</th></tr></thead><tbody>'
    for name in METRICS:
        value = summary['totals'][name]
        body += f"<tr><th>{name.title()}</th><td>{value['covered']} / {value['count']}</td><td>{percentage(value)}</td></tr>"
    body += '</tbody></table><p>Select a source file for LLVM line, branch, region and instantiation details. Select a column heading to sort.</p>'
    body += '<div class="table-scroll"><table data-sortable><caption>Per-file coverage</caption><thead><tr><th><button>File</button></th>'
    body += ''.join(f'<th><button>{name.title()}</button></th>' for name in METRICS)
    body += '</tr></thead><tbody>'
    for file in summary['files']:
        body += '<tr><th><a href="' + quote(file['html'], safe='/') + '">' + html.escape(file['path']) + '</a></th>'
        for name in METRICS:
            value = file['metrics'][name]
            key = value['percent'] if value['percent'] is not None else -1
            body += f'<td data-value="{key}">{percentage(value)} ({value["covered"]}/{value["count"]})</td>'
        body += '</tr>'
    body += '</tbody></table></div><h2>Functions</h2><p>Counts and covered/uncovered status come from LLVM function records; instantiations retain their individual identities.</p>'
    body += '<div class="table-scroll"><table data-sortable><thead><tr><th><button>Function</button></th><th><button>Source</button></th><th><button>Executions</button></th><th><button>Status</button></th></tr></thead><tbody>'
    for function in summary['functions']:
        state = 'Covered' if function['count'] else 'Uncovered'
        body += '<tr class="' + state.lower() + '"><th>' + html.escape(function['name']) + '</th><td><a href="'
        body += quote(function['html'].rsplit('#L', 1)[0], safe='/') + '#L' + str(function['line']) + '">' + html.escape(function['path']) + ':' + str(function['line']) + '</a></td>'
        body += f'<td data-value="{function["count"]}">{function["count"]}</td><td>{state}</td></tr>'
    return page(body + '</tbody></table></div>')


STYLE = '''body{font:16px system-ui,sans-serif;margin:1.5rem auto;padding:0 1rem;max-width:90rem;color:#19232e;background:#fff}nav{padding:1rem 0;border-bottom:1px solid #ccd}a{color:#075da8}table{border-collapse:collapse;margin:1.3rem 0;width:100%}th,td{padding:.6rem;text-align:left;border-bottom:1px solid #dde;vertical-align:top}thead{background:#edf1f5}caption{text-align:left;font-size:1.3rem;font-weight:600;padding:.5rem 0}.table-scroll{overflow:auto}button{font:inherit;font-weight:600;color:inherit;border:0;background:none;cursor:pointer}.uncovered{background:#fff0ef}.covered{background:#f1faf3}th{overflow-wrap:anywhere}'''
SCRIPT = '''document.querySelectorAll('table[data-sortable]').forEach(table=>{table.querySelectorAll('thead button').forEach((button,column)=>{button.addEventListener('click',()=>{const direction=button.dataset.direction==='ascending'?-1:1;table.querySelectorAll('th').forEach(th=>th.removeAttribute('aria-sort'));button.dataset.direction=direction===1?'ascending':'descending';button.parentElement.setAttribute('aria-sort',button.dataset.direction);const rows=Array.from(table.tBodies[0].rows);rows.sort((a,b)=>{const x=a.cells[column],y=b.cells[column];return direction*(x.hasAttribute('data-value')&&y.hasAttribute('data-value')?Number(x.dataset.value)-Number(y.dataset.value):x.textContent.localeCompare(y.textContent));});rows.forEach(row=>table.tBodies[0].appendChild(row));});});});'''


def summarize(report, root, allowed, output):
    require(report.get('type') == 'llvm.coverage.json.export'
            and isinstance(report.get('version'), str) and report.get('data'),
            'llvm-cov produced no valid coverage export')
    totals = {name: {'count': 0, 'covered': 0} for name in METRICS}
    files, functions = [], []
    for entry in report['data']:
        require(isinstance(entry.get('files'), list) and entry['files']
                and isinstance(entry.get('totals'), dict), 'llvm-cov produced no valid coverage export')
        for file in entry['files']:
            path = Path(file['filename'])
            path = (root / path).resolve() if not path.is_absolute() else path.resolve()
            require(path in allowed, 'LLVM export includes an unowned production file: ' + str(path))
            metrics = {name: metric(file['summary'].get(name)) for name in METRICS}
            source = 'llvm/coverage/' + path.as_posix().lstrip('/') + '.html'
            require((output / source).is_file(), 'LLVM source HTML is missing: ' + source)
            files.append({'path': path.relative_to(root).as_posix(), 'html': source, 'metrics': metrics})
        for name in METRICS:
            value = metric(entry['totals'].get(name))
            for key in ('count', 'covered'):
                totals[name][key] += value[key]
        for function in entry.get('functions', []):
            regions = [r for r in function.get('regions', []) if len(r) >= 8 and r[7] == 0]
            if not regions:
                continue
            region = regions[0]
            path = Path(function['filenames'][region[5]])
            path = (root / path).resolve() if not path.is_absolute() else path.resolve()
            if path not in allowed:
                continue
            count = function['count']
            require(type(count) is int and count >= 0, 'invalid LLVM function execution count')
            functions.append({'name': function['name'], 'path': path.relative_to(root).as_posix(),
                              'line': region[0], 'count': count,
                              'html': 'llvm/coverage/' + path.as_posix().lstrip('/') + '.html#L' + str(region[0])})
    require(files, 'no instrumented project-owned production files')
    # Totals are LLVM's filtered export totals, never averages of percentages.
    summary = {'status': 'available', 'totals': {name: metric(value) for name, value in totals.items()},
               'files': sorted(files, key=lambda x: x['path']),
               'functions': sorted(functions, key=lambda x: (x['path'], x['line'], x['name']))}
    demangler = shutil.which('llvm-cxxfilt')
    if demangler and functions:
        result = subprocess.run([demangler], input='\n'.join(f['name'] for f in summary['functions']) + '\n',
                                text=True, capture_output=True, check=True)
        names = result.stdout.splitlines()
        require(len(names) == len(functions), 'incomplete function demangling')
        for function, name in zip(summary['functions'], names):
            function['symbol'] = function['name']
            function['name'] = name
    return summary


def generate(request):
    root = Path(request['root']).resolve()
    output = root / '.ecosystem/coverage'
    output.mkdir(parents=True, exist_ok=True)
    allowed = {Path(p).resolve() for p in request['sources']}
    require(allowed and all(p.is_relative_to(root) and p.is_file() for p in allowed),
            'no valid project-owned production coverage inputs')
    commands = []
    log = Path(request['log'])

    def run(args):
        start = time.monotonic()
        print('coverage: ' + ' '.join(args), flush=True)
        result = subprocess.run(args, cwd=root, text=True, capture_output=True)
        commands.append({'command': args, 'exit_code': result.returncode, 'seconds': time.monotonic() - start})
        (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        print(f'coverage: exit {result.returncode}, {commands[-1]["seconds"]:.3f}s', flush=True)
        return result

    common = [request['binaries'][0], '-instr-profile=' + request['profile']]
    for binary in request['binaries'][1:]:
        common += ['-object', binary]
    # LLVM response files preserve spaces and keep both argv and shell sizes bounded.
    response = output / 'sources.rsp'
    response.write_text('\n'.join(json.dumps(str(p), ensure_ascii=False) for p in sorted(allowed)) + '\n')
    selected = ['@' + str(response)]
    exported = run([request['llvm_cov'], 'export', *common, '-format=text', *selected])
    log.write_text(exported.stdout + exported.stderr)
    require(exported.returncode == 0, f'llvm-cov export failed (exit {exported.returncode}): ' + exported.stderr)
    try:
        report = json.loads(exported.stdout)
        require(isinstance(report, dict) and report.get('type') == 'llvm.coverage.json.export'
                and isinstance(report.get('version'), str) and report.get('data')
                and all(e.get('files') and isinstance(e.get('totals'), dict) for e in report['data']),
                'llvm-cov produced no valid coverage export')
    except (ValueError, TypeError, AttributeError) as error:
        raise ValueError('llvm-cov produced no valid coverage export') from error
    shown = run([request['llvm_cov'], 'show', *common, '-format=html', '-output-dir=' + str(output / 'llvm'),
                 '-show-line-counts-or-regions', '-show-regions', '-show-branches=count',
                 '-show-expansions', '-show-instantiations', *selected])
    if shown.stdout or shown.stderr:
        with log.open('a') as stream:
            stream.write(shown.stdout + shown.stderr)
    require(shown.returncode == 0, f'llvm-cov show failed (exit {shown.returncode}): ' + shown.stderr)
    require((output / 'llvm/index.html').is_file(), 'llvm-cov produced no HTML index')
    import posixpath
    import re
    for source in (output / 'llvm').rglob('*.html'):
        parent = source.relative_to(output).parent.as_posix()
        links = [('Documentation', '../../index.html'), ('C++ API (Doxygen)', '../doxygen/index.html'),
                 ('Code Coverage', 'index.html')]
        navigation = '<nav aria-label="Project documentation">' + ' · '.join(
            '<a href="' + quote(posixpath.relpath(target, parent), safe='/') + '">' + label + '</a>'
            for label, target in links) + '</nav>'
        def local_link(match):
            value = html.unescape(match[2])
            target = (source.parent / value).resolve()
            if target.is_relative_to(output) and target.is_file():
                return 'href=' + match[1] + html.escape(quote(value, safe='/'), quote=True) + match[1]
            return match[0]
        # LLVM emits literal source path characters in its own file table.
        # Encode existing file links while retaining native #L anchors/search.
        text = re.sub(r"href=([\"'])(.*?)\1", local_link, source.read_text())
        source.write_text(re.sub(r'(<body[^>]*>)', lambda match: match[0] + navigation,
                                 text, count=1, flags=re.I))
    summary = summarize(report, root, allowed, output)
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (output / 'index.html').write_text(render(summary))
    (output / 'coverage.css').write_text(STYLE)
    (output / 'coverage.js').write_text(SCRIPT)
    # Commands/profiles/binaries/LLVM JSON remain internal; the public bundle has only UI data.
    with zipfile.ZipFile(request['archive'], 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(output.rglob('*')):
            if path.is_file() and path.name not in ('sources.rsp', 'commands.json'):
                require(not path.is_symlink(), 'symlink in generated LLVM coverage')
                archive.write(path, path.relative_to(output).as_posix())
    Path(request['report']).write_text(exported.stdout)


if __name__ == '__main__':
    try:
        generate(json.loads(Path(sys.argv[1]).read_text()))
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
