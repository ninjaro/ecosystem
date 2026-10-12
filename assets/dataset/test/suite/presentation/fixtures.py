import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import manifesto_presentation as presentation
from .media import image


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
        self.temporary = tempfile.TemporaryDirectory(prefix='manifesto-presentation-')
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
        file.write_text(value)

    def evidence(self, status='passed', kind='checks'):
        prefix = '.ecosystem/evidence/'
        receipt = {'schema': 3 if kind == 'checks' else 1, 'kind': kind, 'policy': {'project': 'example'},
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
            files['github/selection/checks.json'] = json.dumps({'schema': 1,
                'input': {'schema': 1, 'event': receipt['event'], 'event_commit': receipt['tested_commit'],
                          'checkout': None, 'complete': False, 'paths': [], 'changes': []},
                'report': {'schema': 1, 'project': 'example', 'delta_validated': False,
                           'classification': 'full', 'format': {'scope': 'full'}}})
        if status == 'passed':
            files['reports/coverage.json'] = json.dumps({'type': 'llvm.coverage.json.export', 'data': [{'files': [{}], 'totals': {'lines': {'count': 10, 'covered': 7}, 'branches': {'count': 0, 'covered': 0}}}]})
        for name, data in files.items():
            self.write(prefix + name, data)
            receipt['files'][name] = {'bytes': len(data.encode()), 'sha256': hashlib.sha256(data.encode()).hexdigest()}
        self.write(prefix + 'receipt.json', json.dumps(receipt))
        return self.root / prefix, receipt

