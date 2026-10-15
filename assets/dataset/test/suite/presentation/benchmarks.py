import json
from unittest.mock import patch

from sphinx_presentation.benchmark_data import file_stamp
from .fixtures import PresentationCase, presentation


class BenchmarkTests(PresentationCase):
    prefix = '.ecosystem/reports/benchmark/bench/speed/'
    destination = 'benchmarks/bench/speed/'

    def snapshot(self, summary=None, svg=None):
        result = dict(artifact='bench:speed', profile='release', status='passed', exit_code=0,
                      log='bench_log.txt', command=['speed', 'two words', '--mode=flops'])
        summary = summary if summary is not None else {'series': [
            {'algorithm': '<fast> & safe', 'points': [{'n': 0, 'gflops': 0}, {'n': 32, 'gflops': 1.25}]},
            {'algorithm': 'other', 'points': [{'n': 32, 'gflops': 2.5}]}]}
        svg = svg if svg is not None else '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 960 540"><text x="10" y="20">GFLOPs/s</text></svg>'
        files = {'result.json': json.dumps(result), 'bench_log.txt': '<script>raw log</script>',
                 'summary.json': json.dumps(summary), 'bench_plot.svg': svg}
        record = {'kind': 'flops', 'artifact': 'bench:speed',
                  'files': {name: file_stamp(value.encode()) for name, value in files.items()}}
        for name, value in files.items():
            self.write(self.prefix + name, value)
        self.write(self.prefix + 'presentation.json', json.dumps(record))
        return files, record

    def render(self):
        return presentation.compose(self.root)

    def test_chart_and_table(self):
        expected, _ = self.snapshot()
        body, files, _ = self.render()
        self.assertIn('<figure>', body)
        self.assertIn('GFLOPs/s versus n for bench:speed', body)
        self.assertIn('Raw plotted measurements', body)
        self.assertIn('&lt;fast&gt; &amp; safe', body)
        self.assertIn('<td>32</td><td>1.25</td>', body)
        self.assertIn('<td>0</td><td>0</td>', body)
        self.assertIn('Captured command', body)
        self.assertIn('two words', body)
        self.assertIn('&lt;script&gt;raw log', body)
        self.assertIn('separate from', body)
        self.assertIn('Coverage unavailable', files['coverage/index.html'].decode())
        for name, value in expected.items():
            self.assertEqual(files[self.destination + name], value.encode())
        self.assertIn(self.destination + 'presentation.json', files)

    def test_legacy_and_generic_results(self):
        self.snapshot()
        (self.root / (self.prefix + 'presentation.json')).unlink()
        body, files, _ = self.render()
        self.assertIn('No current specialized plot', body)
        self.assertNotIn('<figure>', body)
        self.assertNotIn(self.destination + 'bench_plot.svg', files)
        self.assertIn(self.destination + 'bench_log.txt', files)

    def test_stale_plot_inputs(self):
        for name in ('result.json', 'bench_log.txt', 'summary.json', 'bench_plot.svg'):
            with self.subTest(name=name):
                self.snapshot()
                path = self.root / (self.prefix + name)
                path.write_bytes(path.read_bytes() + b' ')
                body, files, _ = self.render()
                self.assertIn('stale benchmark plot input: ' + name, body)
                self.assertNotIn(self.destination + 'bench_plot.svg', files)
                self.assertIn(self.destination + 'bench_log.txt', files)

    def test_invalid_plot_records(self):
        for key, value in [('schema', True), ('kind', 'latency'), ('artifact', 'other:speed'), ('files', {})]:
            with self.subTest(key=key):
                _, record = self.snapshot()
                record[key] = value
                self.write(self.prefix + 'presentation.json', json.dumps(record))
                body, files, _ = self.render()
                self.assertIn('Optional plot unavailable', body)
                self.assertNotIn(self.destination + 'bench_plot.svg', files)
        for name in ('summary.json', 'bench_plot.svg'):
            with self.subTest(missing=name):
                self.snapshot()
                (self.root / (self.prefix + name)).unlink()
                body, _files, _ = self.render()
                self.assertIn('Optional plot unavailable', body)

    def test_invalid_series(self):
        for point in ({'n': True, 'gflops': 1}, {'n': -1, 'gflops': 1},
                      {'n': 1, 'gflops': -1}, {'n': 1, 'gflops': True},
                      {'n': 1, 'gflops': float('inf')}):
            with self.subTest(point=point):
                self.snapshot(summary={'series': [{'algorithm': 'bad', 'points': [point]}]})
                body, files, _ = self.render()
                self.assertIn('Optional plot unavailable', body)
                self.assertNotIn(self.destination + 'bench_plot.svg', files)
        self.snapshot(summary={'series': [{'algorithm': 'bad', 'points': [
            {'n': 32, 'gflops': 1}, {'n': 16, 'gflops': 2}]}]})
        body, _files, _ = self.render()
        self.assertIn('unordered benchmark size', body)

    def test_static_svg_only(self):
        for contents in ('<script>alert(1)</script>', '<foreignObject/>', '<image href="https://example.invalid/a"/>',
                         '<text onclick="bad()">a</text>', '<rect fill="url(external)"/>'):
            with self.subTest(contents=contents):
                self.snapshot(svg='<svg xmlns="http://www.w3.org/2000/svg">' + contents + '</svg>')
                body, files, _ = self.render()
                self.assertIn('Optional plot unavailable', body)
                self.assertNotIn(self.destination + 'bench_plot.svg', files)
        self.snapshot(svg='not XML')
        self.assertIn('invalid benchmark SVG', self.render()[0])
        self.snapshot(svg='<!DOCTYPE svg><svg xmlns="http://www.w3.org/2000/svg"/>')
        self.assertIn('declarations are unsupported', self.render()[0])
        styled = ('<?xml-stylesheet href="https://example.invalid/style.css"?>'
                  '<svg xmlns="http://www.w3.org/2000/svg"/>')
        self.snapshot(svg=styled)
        self.assertIn('declarations are unsupported', self.render()[0])
        self.snapshot(svg=styled.encode('utf-16-le').decode('utf-8'))
        self.assertIn('must be UTF-8 XML', self.render()[0])

    def test_result_identity(self):
        for key, value in [('profile', 'debug'), ('artifact', 'other:speed'),
                           ('log', '../outside.log'), ('exit_code', False)]:
            with self.subTest(key=key):
                files, _record = self.snapshot()
                result = json.loads(files['result.json'])
                result[key] = value
                self.write(self.prefix + 'result.json', json.dumps(result))
                with self.assertRaises(ValueError):
                    self.render()

    def test_plot_limits_and_aliases(self):
        self.snapshot()
        with patch('sphinx_presentation.benchmark_data.LIMIT', 1):
            body, files, _ = self.render()
            self.assertIn('exceeds 8 MiB', body)
            self.assertNotIn(self.destination + 'bench_plot.svg', files)
        plot = self.root / (self.prefix + 'bench_plot.svg')
        plot.unlink()
        plot.symlink_to(self.root / 'manifest.json')
        body, files, _ = self.render()
        self.assertIn('symlink', body)
        self.assertNotIn(self.destination + 'bench_plot.svg', files)

    def test_failed_and_unowned_results(self):
        self.snapshot()
        self.write(self.prefix + 'result.json', '{"status":"failed","exit_code":1}')
        body, files, _ = self.render()
        self.assertIn('No successful local benchmark', body)
        self.assertNotIn(self.destination + 'bench_log.txt', files)
        self.snapshot()
        self.write('manifest.json', '{"id":"example","artifacts":[]}')
        body, _files, _ = self.render()
        self.assertIn('No successful local benchmark', body)
