import copy
import json

from .fixtures import PresentationCase, presentation


class DiagnosticTests(PresentationCase):
    def snapshot(self):
        finding = dict(rule='style.scope', category='style', severity='hint',
                       authority='manifesto', enforcement='advisory', artifact='core:app',
                       file='include/model.hpp', line=1, column=2, entity_kind='function',
                       entity='BadName', referent='include/model', message='<script>bad</script>',
                       measurements={'lines': 12}, thresholds={'lines': 8})
        return dict(project='example', profile='style', status='failed', artifact='core:app',
                    findings=[finding, dict(finding, rule='clang.error', category='compiler',
                                            severity='error', authority='external', enforcement='required')],
                    errors=['incomplete <analysis>'], files=[{'file': 'include/model.hpp', 'effective_lines': 12}],
                    scopes=[], referents=['include/model'], modules=[])

    def render(self, report):
        name = '.ecosystem/reports/style.json'
        self.write(name, json.dumps(report))
        return presentation.compose(self.root, diagnostic_snapshot=name)

    def test_snapshot_views(self):
        report = self.snapshot()
        body, files, _ = self.render(report)
        self.assertEqual(json.loads(files['diagnostics/report.json']), report)
        self.assertIn('style / hint (1)', body)
        self.assertIn('compiler / error (1)', body)
        self.assertIn('BadName', body)
        self.assertIn('include/model', body)
        self.assertIn('&quot;lines&quot;: 12', body)
        self.assertIn('&quot;lines&quot;: 8', body)
        self.assertIn('Analysis failure:', body)
        self.assertIn('incomplete &lt;analysis&gt;', body)
        self.assertNotIn('<script>bad', body)
        self.assertIn('data-diagnostic-filter="category"', body)
        self.assertIn('data-diagnostic-filter="severity"', body)
        self.assertIn('Raw scope and measurements', body)
        self.assertIn('No verification evidence was selected', body)
        self.assertIn('diagnostics/filters.js', files)
        self.assertIn('diagnostics/style.css', files)

    def test_clean_snapshot(self):
        report = self.snapshot()
        report.update(status='clean', errors=[], findings=[])
        body, files, _ = self.render(report)
        self.assertIn('>clean</code>', body)
        self.assertIn('0 findings</p>', body)
        self.assertNotIn('data-diagnostic-group', body)
        self.assertEqual(json.loads(files['diagnostics/report.json']), report)

    def test_no_implicit_snapshot(self):
        self.render(self.snapshot())
        body, files, _ = presentation.compose(self.root)
        self.assertIn('No diagnostic snapshot was selected', body)
        self.assertNotIn('diagnostics/report.json', files)
        self.assertNotIn('BadName', body)

    def test_snapshot_validation(self):
        for key, value in [('project', 'other'), ('profile', 'matrix'), ('status', 'passed'),
                           ('findings', {}), ('errors', [None])]:
            with self.subTest(key=key):
                report = self.snapshot()
                report[key] = value
                with self.assertRaises(ValueError):
                    self.render(report)
        for key, value in [('entity', None), ('line', True), ('column', -1),
                           ('referent', []), ('measurements', []), ('thresholds', None)]:
            with self.subTest(finding=key):
                report = copy.deepcopy(self.snapshot())
                report['findings'][0][key] = value
                with self.assertRaises(ValueError):
                    self.render(report)
        report = self.snapshot()
        report['status'] = 'clean'
        with self.assertRaisesRegex(ValueError, 'failed status'):
            self.render(report)

    def test_snapshot_ownership(self):
        name = '.ecosystem/reports/style.json'
        path = self.root / name
        path.parent.mkdir(parents=True)
        path.symlink_to(self.root / 'manifest.json')
        with self.assertRaisesRegex(ValueError, 'symlink'):
            presentation.compose(self.root, diagnostic_snapshot=name)
        path.unlink()
        path.write_bytes(b'x' * (8 * 1024 * 1024 + 1))
        with self.assertRaisesRegex(ValueError, '8 MiB'):
            presentation.compose(self.root, diagnostic_snapshot=name)
        for name in ('../report.json', '/tmp/report.json', '.ecosystem//reports/style.json'):
            with self.assertRaisesRegex(ValueError, 'unsafe input path'):
                presentation.compose(self.root, diagnostic_snapshot=name)
