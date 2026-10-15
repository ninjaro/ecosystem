import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import ecosystem_presentation as presentation
from .media import image
from coverage_fixture import fixture as coverage_fixture


def preview_converter(case):
    def convert(args, **kwargs):
        destination = Path(args[-1])
        if destination.suffix != '.png':
            destination = destination.with_suffix('.png')
        image(destination)
        return subprocess.CompletedProcess(args, 0, b'')
    case.addCleanup(patch.stopall)
    patch('sphinx_presentation.previews.shutil.which', return_value='fixture-converter').start()
    return patch('sphinx_presentation.previews.subprocess.run', side_effect=convert).start()


class PresentationCase(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='ecosystem-presentation-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.write('manifest.json', json.dumps({'id': 'example', 'artifacts': [{'id': 'bench:speed'}]}))
        self.write('.gitignore', '.ecosystem/\n')
        self.write('.ecosystem/doxygen/html/index.html', '<h1>C++ API</h1>')
        self.git('init', '-q')
        self.git('config', 'user.name', 'Fixture')
        self.git('config', 'user.email', 'fixture@example.invalid')
        self.git('add', '.')
        self.git('commit', '-qm', 'fixture')

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.root), '-c', 'commit.gpgsign=false', *args], text=True).strip()

    def write(self, name, value):
        file = self.root / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(value) if isinstance(value, bytes) else file.write_text(value)

    def evidence(self, status='passed', kind='checks'):
        prefix = '.ecosystem/evidence/'
        receipt = {'kind': kind, 'policy': {'project': 'example'},
                   'tested_commit': self.git('rev-parse', 'HEAD'), 'tested_tree': self.git('rev-parse', 'HEAD^{tree}'),
                   'coverage': status, 'files': {}}
        if kind == 'checks':
            receipt['verification'] = {'scope': 'full', 'selection': {'kind': 'whole-tree',
                'tree': receipt['tested_tree'], 'manifest': self.git('rev-parse', 'HEAD:manifest.json')},
                'stages': {stage: 'full' for stage in ('repository', 'build', 'tests', 'java', 'tidy', 'format', 'coverage')}}
        stages = ['01-required-checks' if kind == 'checks' else '01-tracked-surface', '02-coverage-check']
        files = {f'github/reports/{stage}/{name}': '<script>do not execute</script>' for stage in stages for name in ['summary.md', 'output.log']}
        if kind == 'checks':
            receipt['event'] = 'workflow_dispatch'
            files['github/selection/checks.json'] = json.dumps({'input': {'event': receipt['event'], 'event_commit': receipt['tested_commit'],
                          'checkout': None, 'complete': False, 'paths': [], 'changes': []},
                'report': {'project': 'example', 'delta_validated': False,
                           'classification': 'full', 'format': {'scope': 'full'}}})
        if status == 'passed':
            files['reports/coverage.json'], files['reports/coverage.zip'] = coverage_fixture(self.root)
        for name, data in files.items():
            self.write(prefix + name, data)
            encoded = data if isinstance(data, bytes) else data.encode()
            receipt['files'][name] = {'bytes': len(encoded), 'sha256': hashlib.sha256(encoded).hexdigest()}
        self.write(prefix + 'receipt.json', json.dumps(receipt))
        return self.root / prefix, receipt

