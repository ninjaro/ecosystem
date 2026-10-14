import csv

from .fixtures import PresentationCase, presentation, preview_converter


class AssetsTests(PresentationCase):
    def setUp(self):
        super().setUp()
        preview_converter(self)

    def index(self, category, rows):
        self.write(f'assets/{category}/index.tsv',
                   'id\tpath\ttype\tdescription\tdatetime\n' + rows)

    def test_category_pages_and_files(self):
        for category in ('sets', 'docs'):
            with self.subTest(category=category):
                self.index(category, 'page\tindex.html\tdata\tStored page\t\n')
                self.write(f'assets/{category}/index.html', 'Selected authored data')
                self.write(f'assets/{category}/unselected.txt', 'Not selected')
                body, files, pages = presentation.compose(self.root)
                self.assertIn(f'catalog/{category}/index.html', body)
                self.assertEqual(files[f'assets/{category}/index.html'], b'Selected authored data')
                names = [name for name, _, _ in pages]
                index = names.index(f'_ecosystem/catalog/{category}/index')
                self.assertIn(f'../../assets/{category}/index.html', pages[index][1]['body'])
                self.assertNotIn(f'assets/{category}/unselected.txt', files)

    def test_video_and_carousel_controls(self):
        self.index('showcase', 'video\tdemo.webm\tvideo\tDemo & notes\t2026\n'
                   'image\tstill.png\timage\tStill\t\n')
        self.write('assets/showcase/demo.webm', 'Video fixture')
        self.write('assets/showcase/still.png', 'Image fixture')
        body, files, pages = presentation.compose(self.root)
        self.assertIn('<video controls preload="metadata"', body)
        self.assertIn('Demo &amp; notes', body)
        self.assertIn('aria-roledescription="carousel"', body)
        self.assertIn('aria-label="1 of 2"', body)
        self.assertIn('carousel-controls" hidden', body)
        self.assertNotIn('autoplay', body)
        self.assertEqual(set(files) & {'carousel.css', 'carousel.js'}, {'carousel.css', 'carousel.js'})
        self.assertIn('../../showcase/demo.webm', pages[0][1]['body'])

    def test_empty_index_and_readme_marker(self):
        self.index('showcase', '')
        body, _, pages = presentation.compose(self.root)
        self.assertIn('No indexed showcase media', body)
        self.assertNotIn('carousel-controls', pages[0][1]['body'])
        self.write('assets/showcase/readme.md', 'Missing carousel marker')
        with self.assertRaisesRegex(ValueError, 'carousel'):
            presentation.compose(self.root)

    def test_index_shape_and_metadata(self):
        for rows in ('a\ta.png\timage\tShort row\n',
                     'a\ta.png\timage\t"Unclosed\t2026\n',
                     'a\ta.png\timage\tNUL\0\t2026\n',
                     '\ta.png\timage\tNo ID\t2026\n',
                     'a\treadme.md\tdata\tMetadata\t2026\n'):
            with self.subTest(rows=rows):
                self.index('sets', rows)
                with self.assertRaises((ValueError, csv.Error)):
                    presentation.compose(self.root)

    def test_duplicate_path_and_alias(self):
        self.index('sets', 'a\ta.json\tdata\tA\t\nb\ta.json\tdata\tB\t\n')
        self.write('assets/sets/a.json', '{}')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            presentation.compose(self.root)
        self.index('sets', 'a\talias.json\tdata\tAlias\t\n')
        (self.root / 'assets/sets/alias.json').symlink_to(self.root / 'manifest.json')
        with self.assertRaisesRegex(ValueError, 'symlink'):
            presentation.compose(self.root)

    def test_showcase_order_and_escaping(self):
        self.write('assets/showcase/index.tsv', 'id\tpath\ttype\tdescription\tdatetime\nb\tb #.png\timage\t<script>\t2026\na\ta.pdf\tpdf\tSecond\t2025\n')
        self.write('assets/showcase/b #.png', 'image')
        self.write('assets/showcase/a.pdf', 'pdf')
        self.write('assets/showcase/unselected.png', 'not indexed')
        body, files, _ = presentation.compose(self.root)
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
