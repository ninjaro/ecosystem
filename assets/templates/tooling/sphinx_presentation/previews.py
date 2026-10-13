"""Generate bounded first-page/frame previews from the selected input snapshot."""
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from .inputs import require, safe_path

PNG_LIMIT = 8 * 1024 * 1024


def preview(inputs, kind, name, data):
    destination = 'previews/' + kind + '-' + hashlib.sha256(data).hexdigest() + '.png'
    if destination in inputs.files:
        return destination
    tool = 'pdftoppm' if kind == 'pdf' else 'ffmpeg'
    command = shutil.which(tool)
    require(command, tool + ' is required for preview: ' + name)
    temporary_parent = safe_path(inputs.root, '.ecosystem/tmp/previews')
    temporary_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='manifesto-preview-', dir=temporary_parent) as temporary:
        root = Path(temporary)
        source = root / ('input' + Path(name).suffix.lower())
        source.write_bytes(data)
        output = root / 'preview.png'
        if kind == 'pdf':
            args = [command, '-f', '1', '-l', '1', '-singlefile', '-scale-to', '960',
                    '-png', str(source), str(output.with_suffix(''))]
        else:
            format_name = 'matroska' if source.suffix == '.webm' else 'mov'
            args = [command, '-nostdin', '-hide_banner', '-loglevel', 'error', '-threads', '1',
                    '-protocol_whitelist', 'file,pipe', '-f', format_name]
            if format_name == 'mov':
                args += ['-enable_drefs', '0']
            args += ['-i', str(source), '-an', '-frames:v', '1', '-vf',
                    'scale=960:720:force_original_aspect_ratio=decrease',
                    '-threads', '1', '-y', str(output)]
        try:
            result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        except (OSError, subprocess.TimeoutExpired) as error:
            raise ValueError('Project presentation: preview failed for ' + name + ': ' + str(error)) from error
        require(result.returncode == 0, 'preview failed for ' + name + ': '
                + result.stdout.decode('utf-8', errors='replace')[-4000:])
        require(output.is_file() and not output.is_symlink(), 'preview output is missing: ' + name)
        require(output.stat().st_size <= PNG_LIMIT, 'preview exceeds 8 MiB: ' + name)
        png = output.read_bytes()
        require(len(png) >= 33 and png.startswith(b'\x89PNG\r\n\x1a\n') and png[12:16] == b'IHDR',
                'invalid PNG preview: ' + name)
        width, height = struct.unpack('>II', png[16:24])
        require(0 < width <= 960 and 0 < height <= 960, 'invalid preview dimensions: ' + name)
        inputs.generated(destination, png)
    return destination
