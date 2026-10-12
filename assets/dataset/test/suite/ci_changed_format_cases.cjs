'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync, spawnSync} = require('node:child_process');
const {test} = require('node:test');
const {fixture, engels} = require('./ci_change_fixtures.cjs');

const formatter = execFileSync('sh', ['-c', 'command -v clang-format'], {encoding: 'utf8'}).trim();
const formatted = source => execFileSync(formatter, ['-style=LLVM'], {input: source, encoding: 'utf8'});
function formatFixture(t) {
  const f = fixture(t);
  f.write('.clang-format', 'BasedOnStyle: LLVM\n');
  f.write('other/src/spare.cpp', 'int  spare( ){return 7;}\n');
  const base = f.commit('format baseline with unrelated drift');
  const log = path.join(f.root, '.ecosystem/formatter-args.jsonl');
  const bin = path.join(f.root, '.ecosystem/test-bin');
  f.write('.ecosystem/test-bin/clang-format', `#!${process.execPath}
const fs = require('node:fs');
const {spawnSync} = require('node:child_process');
fs.appendFileSync(process.env.FORMAT_ARGS_LOG, JSON.stringify(process.argv.slice(2)) + '\\n');
const result = spawnSync(process.env.REAL_FORMATTER, process.argv.slice(2), {stdio: 'inherit'});
process.exit(result.status === null ? 1 : result.status);
`);
  fs.chmodSync(path.join(bin, 'clang-format'), 0o755);
  const env = {...process.env, PATH: bin + path.delimiter + process.env.PATH,
    REAL_FORMATTER: formatter, FORMAT_ARGS_LOG: log};
  const run = (args = ['check', 'format', '--changes'], cwd = f.root) =>
    spawnSync(engels, args, {cwd, env, encoding: 'utf8'});
  const calls = () => fs.existsSync(log) ? fs.readFileSync(log, 'utf8').trim().split('\n').map(JSON.parse) : [];
  const save = input => f.write('.ecosystem/github/changes.json', JSON.stringify(input));
  const edit = (source = formatted('int main(){ return 2; }')) => {
    f.write('program/src/main.cpp', source); f.commit('selected edit'); save(f.record(base));
  };
  return {...f, base, log, run, calls, save, edit};
}

test('one changed C++ file is the only input to the real formatter; full verification still sees unrelated drift', t => {
  const f = formatFixture(t);
  f.edit();
  const before = f.git('status', '--porcelain');
  const result = f.run();
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.match(result.stdout, /format selection: changed/);
  const invocations = f.calls().filter(args => args[0] !== '--version');
  assert.equal(invocations.length, 1);
  assert.deepEqual(invocations[0], ['--dry-run', '--Werror', '-style=file', path.join(f.root, 'program/src/main.cpp')]);
  assert.equal(f.git('status', '--porcelain'), before, 'verification must not repair files');
  const full = f.run(['check', 'format']);
  assert.equal(full.status, 5, full.stdout + full.stderr);
  assert.match(full.stderr, /formatting drift/);
  assert.equal(fs.readFileSync(path.join(f.root, 'other/src/spare.cpp'), 'utf8'), 'int  spare( ){return 7;}\n');
});

test('changed-file verification preserves native formatting failures and source contents', t => {
  const f = formatFixture(t);
  const source = 'int  main( ){return 2;}\n';
  f.edit(source);
  const result = f.run();
  assert.equal(result.status, 5, result.stdout + result.stderr);
  assert.match(result.stdout + result.stderr, /code should be clang-formatted/);
  assert.match(result.stdout, /format selection: changed/);
  assert.equal(fs.readFileSync(path.join(f.root, 'program/src/main.cpp'), 'utf8'), source);
});

test('an explicitly disabled change flag retains full verification', t => {
  const f = formatFixture(t);
  f.edit();
  const result = f.run(['check', 'format', '--changes=false']);
  assert.equal(result.status, 5, result.stdout + result.stderr);
  assert.doesNotMatch(result.stdout, /format selection: changed/);
  assert.ok(f.calls().find(args => args.includes('--Werror')).includes(path.join(f.root, 'other/src/spare.cpp')));
});

for (const edit of [false, true]) test(`${edit ? 'presentation-only' : 'empty'} changes never invoke the formatter`, t => {
  const f = formatFixture(t);
  if (edit) { f.write('docs/guide.md', 'Changed prose\n'); f.commit('docs'); }
  f.save(f.record(f.base));
  const result = f.run();
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.match(result.stdout, /format selection: none\nformat: no files/);
  assert.deepEqual(f.calls(), [], 'even the formatter version probe is unnecessary');
  assert.equal(fs.existsSync(path.join(f.root, '.ecosystem/build')), false);
});

for (const failure of ['missing', 'incomplete', 'unknown', 'format-policy', 'stale', 'tampered', 'dirty']) {
  test(`${failure} change information falls back to the full owned formatting set`, t => {
    const f = formatFixture(t);
    f.edit();
    let input = f.record(f.base);
    if (failure === 'missing') fs.unlinkSync(path.join(f.root, '.ecosystem/github/changes.json'));
    if (failure === 'incomplete') { input.complete = false; f.save(input); }
    if (failure === 'unknown' || failure === 'format-policy') {
      f.write(failure === 'unknown' ? 'scripts/build.py' : '.clang-format', failure === 'unknown' ? '# build\n' : 'BasedOnStyle: LLVM\nColumnLimit: 70\n');
      f.commit('global change'); f.save(f.record(f.base));
    }
    if (failure === 'stale') { f.write('docs/guide.md', 'new revision\n'); f.commit('advance'); }
    if (failure === 'tampered') { input.paths = []; input.changes = []; f.save(input); }
    if (failure === 'dirty') f.write('docs/guide.md', 'uncommitted\n');
    const result = f.run();
    assert.equal(result.status, 5, result.stdout + result.stderr);
    assert.match(result.stdout, /format selection: full/);
    const args = f.calls().find(call => call.includes('--Werror'));
    assert.ok(args.includes(path.join(f.root, 'other/src/spare.cpp')));
    assert.ok(args.includes(path.join(f.root, 'code/include/api.hpp')));
    assert.ok(args.includes(path.join(f.root, 'program/src/main.cpp')));
  });
}

test('changed headers retain independent formatter scope', t => {
  const f = formatFixture(t);
  f.write('code/include/api.hpp', formatted('#pragma once\nint changed_answer();\n'));
  f.commit('header'); f.save(f.record(f.base));
  const result = f.run();
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.deepEqual(f.calls().find(args => args.includes('--Werror')).slice(3), [path.join(f.root, 'code/include/api.hpp')]);
});

test('unsupported profiles, artifact filters, workspace scope and duplicate flags fail before tools run', t => {
  const f = formatFixture(t);
  for (const args of [['check', 'ci', '--changes'], ['check', 'format', 'lib:base', '--changes'],
    ['check', 'format', '--changes', '--changes'], ['check', 'format', '--changes=invalid']]) assert.equal(f.run(args).status, 2);
  const workspace = fs.mkdtempSync(path.join(os.tmpdir(), 'manifesto-format-workspace-'));
  t.after(() => fs.rmSync(workspace, {recursive: true, force: true}));
  f.git('clone', '-q', f.root, path.join(workspace, 'project'));
  assert.equal(f.run(['check', 'format', '--changes'], workspace).status, 2);
  assert.deepEqual(f.calls(), []);
});
