'use strict';
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const {execFileSync} = require('node:child_process');
const changes = require(path.resolve(process.argv[2]));
const engels = path.resolve(process.argv[3]);

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'manifesto-ci-policy-'));
  t.after(() => fs.rmSync(root, {recursive: true, force: true}));
  const git = (...args) => execFileSync('git', ['-C', root, '-c', 'commit.gpgsign=false', ...args],
    {encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe']}).trim();
  const write = (name, data) => {
    fs.mkdirSync(path.dirname(path.join(root, name)), {recursive: true});
    fs.writeFileSync(path.join(root, name), data);
  };
  const manifest = {id: 'fixture', description: 'Change policy fixture.', facade: 'app:main', artifacts: [
    {id: 'lib:base', kind: 'static_lib', root: 'code', owns: ['api']},
    {id: 'lib:wrapper', kind: 'static_lib', root: 'code', owns: ['wrapper'], dependencies: ['lib:base']},
    {id: 'app:main', kind: 'exe', root: 'program', owns: [], entry: 'src/main.cpp', dependencies: ['lib:wrapper']},
    {id: 'other:spare', kind: 'static_lib', root: 'other', owns: ['spare']},
  ]};
  write('manifest.json', JSON.stringify(manifest));
  write('.gitignore', '.ecosystem/\n');
  write('code/include/api.hpp', '#pragma once\nint answer();\n');
  write('code/src/api.cpp', '#include <api.hpp>\nint answer() { return 42; }\n');
  write('code/src/wrapper.cpp', '#include <api.hpp>\nint wrapper() { return answer(); }\n');
  write('program/src/main.cpp', 'int main() { return 0; }\n');
  write('other/src/spare.cpp', 'int spare() { return 7; }\n');
  write('docs/guide.md', '# Guide\n');
  git('init', '-q'); git('config', 'user.name', 'Fixture'); git('config', 'user.email', 'fixture@example.invalid');
  const commit = message => { git('add', '-A'); git('commit', '-qm', message); return git('rev-parse', 'HEAD'); };
  const base = commit('base');
  const record = (before = base) => {
    const head = git('rev-parse', 'HEAD');
    return changes.resolve({root, context: {eventName: 'push', sha: head,
      ref: 'refs/heads/main', payload: {before, after: head}}});
  };
  const readReport = () => JSON.parse(execFileSync(engels, ['report', 'changes', '--json'],
    {cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe']}));
  const report = (input = record()) => {
    write('.ecosystem/github/changes.json', JSON.stringify(input) + '\n');
    return readReport();
  };
  return {root, git, write, manifest, commit, base, record, report, readReport};
}
module.exports = {fixture, changes, engels};
