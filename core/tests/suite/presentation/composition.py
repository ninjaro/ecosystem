import json

from .fixtures import PresentationCase, presentation


class CompositionTests(PresentationCase):
    def test_no_implicit_reports(self):
        self.write('.ecosystem/reports/coverage.json', 'stale unselected coverage')
        body, files, _ = presentation.compose(self.root)
        self.assertIn('No verification evidence was selected', body)
        self.assertNotIn('stale', body)
        self.assertIn('doxygen/index.html', files)

    def test_alias_and_size_limits(self):
        target = self.root / '.ecosystem/doxygen/html/link'
        target.symlink_to(self.root / 'manifest.json')
        with self.assertRaisesRegex(ValueError, 'symlink'):
            presentation.compose(self.root)
        target.unlink()
        from sphinx_presentation import inputs
        old = inputs.LIMIT
        try:
            inputs.LIMIT = 1
            with self.assertRaisesRegex(ValueError, 'exceed'):
                presentation.compose(self.root)
        finally:
            inputs.LIMIT = old

    def test_empty_api_index(self):
        self.write('.ecosystem/doxygen/html/index.html', '')
        with self.assertRaisesRegex(ValueError, 'Doxygen HTML'):
            presentation.compose(self.root)

    def test_benchmark_success_identity_and_failure(self):
        self.write('.ecosystem/reports/benchmark/bench/speed/result.json', json.dumps({'artifact': 'bench:speed', 'profile': 'release', 'status': 'passed', 'exit_code': 0, 'log': 'bench_log.txt'}))
        self.write('.ecosystem/reports/benchmark/bench/speed/bench_log.txt', '<script>1 GFLOPs</script>')
        body, _, _ = presentation.compose(self.root)
        self.assertIn('&lt;script&gt;1 GFLOPs', body)
        self.assertIn('separate from', body)
        self.write('.ecosystem/reports/benchmark/bench/speed/result.json', '{"status":"failed","exit_code":1}')
        body, _, _ = presentation.compose(self.root)
        self.assertIn('No successful local benchmark', body)

