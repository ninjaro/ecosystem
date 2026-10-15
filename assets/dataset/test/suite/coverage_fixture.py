"""Small synthetic LLVM-shaped evidence for transport/presentation unit tests."""
import io
import json
from pathlib import Path
import sys
import zipfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'templates/tooling'))
from coverage_site import metric, render, SCRIPT, STYLE


def fixture(root):
    root = Path(root)
    metrics = {name: metric({'count': count, 'covered': covered}) for name, count, covered in
               [('lines', 10, 7), ('functions', 2, 1), ('branches', 2, 1), ('regions', 4, 3)]}
    native = 'llvm/coverage/' + (root / 'src/main.cpp').as_posix().lstrip('/') + '.html'
    functions = [{'name': 'covered()', 'path': 'src/main.cpp', 'line': 1, 'count': 4, 'html': native + '#L1'},
                 {'name': 'uncovered()', 'path': 'src/main.cpp', 'line': 2, 'count': 0, 'html': native + '#L2'}]
    summary = {'status': 'available', 'totals': metrics, 'files': [{'path': 'src/main.cpp', 'html': native, 'metrics': metrics}],
               'functions': functions}
    report = {'type': 'llvm.coverage.json.export', 'version': '2.0.1',
              'data': [{'files': [{'filename': str(root / 'src/main.cpp'), 'summary': metrics}], 'totals': metrics}]}
    files = {'index.html': render(summary), 'summary.json': json.dumps(summary),
             'coverage.css': STYLE, 'coverage.js': SCRIPT, 'llvm/index.html': '<html>LLVM coverage index</html>',
             native: '<html><body><a id="L1">executed</a><a id="L2">unexecuted</a></body></html>'}
    archive = io.BytesIO()
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as output:
        for name, value in files.items():
            output.writestr(name, value)
    return json.dumps(report), archive.getvalue()


if __name__ == '__main__':
    root = Path(sys.argv[1])
    destination = root / '.ecosystem/reports'
    destination.mkdir(parents=True, exist_ok=True)
    report, archive = fixture(root)
    (destination / 'coverage.json').write_text(report)
    (destination / 'coverage.zip').write_bytes(archive)
