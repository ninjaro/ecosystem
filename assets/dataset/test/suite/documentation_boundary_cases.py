"""Regressions for standalone specifications; executed by the C++ docs suite."""

import importlib.util
import runpy
import shutil
from types import SimpleNamespace
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest


sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location("documentation_boundary", sys.argv.pop(1))
boundary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(boundary)


class DocumentationBoundary(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "unit"
        self.root.mkdir()

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def errors(self):
        return "\n".join(boundary.Boundary(self.root).check())

    def test_valid_markdown_and_rst(self):
        self.write("index.rst", "Index\n=====\n\n.. toctree::\n\n   Reference <reference/page>\n\n:doc:`/reference/page`\n:ref:`contract`\n")
        self.write("reference/page.md", "(contract)=\n# `Current` API\n\n[home](../index.rst#index)\n[local](#current-api)\n[picture][svg]\n\n[svg]: ../art.svg\n")
        self.write("art.svg", "<svg/>")
        self.assertEqual(self.errors(), "")

    def test_escaping_paths(self):
        (self.root.parent / "outside.md").write_text("# Outside\n")
        for target in ("../outside.md", "%2e%2e/outside.md", "nested/../../outside.md", "/tmp/outside.md", "..\\outside.md"):
            with self.subTest(target=target):
                self.write("index.md", f"# Unit\n[outside]({target})\n")
                self.assertTrue(self.errors())

    def test_symlink_escape(self):
        outside = self.root.parent / "outside.md"
        outside.write_text("# Outside\n")
        (self.root / "linked.md").symlink_to(outside)
        self.write("index.md", "[outside](linked.md)\n")
        self.assertIn("leaves documentation unit", self.errors())

    def test_anchors_and_missing_files(self):
        for target, expected in (("missing.md", "missing dependency"), ("#missing", "missing anchor")):
            self.write("index.md", f"# Unit\n[bad]({target})\n")
            self.assertIn(expected, self.errors())
        self.write("index.md", '# Unit\n## Repeat\n## Repeat\n## S-02 — Contract\n<a id="explicit"></a>\n[x](#repeat-1) [y](#explicit) [z](#s-02--contract)\n')
        self.assertEqual(self.errors(), "")

    def test_reference_links(self):
        self.write("index.md", "[outside][doc]\n\n[doc]: ../outside.md\n")
        self.assertIn("leaves documentation unit", self.errors())
        self.write("index.md", "[missing][doc]\n")
        self.assertIn("undefined Markdown reference", self.errors())

    def test_rst_links_and_roles(self):
        for link in ("`outside <../outside.rst>`_", ".. _outside: ../outside.rst", ":doc:`../outside`", ":download:`file <../outside.txt>`", ":ref:`missing-label`"):
            with self.subTest(link=link):
                self.write("index.rst", link + "\n")
                self.assertTrue(self.errors())

    def test_html_and_external_dependencies(self):
        for link in ('<img src="../image.svg">', '<img src=../image.svg>', '<https://example.invalid/guide>', '<a href="../outside.md">link</a>', "[background](https://example.invalid/guide)", ".. image:: https://example.invalid/logo.svg"):
            self.write("index.md", link + "\n")
            self.assertTrue(self.errors())

    def test_nested_include(self):
        self.write("index.rst", ".. include:: sections/part.txt\n")
        self.write("sections/part.txt", ".. include:: ../../outside.txt\n")
        self.assertIn("sections/part.txt: dependency leaves", self.errors())
        self.write("sections/part.txt", "Self-contained part.\n")
        self.assertEqual(self.errors(), "")

    def test_rst_toctree_and_glob(self):
        self.write("index.rst", ".. toctree::\n   :glob:\n\n   parts/*\n")
        self.write("parts/one.rst", "One\n===\n")
        self.assertEqual(self.errors(), "")
        self.write("index.rst", ".. toctree::\n   :glob:\n\n   ../*\n")
        self.assertIn("glob leaves documentation unit", self.errors())

    def test_myst_directive_and_roles(self):
        self.write("index.md", "```{toctree}\npage\n```\n{doc}`page`\n{ref}`current`\n")
        self.write("page.md", "(current)=\n# Page\n")
        self.assertEqual(self.errors(), "")
        self.write("index.md", ":::{include} ../outside.md\n:::\n")
        self.assertIn("leaves documentation unit", self.errors())

    def test_embedded_file_options(self):
        for name, option in (("raw", "file"), ("raw", "url"), ("csv-table", "file")):
            self.write("index.rst", f".. {name}:: html\n   :{option}: ../outside.txt\n")
            self.assertIn("leaves documentation unit", self.errors())

    def test_source_generated_directives(self):
        for name in ("doxygenstruct", "automodule", "autosummary", "bibliography", "program-output"):
            self.write("index.rst", f".. {name}:: source\n")
            self.assertIn("source-derived dependency", self.errors())

    def test_code_examples_are_not_dependencies(self):
        self.write("index.md", "# Examples\n`[literal](../outside.md)`\n`<a href=../outside.md>literal</a>`\n```rst\n.. include:: ../outside.rst\n```\n```{code-block} rst\n.. automodule:: outside\n```\n<!-- [hidden](../outside.md) -->\n")
        self.write("page.rst", ".. code-block:: rst\n\n   .. include:: ../outside.rst\n\nExample::\n\n   :doc:`../outside`\n")
        self.assertEqual(self.errors(), "")

    def test_substitution_and_executable_config(self):
        self.write("index.rst", ".. |logo| image:: ../logo.svg\n")
        self.assertIn("leaves documentation unit", self.errors())
        self.write("index.rst", "Index\n=====\n")
        self.write("conf.py", "exec(open('../config.py').read())\n")
        self.assertIn("executable Sphinx config", self.errors())

    def test_rst_named_hyperlinks(self):
        self.write("index.rst", "Contract\n========\n\n`Contract`_\n")
        self.assertEqual(self.errors(), "")
        self.write("index.rst", "`Missing`_\n")
        self.assertIn("missing RST hyperlink target", self.errors())

    def test_missing_entry_point(self):
        self.assertIn("entry point is required", self.errors())

    def test_multiple_units_keep_independent_boundaries(self):
        self.write("index.md", "# First unit\n")
        other = self.root.parent / "other"
        other.mkdir()
        (other / "index.md").write_text("# Second unit\n")
        command = [sys.executable, boundary.__file__, str(self.root), str(other)]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count("self-contained:"), 2)
        (other / "index.md").write_text("[not internal](../unit/index.md)\n")
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 1)
        self.assertIn("leaves documentation unit", result.stdout)
        self.assertEqual(result.stdout.count("self-contained:"), 1)

    def test_unknown_directive_requires_review(self):
        self.write("index.rst", ".. custom-import:: source\n")
        self.assertIn("needs boundary review", self.errors())


