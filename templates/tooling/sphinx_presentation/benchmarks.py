"""Separate local benchmark snapshots."""
import html
import json
from urllib.parse import quote

from .inputs import link, pre, require, safe_path
from .benchmark_data import load_plot


def plot_view(inputs, prefix, destination, identity, result, log):
    try:
        selected = load_plot(inputs, prefix, identity, result, log)
    except (ValueError, OSError, TypeError, KeyError, OverflowError) as error:
        return '<p>Optional plot unavailable: ' + html.escape(str(error)) + '</p>'
    if selected is None:
        return '<p>No current specialized plot is available for this snapshot.</p>'
    summary, files = selected
    for name in ('summary.json', 'bench_plot.svg', 'presentation.json'):
        target = destination + name
        require(target not in inputs.files, 'duplicate benchmark output: ' + target)
        inputs.files[target] = files[name]
    body = '<figure><img loading="lazy" src="' + quote(destination + 'bench_plot.svg', safe='/') + '" alt="'
    body += html.escape('GFLOPs/s versus n for ' + identity, quote=True) + '">'
    body += '<figcaption>FLOPs throughput versus problem size n.</figcaption></figure>'
    body += '<p>' + link(destination + 'bench_plot.svg', 'SVG plot') + ' · '
    body += link(destination + 'summary.json', 'Series JSON') + '</p>'
    body += '<p>Mean counters take precedence; otherwise repeated samples are averaged. '
    body += 'These saved series describe this benchmark run.</p>'
    body += '<div style="overflow-x:auto"><table><caption>Raw plotted measurements</caption>'
    body += '<thead><tr><th scope="col">Algorithm</th><th scope="col">n</th>'
    body += '<th scope="col">GFLOPs/s</th></tr></thead><tbody>'
    for series in summary['series']:
        for point in series['points']:
            body += '<tr><th scope="row">' + html.escape(series['algorithm']) + '</th>'
            body += '<td>' + str(point['n']) + '</td><td>' + str(point['gflops']) + '</td></tr>'
    return body + '</tbody></table></div>'


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
        data = inputs.read(relative)
        result = json.loads(data)
        require(isinstance(result, dict), 'invalid benchmark result: ' + relative)
        if result.get('status') != 'passed' or result.get('exit_code') != 0:
            continue
        require(type(result['exit_code']) is int, 'invalid benchmark exit code: ' + relative)
        require(result.get('artifact') == identity and result.get('profile') == 'release' and result.get('log') == 'bench_log.txt', 'invalid benchmark identity: ' + relative)
        prefix = path.parent.relative_to(inputs.root).as_posix() + '/'
        destination = 'benchmarks/' + path.parent.parent.name + '/' + path.parent.name + '/'
        log = inputs.attach(prefix + 'bench_log.txt', destination + 'bench_log.txt')
        require(destination + 'result.json' not in inputs.files, 'duplicate benchmark result')
        inputs.files[destination + 'result.json'] = data
        body += '<section><h3>' + html.escape(identity) + '</h3>'
        body += '<p>Profile: release · Status: passed · Exit code: 0</p>'
        body += '<p>' + link(destination + 'result.json', 'Run JSON') + ' · '
        body += link(destination + 'bench_log.txt', 'Raw log') + '</p>'
        if 'command' in result:
            require(isinstance(result['command'], list)
                    and all(isinstance(arg, str) for arg in result['command']), 'invalid benchmark command')
            body += '<details><summary>Captured command</summary>' + pre(json.dumps(result['command'])) + '</details>'
        body += plot_view(inputs, prefix, destination, identity, data, log)
        body += '<details><summary>Captured benchmark output</summary>'
        body += pre(log.decode('utf-8', errors='replace')) + '</details></section>'
        count += 1
    return body if count else '<p>No successful local benchmark output is available.</p>'
