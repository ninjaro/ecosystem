import json

from .fixtures import PresentationCase, presentation


class CompositionTests(PresentationCase):
    def test_development_docs_cannot_influence_generated_publication(self):
        original = presentation.compose(self.root)
        self.write('docs/index.md', '[invalid](missing.md)')
        self.write('docs/index.rst', '.. missing-directive::')
        self.write('docs/sphinx.json', '{malformed')
        self.write('docs/conf.py', "raise RuntimeError('must not run')")
        self.write('docs/receipt.json', '{malformed')
        self.write('docs/findings.json', '{malformed')
        self.assertEqual(presentation.compose(self.root), original)
        with self.assertRaisesRegex(ValueError, 'root docs/ is not a publication input'):
            presentation.compose(self.root, self.root / 'docs')
        with self.assertRaisesRegex(ValueError, 'root docs/ is not a publication input'):
            presentation.compose(self.root, diagnostic_snapshot='docs/findings.json')

    def test_no_implicit_reports(self):
        self.write('.ecosystem/reports/coverage.json', 'stale unselected coverage')
        body, files, _ = presentation.compose(self.root)
        self.assertIn('Coverage unavailable', files['coverage/index.html'].decode())
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

    def test_global_navigation_in_both_theme_layouts_and_nested_pages(self):
        import posixpath
        from types import SimpleNamespace
        from urllib.parse import urljoin
        try:
            from jinja2 import Environment, FileSystemLoader, ChoiceLoader, DictLoader
        except ImportError:
            Environment = None
        templates = __import__('pathlib').Path(presentation.__file__).parent / 'templates'
        # The generated layout extends both the RTD menu and basic sidebar hooks.
        for base in ('{% block menu %}native toc{% endblock %}', '{% block sidebarextra %}native sidebar{% endblock %}'):
            layout = (templates / 'layout.html').read_text()
            self.assertIn('block menu', layout)
            self.assertIn('block sidebarextra', layout)
            self.assertIn('super()', layout)
            env = Environment(loader=ChoiceLoader([FileSystemLoader(templates), DictLoader({'!layout.html': base})])) if Environment else None
            for page in ('index', 'guide/nested'):
                app = SimpleNamespace(builder=SimpleNamespace(get_relative_uri=lambda origin, target:
                    posixpath.relpath(target + '.html', posixpath.dirname(origin) or '.')))
                context = {}
                presentation.page_context(app, page, 'page.html', context, None)
                output = env.get_template('layout.html').render(**context) if env else 'native ' + context['ecosystem_navigation']
                self.assertIn('native', output)
                for label in ('Documentation', 'C++ API (Doxygen)', 'Code Coverage'):
                    self.assertIn(label, output)
                from html.parser import HTMLParser
                links = []
                class Parser(HTMLParser):
                    def handle_starttag(self, tag, attributes):
                        if tag == 'a':
                            links.append(dict(attributes)['href'])
                Parser().feed(output)
                resolved = [urljoin('https://example.invalid/project/' + page + '.html', link) for link in links]
                self.assertEqual(resolved, ['https://example.invalid/project/index.html',
                    'https://example.invalid/project/_ecosystem/doxygen/index.html',
                    'https://example.invalid/project/_ecosystem/coverage/index.html'])

    def test_doxygen_native_navigation_survives_coverage_link_integration(self):
        from types import SimpleNamespace
        from pathlib import Path
        self.write('.ecosystem/doxygen/html/index.html', '<html><body><nav>Native search and graphs</nav></body></html>')
        self.write('.ecosystem/doxygen/html/nested/api.html', '<html><body>Native cross references</body></html>')
        app = SimpleNamespace(confdir=self.root / '.ecosystem/sphinx', outdir=self.root / '.ecosystem/output',
                              env=SimpleNamespace(found_docs={'index'}))
        Path(app.outdir).mkdir(parents=True)
        presentation.collect_pages(app)
        index = (Path(app.outdir) / '_ecosystem/doxygen/index.html').read_text()
        nested = (Path(app.outdir) / '_ecosystem/doxygen/nested/api.html').read_text()
        self.assertIn('Native search and graphs', index)
        self.assertIn('href="../coverage/index.html"', index)
        self.assertIn('href="../../coverage/index.html"', nested)
