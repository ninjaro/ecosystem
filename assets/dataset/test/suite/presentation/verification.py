import hashlib
import json
import subprocess

from .fixtures import PresentationCase, presentation


class VerificationTests(PresentationCase):
    def test_checked_coverage_and_escaped_reports(self):
        directory, _ = self.evidence()
        body, files, _ = presentation.compose(self.root, directory)
        coverage = files['coverage/index.html'].decode()
        for metric in ('Lines', 'Functions', 'Branches', 'Regions'):
            self.assertIn(metric, coverage)
        self.assertIn('70.0%', coverage)
        self.assertIn('data-sortable', coverage)
        self.assertIn('covered()', coverage)
        self.assertIn('uncovered()', coverage)
        self.assertIn('Covered', coverage)
        self.assertIn('Uncovered', coverage)
        self.assertNotIn('01-required-checks', body + coverage)
        self.assertNotIn('do not execute', body + coverage)
        self.assertNotIn('artifact_digest', body + coverage)

    def test_manual_only_receipt_cannot_authorize_publication(self):
        directory, _ = self.evidence(kind='pages')
        with self.assertRaisesRegex(ValueError, 'unknown receipt'):
            presentation.compose(self.root, directory)

    def test_unsupported_coverage_ignores_old_export(self):
        directory, _ = self.evidence('skipped')
        self.write('.ecosystem/evidence/reports/coverage.json', 'old export')
        body, files, _ = presentation.compose(self.root, directory)
        self.assertIn('Coverage unavailable', files['coverage/index.html'].decode())
        self.assertNotIn('old export', body)

    def test_receipt_failures(self):
        directory, original = self.evidence()
        for field, value in [('schema', 1), ('kind', 'other'), ('coverage', 'failed'), ('tested_tree', 'stale'), ('tested_commit', ''), ('policy', {'project': 'other'}), ('files', {}), ('verification', None)]:
            with self.subTest(field=field):
                self.write('.ecosystem/evidence/receipt.json', json.dumps(dict(original, **{field: value})))
                with self.assertRaises(ValueError):
                    presentation.compose(self.root, directory)

    def test_narrow_scope_cannot_be_presented_as_full(self):
        directory, receipt = self.evidence()
        receipt['verification']['stages']['format'] = 'changed'
        self.write('.ecosystem/evidence/receipt.json', json.dumps(receipt))
        with self.assertRaisesRegex(ValueError, 'scope'):
            presentation.compose(self.root, directory)

    def test_invalid_classification_binding(self):
        directory, receipt = self.evidence()
        name = 'github/selection/checks.json'
        original = json.loads((directory / name).read_text())
        for section, key, value in [('input', 'event_commit', '0' * 40), ('input', 'event', 'push'),
                                    ('report', 'project', 'other'), ('report', 'classification', 'presentation')]:
            with self.subTest(key=key):
                selection = json.loads(json.dumps(original))
                selection[section][key] = value
                data = json.dumps(selection)
                self.write('.ecosystem/evidence/' + name, data)
                receipt['files'][name] = {'bytes': len(data.encode()), 'sha256': hashlib.sha256(data.encode()).hexdigest()}
                self.write('.ecosystem/evidence/receipt.json', json.dumps(receipt))
                with self.assertRaisesRegex(ValueError, 'classification'):
                    presentation.compose(self.root, directory)

    def test_missing_classification_snapshot(self):
        directory, receipt = self.evidence()
        receipt['files'].pop('github/selection/checks.json')
        self.write('.ecosystem/evidence/receipt.json', json.dumps(receipt))
        with self.assertRaisesRegex(ValueError, 'report set'):
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


    def test_foreign_commit_with_identical_tree_is_rejected(self):
        directory, receipt = self.evidence()
        receipt['tested_commit'] = '0' * 40
        self.write('.ecosystem/evidence/receipt.json', json.dumps(receipt))
        with self.assertRaisesRegex(ValueError, 'revision differs'):
            presentation.compose(self.root, directory)

    def test_html_artifact_integrity_and_contents(self):
        import io
        import zipfile
        directory, receipt = self.evidence()
        name = 'reports/coverage.zip'
        original = (directory / name).read_bytes()
        self.write('.ecosystem/evidence/' + name, original + b'tampered')
        with self.assertRaisesRegex(ValueError, 'digest'):
            presentation.compose(self.root, directory)
        with zipfile.ZipFile(io.BytesIO(original)) as archive:
            files = {entry: archive.read(entry) for entry in archive.namelist()}
        for change in ('traversal', 'internal-receipt', 'missing-source', 'missing-function', 'wrong-totals'):
            selected = dict(files)
            summary = json.loads(selected['summary.json'])
            if change == 'traversal':
                selected['../outside.html'] = b'forbidden'
            elif change == 'internal-receipt':
                selected['receipt.json'] = b'internal evidence'
            elif change == 'missing-source':
                selected.pop(summary['files'][0]['html'])
            elif change == 'missing-function':
                summary['functions'][0]['html'] = 'llvm/missing.html#L1'
            else:
                summary['totals']['lines']['covered'] = 0
            selected['summary.json'] = json.dumps(summary).encode()
            output = io.BytesIO()
            with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
                for path, data in selected.items():
                    archive.writestr(path, data)
            data = output.getvalue()
            self.write('.ecosystem/evidence/' + name, data)
            receipt['files'][name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
            self.write('.ecosystem/evidence/receipt.json', json.dumps(receipt))
            with self.subTest(change=change), self.assertRaises(ValueError):
                presentation.compose(self.root, directory)
    def test_publication_consumes_coverage_without_analysis_tools(self):
        from unittest.mock import patch
        original = subprocess.run
        directory, _ = self.evidence()
        def only_identity_commands(args, *positional, **options):
            self.assertEqual(args[0], 'git', 'publication may inspect Git identity but must not execute tests or LLVM')
            return original(args, *positional, **options)
        with patch('subprocess.run', side_effect=only_identity_commands):
            _, files, _ = presentation.compose(self.root, directory)
        self.assertIn('70.0%', files['coverage/index.html'].decode())

    def test_empty_denominators_are_not_invented_percentages(self):
        from coverage_site import metric, percentage
        self.assertIsNone(metric({'count': 0, 'covered': 0})['percent'])
        self.assertEqual(percentage(metric({'count': 0, 'covered': 0})), 'Not applicable')
        with self.assertRaises(ValueError):
            metric({'count': 1, 'covered': 2})
