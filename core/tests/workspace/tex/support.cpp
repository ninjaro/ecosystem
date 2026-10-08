#include "../../suite/test_support.hpp"
#include "../tex.hpp"

namespace ecosystem_test_support {

void write_tex_tools(const fs::path& root) {
    const auto python = ecosystem::find_command_path("python3");
    require_true(!python.empty(), "TeX fixtures require Python");
    write_executable_script(root / "bin/latexmk", "#!" + python + R"py(
from pathlib import Path
import json
import os
import sys
if '-v' in sys.argv:
    print('Latexmk fixture 1.0')
    sys.exit(0)
project = Path.cwd().parent
with (project / 'tex_calls.jsonl').open('a') as log:
    log.write(json.dumps({'args': sys.argv[1:], 'cwd': str(Path.cwd())}) + '\n')
source = Path(sys.argv[-1])
mode = os.environ.get('MANIFESTO_TEX_FIXTURE', '')
if mode == 'failure' and source.name.startswith('b'):
    print('fixture TeX compilation failed')
    sys.exit(7)
output = Path(next(a.split('=', 1)[1] for a in sys.argv if a.startswith('-outdir=')))
pdf = output / (source.stem + '.pdf')
if mode == 'missing':
    sys.exit(0)
if mode == 'symlink':
    pdf.symlink_to(project / 'retained.pdf')
elif mode == 'hardlink':
    pdf.hardlink_to(project / 'retained.pdf')
elif mode == 'directory':
    pdf.mkdir()
elif mode == 'fifo':
    os.mkfifo(pdf)
else:
    pdf.write_bytes(b'wrong output' if mode == 'invalid' else b'%PDF-1.4\n' + source.read_bytes())
if mode == 'index-lock':
    lock = project / 'assets/showcase/.index.tsv.pending'
    lock.mkdir(exist_ok=True)
    (lock / 'keep').write_text('concurrent recovery')
if mode == 'metadata-error':
    (project / 'assets/showcase/readme.md').write_text('invalid paired readme')
if mode == 'output-obstruction' and source.name.startswith('b'):
    obstruction = project / 'assets/showcase/tex' / (source.stem + '.pdf')
    obstruction.mkdir()
    (obstruction / 'keep').write_text('Retained obstruction')
)py");
    write_executable_script(
        root / "bin/pdflatex", "#!" + python + "\nprint('pdfTeX fixture 1.0')\n"
    );
    write_executable_script(
        root / "bin/cmake",
        "#!" + python + "\nraise SystemExit('TeX must not configure C++')\n"
    );
}

} // namespace ecosystem_test_support