class SphinxConfiguration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name)
        self.source = self.project / 'assets/docs'
        self.source.mkdir(parents=True)
        (self.source / 'index.md').write_text('# Stable contract\n')
        self.generated = self.project / '.ecosystem/sphinx'
        self.generated.mkdir(parents=True)
        shutil.copyfile(boundary.__file__, self.generated / 'ecosystem_documentation_boundary.py')
        template = Path(boundary.__file__).with_name('sphinx_conf.py.tpl').read_text()
        for key, value in {'project_name': '"fixture"', 'exclude_patterns': '["_build"]',
                           'extensions': "    'myst_parser',\n", 'source_suffix_markdown': "    '.md': 'markdown',\n"}.items():
            template = template.replace('{{' + key + '}}', value)
        self.config = self.generated / 'conf.py'
        self.config.write_text(template)
        self.addCleanup(lambda: sys.modules.pop('ecosystem_documentation_boundary', None))
        original_path = sys.path[:]
        self.addCleanup(lambda: sys.path.__setitem__(slice(None), original_path))

    def load(self):
        return runpy.run_path(str(self.config))

    def test_conflicting_malformed_development_docs_have_no_influence(self):
        original = self.load()
        development = self.project / 'docs'
        development.mkdir()
        for name, text in {'index.md': '[bad](missing.md)', 'index.rst': '.. missing::',
                           'sphinx.json': '{', 'conf.py': "raise RuntimeError('must not run')"}.items():
            (development / name).write_text(text)
        polluted = self.load()
        for key in ['project', 'extensions', 'exclude_patterns', 'source_suffix', 'master_doc']:
            self.assertEqual(polluted[key], original[key])
        polluted['setup'](SimpleNamespace(srcdir=str(self.source)))
        with self.assertRaisesRegex(ValueError, 'source must be assets/docs'):
            polluted['setup'](SimpleNamespace(srcdir=str(development)))

    def test_missing_stable_entry_cannot_use_root_docs(self):
        (self.source / 'index.md').unlink()
        (self.project / 'docs').mkdir()
        (self.project / 'docs/index.md').write_text('# Cannot be a fallback\n')
        with self.assertRaisesRegex(ValueError, 'entry point is required'):
            self.load()

    def test_nested_escape_is_rejected_by_direct_sphinx_configuration(self):
        (self.project / 'docs').mkdir()
        (self.project / 'docs/planning.rst').write_text('Planning\n========\n')
        (self.source / 'index.md').unlink()
        (self.source / 'index.rst').write_text('.. include:: part.txt\n')
        (self.source / 'part.txt').write_text('.. include:: ../../docs/planning.rst\n')
        with self.assertRaisesRegex(ValueError, 'leaves documentation unit'):
            self.load()

    def test_disposable_product_has_no_root_docs_or_authored_configuration(self):
        config = self.load()
        config['setup'](SimpleNamespace(srcdir=str(self.source)))
        self.assertFalse((self.project / 'docs').exists())
        self.assertFalse((self.source / 'conf.py').exists())
        self.assertFalse((self.source / 'sphinx.json').exists())
        self.assertEqual(config['authored_source'], self.source)

    def test_symlink_source_is_rejected(self):
        original = self.project / 'outside'
        self.source.rename(original)
        self.source.symlink_to(original, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'symlink source'):
            self.load()

    def test_development_alias_is_not_an_alternative_source_root(self):
        development = self.project / 'docs'
        development.symlink_to(self.source, target_is_directory=True)
        config = self.load()
        with self.assertRaisesRegex(ValueError, 'source must be assets/docs'):
            config['setup'](SimpleNamespace(srcdir=str(development)))


if __name__ == "__main__":
    unittest.main()
