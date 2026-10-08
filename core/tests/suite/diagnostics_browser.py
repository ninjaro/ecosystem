"""Check native diagnostic snapshots and Pages filters in Chromium."""
import html
import importlib.util
from importlib.machinery import SourceFileLoader
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
module = Path(sys.argv[1]).absolute()
engels = Path(sys.argv[2]).absolute()
browser = sys.argv[3] if len(sys.argv) > 3 else 'chromium'
sys.path.insert(0, str(module.parent))
loader = SourceFileLoader('manifesto_presentation', str(module))
site = importlib.util.module_from_spec(importlib.util.spec_from_loader(loader.name, loader))
loader.exec_module(site)

with tempfile.TemporaryDirectory(prefix='manifesto-diagnostics-browser-') as temporary:
    root = Path(temporary)
    (root / 'manifest.json').write_text(json.dumps({'id': 'browser', 'description': 'Diagnostic browser fixture',
        'facade': 'core:model', 'artifacts': [
        {'id': 'core:model', 'kind': 'static_lib', 'root': 'core', 'owns': ['model']}]}))
    header = root / 'core/include/model.hpp'
    header.parent.mkdir(parents=True)
    header.write_text('#pragma once\ninline int BadName(int value) { if (value) return 1; return 0; }\n'
                      '#error incomplete_report\n')
    arguments = [str(engels), 'report', 'style', 'core:model']
    native = subprocess.run(arguments + ['--json'], cwd=root, capture_output=True, text=True, timeout=30)
    assert native.returncode == 5, native.stdout + native.stderr
    name = '.ecosystem/reports/style.json'
    snapshot = (root / name).read_bytes()
    report = json.loads(snapshot)
    assert json.loads(native.stdout) == report and report['errors'] and report['findings']
    ci = subprocess.run(arguments + ['--markdown'], cwd=root, capture_output=True, text=True, timeout=30)
    assert ci.returncode == 5 and (root / name).read_bytes() == snapshot, ci.stdout + ci.stderr
    assert '**Analysis failure:**' in ci.stdout and 'entity' in ci.stdout
    (root / '.ecosystem/doxygen/html').mkdir(parents=True)
    (root / '.ecosystem/doxygen/html/index.html').write_text('API')
    body, files, _ = site.compose(root, diagnostic_snapshot=name)
    assert files['diagnostics/report.json'] == snapshot
    for finding in report['findings']:
        for key in ('rule', 'entity', 'referent'):
            if finding[key]:
                assert html.escape(finding[key]) in body
        assert html.escape(json.dumps(finding['measurements'])) in body
    output = root / 'site'
    for name, data in files.items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data.read_bytes() if isinstance(data, Path) else data)
    harness = r'''<script>
window.addEventListener('load', () => {
  const result = document.createElement('output');
  result.id = 'diagnostic-result';
  const check = (value, message) => { if (!value) throw Error(message); };
  try {
    const report = document.querySelector('[data-diagnostic-report]');
    const groups = [...report.querySelectorAll('[data-diagnostic-group]')];
    const count = report.querySelector('[data-diagnostic-count]');
    const filters = report.querySelector('.diagnostic-filters');
    const category = report.querySelector('[data-diagnostic-filter="category"]');
    const severity = report.querySelector('[data-diagnostic-filter="severity"]');
    const select = (input, value) => { input.value = value; input.dispatchEvent(new Event('change')); };
    check(!filters.hidden && groups.length > 1, 'controls enabled and multiple categories');
    const status = count.textContent;
    select(category, category.options[1].value);
    check(groups.some(group => group.hidden) && groups.some(group => !group.hidden), 'category filter');
    const missing = document.createElement('option');
    missing.value = 'absent'; missing.textContent = 'Fixture absent'; severity.append(missing);
    select(severity, 'absent');
    check(groups.every(group => group.hidden) && count.textContent.startsWith('0/'), 'combined empty filter');
    check(report.querySelector('.diagnostic-error').textContent.includes('Analysis failure'), 'failures remain visible');
    select(category, ''); select(severity, '');
    check(groups.every(group => !group.hidden) && count.textContent === status, 'reset restores all findings');
    result.textContent = 'PASS';
  } catch (error) { result.textContent = 'FAIL: ' + error.message; }
  document.body.append(result);
});
</script>'''
    page = output / 'index.html'
    page.write_text('<!doctype html><meta charset="utf-8"><main>' + body + '</main>' + harness)
    base = [browser, '--headless=new', '--no-sandbox', '--disable-gpu', '--disable-dev-shm-usage',
            '--allow-file-access-from-files', '--no-first-run', '--no-default-browser-check',
            '--dump-dom', '--virtual-time-budget=2000']
    result = subprocess.run(base + ['--user-data-dir=' + str(root / 'browser'), page.as_uri()],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    assert '<output id="diagnostic-result">PASS</output>' in result.stdout, result.stdout[-3000:]
    # Keep Chromium's DOM engine enabled; remove both application and test scripts.
    page.write_text('<!doctype html><meta charset="utf-8"><main>'
                    + body.replace('<script defer src="diagnostics/filters.js"></script>', '') + '</main>')
    plain = subprocess.run(base + ['--user-data-dir=' + str(root / 'no-script'),
                                  page.as_uri()], capture_output=True, text=True, timeout=30)
    assert plain.returncode == 0, plain.stderr
    assert 'data-diagnostic-group hidden' not in plain.stdout
    assert 'Analysis failure:' in plain.stdout, plain.stdout[-3000:]
    assert all(html.escape(finding['rule']) in plain.stdout for finding in report['findings']), plain.stdout[-3000:]
    assert 'data-diagnostic-count' in plain.stdout and ' findings</p>' in plain.stdout
    print('PASS native JSON/CI/Pages identity, filters, failures and script-free findings')
