from pathlib import Path
import struct
import subprocess
from unittest.mock import patch

from .fixtures import PresentationCase, preview_converter
from .media import image, pdf
from sphinx_presentation.inputs import Inputs
from sphinx_presentation import previews


class PreviewTests(PresentationCase):
    def setUp(self):
        super().setUp()
        self.converter = preview_converter(self)
        self.inputs = Inputs(self.root)
        self.file = self.root / 'brief #.pdf'
        pdf(self.file)

    def test_preview_snapshot_and_reuse(self):
        data = self.file.read_bytes()
        def convert(args, **kwargs):
            source = Path(args[-2])
            self.assertTrue(source.is_relative_to(self.root / '.ecosystem/tmp/previews'))
            self.assertNotEqual(source, self.file)
            self.assertEqual(source.read_bytes(), data)
            image(Path(args[-1]).with_suffix('.png'))
            return subprocess.CompletedProcess(args, 0, b'')
        self.converter.side_effect = convert
        name = previews.preview(self.inputs, 'pdf', self.file.name, data)
        self.assertTrue(self.inputs.files[name].startswith(b'\x89PNG'))
        self.assertEqual(self.file.read_bytes(), data)
        self.assertEqual(previews.preview(self.inputs, 'pdf', 'copy.pdf', data), name)
        self.assertEqual(self.converter.call_count, 1)

    def test_preview_workspace_rejects_symlink_parent(self):
        outside = self.root / 'unrelated'
        outside.mkdir()
        (self.root / '.ecosystem/tmp').symlink_to(outside)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            previews.preview(self.inputs, 'pdf', self.file.name, self.file.read_bytes())
        self.assertFalse(self.converter.called)
        self.assertEqual(list(outside.iterdir()), [])

    def test_converter_failures(self):
        for result in (subprocess.CompletedProcess([], 4, b'converter failed'),
                       subprocess.TimeoutExpired('converter', 30), OSError('cannot launch')):
            with self.subTest(result=result):
                self.converter.side_effect = result if isinstance(result, Exception) else None
                self.converter.return_value = result
                with self.assertRaisesRegex(ValueError, 'preview failed.*brief #.pdf'):
                    previews.preview(self.inputs, 'pdf', self.file.name, self.file.read_bytes())
                self.assertEqual(self.inputs.files, {})

    def test_missing_tool(self):
        with patch('sphinx_presentation.previews.shutil.which', return_value=None):
            with self.assertRaisesRegex(ValueError, 'pdftoppm is required'):
                previews.preview(self.inputs, 'pdf', self.file.name, self.file.read_bytes())
            with self.assertRaisesRegex(ValueError, 'ffmpeg is required'):
                previews.preview(self.inputs, 'video', 'clip.webm', b'video')
        self.assertFalse(self.converter.called)

    def test_invalid_preview_outputs(self):
        for mode in ('missing', 'garbage', 'symlink', 'huge', 'dimensions'):
            with self.subTest(mode=mode):
                def convert(args, **kwargs):
                    output = Path(args[-1]).with_suffix('.png')
                    if mode == 'garbage':
                        output.write_bytes(b'not a PNG')
                    elif mode == 'symlink':
                        output.symlink_to(self.file)
                    elif mode == 'huge':
                        with output.open('wb') as stream:
                            stream.truncate(previews.PNG_LIMIT + 1)
                    elif mode == 'dimensions':
                        image(output)
                        data = output.read_bytes()
                        output.write_bytes(data[:16] + struct.pack('>II', 9999, 120) + data[24:])
                    return subprocess.CompletedProcess(args, 0, b'')
                self.converter.side_effect = convert
                with self.assertRaises(ValueError):
                    previews.preview(self.inputs, 'pdf', self.file.name, self.file.read_bytes())
                self.assertEqual(self.inputs.files, {})

    def test_preview_budget(self):
        with patch('sphinx_presentation.inputs.LIMIT', 1):
            with self.assertRaisesRegex(ValueError, 'exceed'):
                previews.preview(self.inputs, 'pdf', self.file.name, self.file.read_bytes())
        self.assertEqual(self.inputs.files, {})
