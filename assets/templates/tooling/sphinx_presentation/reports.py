"""Validate selected verification evidence without publishing internal reports."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

from .inputs import require


def change_selection(selection, identity, project, manifest):
    require(isinstance(selection, dict) and 'schema' not in selection, 'invalid change selection')
    require(identity.get('event') in ('push', 'pull_request', 'workflow_dispatch', 'schedule')
            and isinstance(identity.get('commit'), str)
            and re.fullmatch('[0-9a-f]{40}', identity['commit']), 'invalid classification provenance')
    event, report = selection.get('input'), selection.get('report')
    require(isinstance(event, dict) and isinstance(report, dict), 'missing classification inputs')
    require('schema' not in event and 'schema' not in report
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


def verified_coverage(inputs, evidence):
    if not evidence:
        return None
    # Selection is explicit. Local files are integrity checked, not a substitute
    # for the CI caller's workflow/job/artifact authorization checks.
    directory = Path(evidence).absolute().relative_to(inputs.root.absolute())
    prefix = directory.as_posix() + '/'
    receipt = json.loads(inputs.read(prefix + 'receipt.json'))
    kind = receipt.get('kind')
    require(kind in ('checks', 'publication')
            and 'schema' not in receipt, 'unknown receipt')
    project = json.loads(inputs.read('manifest.json'))['id']
    require(receipt.get('policy', {}).get('project') == project, 'receipt project differs')
    def git(*args):
        return subprocess.check_output(['git', '-C', str(inputs.root), *args], text=True).strip()
    require(receipt.get('tested_tree') == git('rev-parse', 'HEAD^{tree}')
            and receipt.get('tested_commit') == git('rev-parse', 'HEAD'), 'receipt revision differs')
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
    stages = ['01-required-checks', '02-coverage-check']
    stage_files = {stage: ('summary.md', 'output.log') for stage in stages}
    if receipt['kind'] == 'publication':
        source = receipt.get('verification', {})
        require('schema' not in source and source.get('commit') == receipt['tested_commit']
                and source.get('tree') == receipt['tested_tree'] and source.get('pushes')
                and source.get('checks') and source.get('codeql'), 'missing publication provenance')
        full_scope(source['checks'].get('verification'), check_stages)
        full_scope(source['codeql'].get('verification'), ('repository', 'build', 'analysis'))
        stage_files['01-tracked-surface'] = ('summary.md', 'output.log')
        stage_files['02-codeql-build'] = ('summary.md', 'output.log')
        stage_files['03-codeql-analysis'] = ('summary.md',)
    expected = {f'github/reports/{stage}/{name}' for stage, names in stage_files.items() for name in names}
    selections = ['checks', 'codeql'] if kind == 'publication' else ['checks']
    expected.update(f'github/selection/{selected}.json' for selected in selections)
    if coverage == 'passed':
        expected.update(('reports/coverage.json', 'reports/coverage.zip'))
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
    if coverage == 'skipped':
        return {'status': 'skipped'}
    return {'status': 'passed', 'report': json.loads(reports['reports/coverage.json']),
            'archive': reports['reports/coverage.zip']}
