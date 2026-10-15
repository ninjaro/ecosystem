"""Consume sealed coverage output without executing LLVM or tests."""
import io
import json
from pathlib import PurePosixPath
import stat
import zipfile

from coverage_site import METRICS, SCRIPT, STYLE, metric, render
from .inputs import require


def coverage_files(inputs, selected):
    if selected is None:
        summary = {'status': 'unavailable', 'reason': 'No verified Checks coverage was selected.'}
    elif selected['status'] == 'skipped':
        summary = {'status': 'unavailable', 'reason': 'Coverage is unsupported by this project.'}
    else:
        raw, data = selected['report'], selected['archive']
        require(raw.get('type') == 'llvm.coverage.json.export' and raw.get('data'), 'invalid LLVM coverage export')
        expected = {name: {'count': 0, 'covered': 0} for name in METRICS}
        for entry in raw['data']:
            require(entry.get('files'), 'incomplete LLVM coverage export')
            for name in METRICS:
                value = metric(entry['totals'].get(name))
                for key in ('count', 'covered'):
                    expected[name][key] += value[key]
        expected = {name: metric(value) for name, value in expected.items()}
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            entries = archive.infolist()
            require(len(entries) <= 10000 and sum(e.file_size for e in entries) <= 512 * 1024 * 1024,
                    'coverage archive exceeds limits')
            names = set()
            for entry in entries:
                name = entry.filename
                require(name and not name.startswith('/') and '\\' not in name and ':' not in name
                        and all(p not in ('', '.', '..') for p in name.split('/'))
                        and name not in names and not entry.is_dir()
                        and stat.S_IFMT(entry.external_attr >> 16) in (0, stat.S_IFREG), 'unsafe coverage archive')
                require(name in ('index.html', 'summary.json', 'coverage.css', 'coverage.js')
                        or (name.startswith('llvm/') and PurePosixPath(name).suffix.lower() in ('.html', '.css', '.js')),
                        'unexpected coverage payload')
                names.add(name)
            require({'index.html', 'summary.json', 'coverage.js', 'coverage.css', 'llvm/index.html'} <= names,
                    'incomplete coverage HTML artifact')
            summary = json.loads(archive.read('summary.json'))
            require(summary.get('status') == 'available' and summary.get('totals') == expected,
                    'coverage summary differs from measured LLVM totals')
            for file in summary.get('files', []):
                require(file.get('html') in names and file['html'].startswith('llvm/coverage/'), 'missing source coverage page')
                for name in METRICS:
                    metric(file['metrics'].get(name))
            require(summary.get('files') and isinstance(summary.get('functions'), list), 'missing file/function coverage')
            for function in summary['functions']:
                require(function.get('html', '').rsplit('#L', 1)[0] in names
                        and type(function.get('count')) is int and function['count'] >= 0,
                        'invalid function coverage')
            for entry in entries:
                # These bytes have been selected by a SHA-validated Checks receipt.
                inputs.generated('coverage/' + entry.filename, archive.read(entry))
        return
    inputs.generated('coverage/index.html', render(summary).encode())
    inputs.generated('coverage/coverage.css', STYLE.encode())
    inputs.generated('coverage/coverage.js', SCRIPT.encode())
