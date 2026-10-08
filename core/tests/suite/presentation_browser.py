"""Exercise carousel behavior in Chromium; run explicitly beside native Sphinx tests."""
import csv
import importlib.util
from importlib.machinery import SourceFileLoader
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from presentation.media import image, pdf, video
module = Path(sys.argv[1]).absolute()
sys.path.insert(0, str(module.parent))
loader = SourceFileLoader('manifesto_presentation', str(module))
site = importlib.util.module_from_spec(importlib.util.spec_from_loader(loader.name, loader))
loader.exec_module(site)
browser = sys.argv[2] if len(sys.argv) > 2 else 'chromium'

with tempfile.TemporaryDirectory(prefix='manifesto-carousel-browser-') as temporary:
    root = Path(temporary)
    media = root / 'assets/showcase'
    media.mkdir(parents=True)
    image(media / 'frame.png')
    pdf(media / 'brief.pdf')
    video(media / 'demo.webm')
    (root / 'manifest.json').write_text(json.dumps({'id': 'browser', 'artifacts': []}))
    (root / '.ecosystem/doxygen/html').mkdir(parents=True)
    (root / '.ecosystem/doxygen/html/index.html').write_text('API')
    with (media / 'index.tsv').open('w', newline='') as stream:
        writer = csv.writer(stream, delimiter='\t')
        writer.writerow(['id', 'path', 'type', 'description', 'datetime'])
        writer.writerows([['still', 'frame.png', 'image', 'Still', '2026'],
                         ['clip', 'demo.webm', 'video', 'Clip', '2025'],
                         ['brief', 'brief.pdf', 'pdf', 'Brief', '2024']])
    body, files, _ = site.compose(root)
    output = root / 'site'
    output.mkdir()
    native = len(sys.argv) > 4
    if native:
        conf = root / '.ecosystem/sphinx'
        conf.mkdir()
        shutil.copyfile(module, conf / 'manifesto_presentation.py')
        shutil.copytree(module.parent / 'sphinx_presentation', conf / 'sphinx_presentation')
        (conf / 'conf.py').write_text('import sys\nsys.path.insert(0, ' + repr(str(conf)) + ')\n'
                                    'project="Browser"\nextensions=["manifesto_presentation"]\n'
                                    'html_theme=' + repr(sys.argv[4]) + '\n')
        (root / 'docs').mkdir()
        (root / 'docs/index.rst').write_text('Browser fixture\n===============\n')
        subprocess.run([sys.executable, '-m', 'sphinx', '-b', 'html', '-E', '-a', '-W', '-c',
                        str(conf), str(root / 'docs'), str(output)], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    else:
        for name, data in files.items():
            file = output / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(data.read_bytes() if isinstance(data, Path) else data)
    harness = r'''<script id="carousel-test">
window.addEventListener('load', async () => {
    const result = document.createElement('output');
    result.id = 'browser-result';
    const check = (value, message) => { if (!value) throw Error(message); };
    const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
    try {
        const carousel = document.querySelector('.manifesto-carousel');
        const track = carousel.querySelector('.carousel-track');
        const controls = carousel.querySelector('.carousel-controls');
        const previous = controls.querySelector('[data-step="-1"]');
        const next = controls.querySelector('[data-step="1"]');
        const status = carousel.querySelector('.carousel-status');
        const key = name => track.dispatchEvent(new KeyboardEvent('keydown', {key: name, bubbles: true}));
        check(!controls.hidden && status.textContent === '1 / 3' && previous.disabled, 'initial state');
        check(track.clientWidth <= innerWidth && track.scrollWidth > track.clientWidth, 'responsive viewport');
        next.click();
        check(status.textContent === '2 / 3' && !previous.disabled, 'next button');
        const player = carousel.querySelector('video');
        check(player.controls && !player.autoplay, 'explicit video playback');
        const preview = carousel.querySelector('img[alt="First page: Brief"]');
        check(player.poster && preview && preview.complete && preview.naturalWidth > 0, 'native media previews');
        player.muted = true;
        await player.play();
        key('End');
        check(status.textContent === '3 / 3' && next.disabled && player.paused, 'end pauses departed video');
        key('Home');
        next.click(); next.click();
        await wait(500);
        check(status.textContent === '3 / 3', 'rapid navigation reaches the final slide');
        check(Math.abs(track.scrollLeft - (track.scrollWidth - track.clientWidth)) < 6,
              'final slide is visible: ' + track.scrollLeft + '/' + track.scrollWidth + '/' + track.clientWidth);
        carousel.style.width = '240px';
        await wait(100);
        check(status.textContent === '3 / 3' && track.clientWidth <= 240, 'resize keeps selected slide');
        key('ArrowLeft');
        check(status.textContent === '2 / 3', 'keyboard navigation');
        result.textContent = 'PASS';
    } catch (error) { result.textContent = 'FAIL: ' + error.message; }
    document.body.append(result);
});
</script>'''
    page = output / '_manifesto/index.html' if native else output / 'index.html'
    if native:
        page.write_text(page.read_text().replace('</body>', harness + '</body>'))
    else:
        page.write_text('<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width">'
                        '<style>body {margin: 8px;} main {max-width: 900px;}</style><main>'
                        + body + '</main>' + harness)
    base = [browser, '--headless=new', '--no-sandbox', '--disable-gpu', '--disable-dev-shm-usage',
            '--allow-file-access-from-files', '--no-first-run', '--no-default-browser-check',
            '--dump-dom', '--virtual-time-budget=2500']
    for width, reduced in ((1000, False), (320, True)):
        command = base + [f'--user-data-dir={root / ("browser-" + str(width))}', f'--window-size={width},900']
        if reduced:
            command.append('--force-prefers-reduced-motion')
        result = subprocess.run(command + [page.as_uri()], capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        assert '<output id="browser-result">PASS</output>' in result.stdout, result.stdout[-2500:]
        print('PASS carousel controls, playback, keyboard, resize:', width, 'reduced:', reduced)
    screenshot = Path(sys.argv[3]) if len(sys.argv) > 3 else root / 'no-js.png'
    source = page.read_text().split('<script id="carousel-test">')[0]
    assert 'carousel-ready' not in source and source.count('class="carousel-slide"') == 3
    # Omit application scripts; Chromium's own headless DOM/screenshot machinery still needs its engine.
    page.write_text(source.replace('<script defer src="carousel.js"></script>', ''))
    result = subprocess.run(base + [f'--user-data-dir={root / "browser-no-js"}',
                               '--window-size=1000,1600',
                               '--screenshot=' + str(screenshot), page.as_uri()],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    assert 'carousel-ready' not in result.stdout and result.stdout.count('class="carousel-slide"') == 3
    assert screenshot.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'), 'native fallback render must succeed'
    print('PASS ordered list without JavaScript')
