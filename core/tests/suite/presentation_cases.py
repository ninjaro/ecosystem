"""Exercise rendered presentation code without requiring a Sphinx environment."""
import hashlib
import importlib.util
from importlib.machinery import SourceFileLoader
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
loader = SourceFileLoader('presentation', sys.argv.pop(1))
spec = importlib.util.spec_from_loader(loader.name, loader)
presentation = importlib.util.module_from_spec(spec)
loader.exec_module(presentation)


class PresentationTests(unittest.TestCase):
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
        receipt = {'schema': 1, 'kind': kind, 'policy': {'project': 'example'},
                   'tested_commit': self.git('rev-parse', 'HEAD'), 'tested_tree': self.git('rev-parse', 'HEAD^{tree}'),
                   'coverage': status, 'files': {}}
        stages = ['01-required-checks' if kind == 'checks' else '01-tracked-surface', '02-coverage-check']
        files = {f'github/reports/{stage}/{name}': '<script>do not execute</script>' for stage in stages for name in ['summary.md', 'output.log']}
        if status == 'passed':
            files['reports/coverage.json'] = json.dumps({'type': 'llvm.coverage.json.export', 'data': [{'files': [{}], 'totals': {'lines': {'count': 10, 'covered': 7}, 'branches': {'count': 0, 'covered': 0}}}]})
        for name, data in files.items():
            self.write(prefix + name, data)
            receipt['files'][name] = {'bytes': len(data.encode()), 'sha256': hashlib.sha256(data.encode()).hexdigest()}
        self.write(prefix + 'receipt.json', json.dumps(receipt))
        return self.root / prefix, receipt

    def test_no_implicit_reports(self):
        self.write('.ecosystem/reports/coverage.json', 'stale unselected coverage')
        body, files = presentation.compose(self.root)
        self.assertIn('No verification evidence was selected', body)
        self.assertNotIn('stale', body)
        self.assertIn('doxygen/index.html', files)

    def test_checked_coverage_and_escaped_reports(self):
        directory, _ = self.evidence()
        body, _ = presentation.compose(self.root, directory)
        self.assertIn('70.0%', body)
        self.assertIn('Not applicable', body)
        self.assertIn('&lt;script&gt;', body)
        self.assertNotIn('<script>', body)

    def test_manual_prerequisites_do_not_claim_full_checks(self):
        directory, _ = self.evidence(kind='pages')
        body, _ = presentation.compose(self.root, directory)
        self.assertIn('01-tracked-surface', body)
        self.assertNotIn('01-required-checks', body)

    def test_unsupported_coverage_ignores_old_export(self):
        directory, _ = self.evidence('skipped')
        self.write('.ecosystem/evidence/reports/coverage.json', 'old export')
        body, _ = presentation.compose(self.root, directory)
        self.assertIn('Unsupported', body)
        self.assertNotIn('old export', body)

    def test_receipt_failures(self):
        directory, original = self.evidence()
        for field, value in [('schema', 2), ('kind', 'other'), ('coverage', 'failed'), ('tested_tree', 'stale'), ('tested_commit', ''), ('policy', {'project': 'other'}), ('files', {})]:
            with self.subTest(field=field):
                self.write('.ecosystem/evidence/receipt.json', json.dumps(dict(original, **{field: value})))
                with self.assertRaises(ValueError):
                    presentation.compose(self.root, directory)

    def test_tampered_report(self):
        directory, _ = self.evidence()
        self.write('.ecosystem/evidence/reports/coverage.json', 'tampered')
        with self.assertRaisesRegex(ValueError, 'digest'):
            presentation.compose(self.root, directory)

    def test_dirty_source(self):
        directory, _ = self.evidence()
        self.write('.gitignore', 'changed')
        with self.assertRaises(subprocess.CalledProcessError):
            presentation.compose(self.root, directory)

    def test_malformed_coverage(self):
        directory, receipt = self.evidence()
        name = 'reports/coverage.json'
        for data in ['{}', '{"type":"llvm.coverage.json.export","data":[{"files":[{}],"totals":{"lines":{"count":2,"covered":3}}}]}']:
            with self.subTest(data=data):
                self.write('.ecosystem/evidence/' + name, data)
                receipt['files'][name] = {'bytes': len(data), 'sha256': hashlib.sha256(data.encode()).hexdigest()}
                self.write('.ecosystem/evidence/receipt.json', json.dumps(receipt))
                with self.assertRaises(ValueError):
                    presentation.compose(self.root, directory)

    def test_showcase_order_and_escaping(self):
        self.write('assets/showcase/index.tsv', 'id\tpath\ttype\tdescription\tdatetime\nb\tb #.png\timage\t<script>\t2026\na\ta.pdf\tpdf\tSecond\t2025\n')
        self.write('assets/showcase/b #.png', 'image')
        self.write('assets/showcase/a.pdf', 'pdf')
        self.write('assets/showcase/unselected.png', 'not indexed')
        body, files = presentation.compose(self.root)
        self.assertLess(body.index('&lt;script&gt;'), body.index('Second'))
        self.assertIn('b%20%23.png', body)
        self.assertNotIn('<script>', body)
        self.assertNotIn('showcase/unselected.png', files)

    def test_showcase_unsafe_and_missing_inputs(self):
        for name, kind in [('../escape.png', 'image'), ('/escape.png', 'image'), ('nested\\escape.png', 'image'), ('missing.png', 'image'), ('script.svg', 'image'), ('script.html', 'pdf')]:
            with self.subTest(name=name):
                self.write('assets/showcase/index.tsv', f'id\tpath\ttype\tdescription\tdatetime\na\t{name}\t{kind}\tLabel\t2026\n')
                with self.assertRaises((ValueError, FileNotFoundError)):
                    presentation.compose(self.root)

    def test_showcase_duplicate_identity(self):
        self.write('assets/showcase/a.png', 'image')
        self.write('assets/showcase/index.tsv', 'id\tpath\ttype\tdescription\tdatetime\na\ta.png\timage\tA\t2026\na\ta.png\timage\tB\t2026\n')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            presentation.compose(self.root)

    def test_alias_and_size_limits(self):
        target = self.root / '.ecosystem/doxygen/html/link'
        target.symlink_to(self.root / 'manifest.json')
        with self.assertRaisesRegex(ValueError, 'symlink'):
            presentation.compose(self.root)
        target.unlink()
        old = presentation.LIMIT
        try:
            presentation.LIMIT = 1
            with self.assertRaisesRegex(ValueError, 'exceed'):
                presentation.compose(self.root)
        finally:
            presentation.LIMIT = old

    def test_benchmark_success_identity_and_failure(self):
        self.write('.ecosystem/reports/benchmark/bench/speed/result.json', json.dumps({'artifact': 'bench:speed', 'profile': 'release', 'status': 'passed', 'exit_code': 0, 'log': 'bench_log.txt'}))
        self.write('.ecosystem/reports/benchmark/bench/speed/bench_log.txt', '<script>1 GFLOPs</script>')
        body, _ = presentation.compose(self.root)
        self.assertIn('&lt;script&gt;1 GFLOPs', body)
        self.assertIn('separate from', body)
        self.write('.ecosystem/reports/benchmark/bench/speed/result.json', '{"status":"failed","exit_code":1}')
        body, _ = presentation.compose(self.root)
        self.assertIn('No successful local benchmark', body)


if __name__ == '__main__':
    unittest.main()
