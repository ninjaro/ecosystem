"""Render an explicitly selected canonical diagnostic snapshot."""
import html
import json
from pathlib import Path

from .inputs import link, pre, require, safe_path


def validate(report, project):
    require(isinstance(report, dict) and isinstance(project, str) and project
            and report.get('project') == project,
            'diagnostic snapshot project mismatch')
    require(report.get('profile') in ('cxx', 'naming', 'style')
            and report.get('status') in ('clean', 'findings', 'failed'),
            'invalid diagnostic profile or status')
    require(isinstance(report.get('findings'), list)
            and isinstance(report.get('errors'), list)
            and all(isinstance(error, str) for error in report['errors']),
            'invalid diagnostic findings or errors')
    require(not report['errors'] or report['status'] == 'failed',
            'diagnostic errors require failed status')
    require('artifact' not in report or isinstance(report['artifact'], str),
            'invalid diagnostic artifact')
    for finding in report['findings']:
        require(isinstance(finding, dict), 'invalid diagnostic finding')
        for key in ('rule', 'category', 'severity', 'authority', 'enforcement',
                    'artifact', 'file', 'entity_kind', 'entity', 'message'):
            require(isinstance(finding.get(key), str), 'invalid diagnostic ' + key)
        for key in ('line', 'column'):
            require(type(finding.get(key)) is int and finding[key] >= 0,
                    'invalid diagnostic ' + key)
        require('referent' in finding and (finding['referent'] is None
                or isinstance(finding['referent'], str)), 'invalid diagnostic referent')
        require(isinstance(finding.get('measurements'), dict)
                and isinstance(finding.get('thresholds'), dict),
                'invalid diagnostic measurements or thresholds')


def field(label, value):
    text = value if isinstance(value, str) else json.dumps(value, ensure_ascii=False)
    return '<dt>' + html.escape(label) + '</dt><dd><code>' + html.escape(text) + '</code></dd>'


def options(findings, key):
    return '<option value="">All</option>' + ''.join(
        '<option value="' + html.escape(value, quote=True) + '">'
        + html.escape(value) + '</option>' for value in sorted({f[key] for f in findings}))


def diagnostics(inputs, selected):
    if not selected:
        return '<p>No diagnostic snapshot was selected.</p>'
    path = safe_path(inputs.root, selected)
    require(path.is_file() and path.stat().st_size <= 8 * 1024 * 1024,
            'diagnostic snapshot is missing or exceeds 8 MiB')
    data = inputs.attach(selected, 'diagnostics/report.json')
    report = json.loads(data)
    manifest = json.loads(inputs.read('manifest.json'))
    validate(report, manifest.get('id'))
    body = '<section data-diagnostic-report><h3>Local diagnostic snapshot</h3>'
    body += '<p>Source: <code>' + html.escape(selected) + '</code>. '
    body += link('diagnostics/report.json', 'Download canonical JSON') + '</p><dl>'
    for key in ('project', 'profile', 'status', 'artifact', 'root'):
        if key in report:
            body += field(key, report[key])
    body += '</dl><p data-diagnostic-count aria-live="polite">' + str(len(report['findings'])) + ' findings</p>'
    body += '<div class="diagnostic-filters" hidden>'
    for key in ('category', 'severity'):
        body += '<label>' + key.capitalize() + ' <select data-diagnostic-filter="' + key + '">'
        body += options(report['findings'], key) + '</select></label> '
    body += '</div>'
    for error in report['errors']:
        body += '<p class="diagnostic-error"><strong>Analysis failure:</strong> ' + html.escape(error) + '</p>'
    groups = {}
    for finding in report['findings']:
        groups.setdefault((finding['category'], finding['severity']), []).append(finding)
    rank = {'fatal': 0, 'error': 0, 'warning': 1, 'note': 2, 'hint': 3}
    for category, severity in sorted(groups, key=lambda k: (k[0], rank.get(k[1], 3), k[1])):
        findings = groups[category, severity]
        body += '<section data-diagnostic-group data-category="' + html.escape(category, quote=True)
        body += '" data-severity="' + html.escape(severity, quote=True) + '"><h4>'
        body += html.escape(category + ' / ' + severity) + ' (' + str(len(findings)) + ')</h4>'
        for finding in findings:
            body += '<details data-diagnostic-finding><summary>' + html.escape(finding['rule'])
            body += ' — ' + html.escape(finding['entity_kind'] + ' ' + finding['entity']) + '</summary>'
            body += '<p>' + html.escape(finding['message']) + '</p><dl>'
            for key in ('file', 'line', 'column', 'artifact', 'entity_kind', 'entity',
                        'referent', 'authority', 'enforcement', 'measurements', 'thresholds'):
                body += field(key, finding[key])
            body += '</dl></details>'
        body += '</section>'
    measurements = {key: value for key, value in report.items() if key != 'findings'}
    body += '<details><summary>Raw scope and measurements</summary>'
    body += pre(json.dumps(measurements, indent=2, ensure_ascii=False)) + '</details></section>'
    script = Path(__file__).with_name('diagnostics.js')
    inputs.generated('diagnostics/filters.js', script.read_bytes())
    style = Path(__file__).with_name('diagnostics.css')
    inputs.generated('diagnostics/style.css', style.read_bytes())
    body += '<link rel="stylesheet" href="diagnostics/style.css">'
    body += '<script defer src="diagnostics/filters.js"></script>'
    return body
