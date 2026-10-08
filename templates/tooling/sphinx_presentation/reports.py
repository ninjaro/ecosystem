"""Selected verification evidence and separate local benchmark snapshots."""
import hashlib
import html
import json
from pathlib import Path
import re
import subprocess

from .inputs import pre, require, safe_path


def change_selection(selection, identity, project, manifest):
    require(isinstance(selection, dict) and selection.get('schema') == 1, 'invalid change selection')
    require(identity.get('event') in ('push', 'pull_request', 'workflow_dispatch', 'schedule')
            and isinstance(identity.get('commit'), str)
            and re.fullmatch('[0-9a-f]{40}', identity['commit']), 'invalid classification provenance')
    event, report = selection.get('input'), selection.get('report')
    require(isinstance(event, dict) and isinstance(report, dict), 'missing classification inputs')
    require(event.get('schema') == 1 and report.get('schema') == 1
            and event.get('event') == identity.get('event') and event.get('event_commit') == identity.get('commit')
            and event.get('checkout') in (None, identity.get('commit'))
            and type(event.get('complete')) is bool and isinstance(event.get('paths'), list)
            and isinstance(event.get('changes'), list) and report.get('project') == project
            and type(report.get('delta_validated')) is bool
            and report.get('classification') in ('full', 'affected', 'presentation', 'none'), 'invalid classification identity')
    if event['complete']:
        require(event.get('checkout') == identity['commit']
                and all(isinstance(event.get(key), str) and re.fullmatch('[0-9a-f]{40}', event[key])
                        for key in ('base', 'head', 'comparison_base')),
                'incomplete change comparison')
        pr = identity.get('pr') or {}
        require((event['event'] == 'pull_request' and event['base'] == pr.get('base') and event['head'] == pr.get('head'))
                or (event['event'] == 'push' and event['head'] == identity['commit'] and event['comparison_base'] == event['base']),
                'change event differs')
    else:
        require(not event['paths'] and not event['changes'] and not report['delta_validated'], 'incomplete delta has a selection')
    if report['delta_validated']:
        comparison = {key: event.get(key) for key in ('event', 'checkout', 'base', 'head', 'comparison_base')}
        comparison['manifest_blob'] = manifest
        require(event['complete'] and report.get('comparison') == comparison, 'classification comparison differs')
    else:
        require(report['classification'] == 'full' and report.get('format', {}).get('scope') == 'full', 'unvalidated narrow classification')


def verification(inputs, evidence):
    if not evidence:
        return '<p>No verification evidence was selected for this documentation build.</p>'
    # Selection is explicit. Local files are integrity checked, not a substitute
    # for the CI caller's workflow/job/artifact authorization checks.
    directory = Path(evidence).absolute().relative_to(inputs.root.absolute())
    prefix = directory.as_posix() + '/'
    receipt = json.loads(inputs.read(prefix + 'receipt.json'))
    kind = receipt.get('kind')
    require(kind in ('checks', 'pages', 'publication')
            and receipt.get('schema') == {'checks': 3, 'publication': 2, 'pages': 1}[kind], 'unknown receipt')
    project = json.loads(inputs.read('manifest.json'))['id']
    require(receipt.get('policy', {}).get('project') == project, 'receipt project differs')
    def git(*args):
        return subprocess.check_output(['git', '-C', str(inputs.root), *args], text=True).strip()
    require(receipt.get('tested_tree') == git('rev-parse', 'HEAD^{tree}'), 'receipt tree differs')
    git('diff', '--quiet', '--ignore-submodules=none', 'HEAD')
    require(not git('ls-files', '--others', '--exclude-standard'), 'untracked authored inputs differ')
    require(re.fullmatch('[0-9a-f]{40}', receipt.get('tested_commit', '')), 'invalid tested revision')
    def full_scope(value, stages):
        expected = {'scope': 'full', 'selection': {'kind': 'whole-tree', 'tree': receipt['tested_tree'],
                    'manifest': git('rev-parse', 'HEAD:manifest.json')},
                    'stages': {stage: 'full' for stage in stages}}
        require(value == expected, 'incomplete verification scope')
    check_stages = ('repository', 'build', 'tests', 'java', 'tidy', 'format', 'coverage')
    if kind == 'checks':
        full_scope(receipt.get('verification'), check_stages)
    coverage = receipt.get('coverage')
    require(coverage in ('passed', 'skipped'), 'coverage is incomplete')
    stages = ['01-tracked-surface', '02-coverage-check'] if receipt['kind'] == 'pages' else ['01-required-checks', '02-coverage-check']
    stage_files = {stage: ('summary.md', 'output.log') for stage in stages}
    if receipt['kind'] == 'publication':
        source = receipt.get('verification', {})
        require(source.get('schema') == 1 and source.get('commit') == receipt['tested_commit']
                and source.get('tree') == receipt['tested_tree'] and source.get('pushes')
                and source.get('checks') and source.get('codeql'), 'missing publication provenance')
        full_scope(source['checks'].get('verification'), check_stages)
        full_scope(source['codeql'].get('verification'), ('repository', 'build', 'analysis'))
        stage_files['01-tracked-surface'] = ('summary.md', 'output.log')
        stage_files['02-codeql-build'] = ('summary.md', 'output.log')
        stage_files['03-codeql-analysis'] = ('summary.md',)
    expected = {f'github/reports/{stage}/{name}' for stage, names in stage_files.items() for name in names}
    selections = ['checks', 'codeql'] if kind == 'publication' else ['checks'] if kind == 'checks' else []
    expected.update(f'github/selection/{selected}.json' for selected in selections)
    if coverage == 'passed':
        expected.add('reports/coverage.json')
    require(isinstance(receipt.get('files'), dict) and set(receipt['files']) == expected, 'incomplete report set')
    reports = {}
    for name, record in sorted(receipt['files'].items()):
        data = inputs.read(prefix + name)
        require(len(data) == record.get('bytes') and hashlib.sha256(data).hexdigest() == record.get('sha256'), 'report digest differs: ' + name)
        require(data or name.endswith('/output.log'), 'empty report: ' + name)
        reports[name] = data
    for selected in selections:
        name = f'github/selection/{selected}.json'
        identity = {'event': receipt.get('event'), 'commit': receipt['tested_commit'], 'pr': receipt.get('pr')}
        if kind == 'publication':
            identity = receipt['verification'][selected].get('change', {})
            require(isinstance(identity, dict) and all(identity.get(key) == receipt['files'][name][key]
                    for key in ('bytes', 'sha256')), 'classification provenance differs')
        change_selection(json.loads(reports[name]), identity, project, git('rev-parse', 'HEAD:manifest.json'))
    source = {key: receipt.get(key) for key in ('kind', 'repository', 'run_id', 'run_attempt', 'tested_commit', 'tested_tree')}
    if receipt['kind'] in ('checks', 'publication'):
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
