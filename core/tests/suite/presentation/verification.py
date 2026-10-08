import hashlib
import json
import subprocess

from .fixtures import PresentationCase, presentation


class VerificationTests(PresentationCase):
    def test_checked_coverage_and_escaped_reports(self):
        directory, _ = self.evidence()
        body, _, _ = presentation.compose(self.root, directory)
        self.assertIn('70.0%', body)
        self.assertIn('Not applicable', body)
        self.assertIn('&lt;script&gt;', body)
        self.assertNotIn('<script>', body)

    def test_manual_prerequisites_do_not_claim_full_checks(self):
        directory, _ = self.evidence(kind='pages')
        body, _, _ = presentation.compose(self.root, directory)
        self.assertIn('01-tracked-surface', body)
        self.assertNotIn('01-required-checks', body)

    def test_unsupported_coverage_ignores_old_export(self):
        directory, _ = self.evidence('skipped')
        self.write('.ecosystem/evidence/reports/coverage.json', 'old export')
        body, _, _ = presentation.compose(self.root, directory)
        self.assertIn('Unsupported', body)
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


