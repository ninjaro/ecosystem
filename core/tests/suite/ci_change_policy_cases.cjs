'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const {test} = require('node:test');
const {fixture, changes, engels} = require('./ci_change_fixtures.cjs');

const closure = ['app:main', 'lib:base', 'lib:wrapper'];
const all = [...closure, 'other:spare'];
function full(report, validated = true) {
  assert.equal(report.delta_validated, validated, report.reason);
  assert.equal(report.classification, 'full');
  assert.deepEqual(report.affected_artifacts, all);
  assert.equal(report.format.scope, 'full');
  for (const check of ['repository', 'build', 'tests', 'coverage', 'codeql']) assert.equal(report.checks[check], 'full');
}

for (const name of ['code/src/api.cpp', 'code/include/api.hpp', 'code/tests/api_tests.cpp']) {
  test(`owned ${name} selects its transitive dependents and only the changed formatter input`, t => {
    const f = fixture(t);
    f.write(name, '// changed source\n'); f.commit('source change');
    const report = f.report();
    assert.equal(report.delta_validated, true, report.reason);
    assert.equal(report.classification, 'affected');
    assert.deepEqual(report.affected_artifacts, closure);
    assert.deepEqual(report.affected_projects, ['fixture']);
    assert.deepEqual(report.paths[0].owners, ['lib:base']);
    assert.deepEqual(report.format, {scope: 'changed', files: [name]});
    assert.equal(report.checks.build, 'affected');
    assert.equal(report.checks.tests, 'affected');
    assert.equal(report.checks.codeql, 'full');
    assert.equal(report.checks.coverage, 'full');
    assert.equal(report.checks.repository, 'full');
    assert.equal(report.comparison.manifest_blob, f.git('rev-parse', 'HEAD:manifest.json'));
    assert.equal(f.git('status', '--porcelain'), '', 'report must preserve tracked inputs');
    assert.equal(fs.existsSync(path.join(f.root, '.ecosystem/build')), false, 'report must not configure/build');
  });
}

test('an entry source belongs only to its executable, without reversing dependency direction', t => {
  const f = fixture(t);
  f.write('program/src/main.cpp', 'int main() { return 1; }\n'); f.commit('entry');
  const report = f.report();
  assert.equal(report.classification, 'affected');
  assert.deepEqual(report.affected_artifacts, ['app:main']);
});

for (const name of ['docs/guide.md', 'docs/subdirectory/guide.rst', 'README.md']) {
  test(`presentation-only ${name} has no C++ relevance but retains full repository/coverage policy`, t => {
    const f = fixture(t);
    f.write(name, 'Updated documentation\n'); f.commit('docs');
    const report = f.report();
    assert.equal(report.classification, 'presentation', report.reason);
    assert.deepEqual(report.affected_artifacts, []);
    assert.deepEqual(report.format, {scope: 'none', files: []});
    assert.equal(report.checks.build, 'none');
    assert.equal(report.checks.codeql, 'none');
    assert.equal(report.checks.presentation, true);
    assert.equal(report.checks.repository, 'full');
    assert.equal(report.checks.coverage, 'full');
  });
}

for (const [name, kind] of [['manifest.json', 'manifest'], ['templates/generator.tpl', 'build-policy'],
  ['code/.clang-format', 'build-policy'], ['cmake/toolchain.cmake', 'build-policy'],
  ['.github/workflows/tests.yml', 'workflow'], ['assets/instructions.md', 'assets'],
  ['java/gradlew', 'platform-input'], ['scripts/build.py', 'unknown'],
  ['code/src/api_extra.cpp', 'unowned-cxx'], ['docs/example.cpp', 'unowned-cxx']]) {
  test(`${name} forces conservative checks (${kind})`, t => {
    const f = fixture(t);
    f.write(name, name === 'manifest.json' ? JSON.stringify({...f.manifest, description: 'edited'}) : 'input\n');
    f.commit('input');
    const report = f.report();
    full(report);
    assert.equal(report.paths[0].kind, kind);
  });
}

test('an owned Markdown file under docs is a build input', t => {
  const f = fixture(t);
  f.manifest.artifacts[3].root = 'docs';
  f.manifest.artifacts[3].owns = ['include'];
  f.write('manifest.json', JSON.stringify(f.manifest));
  f.write('docs/include/readme.md', 'old\n');
  const base = f.commit('own docs input');
  f.write('docs/include/readme.md', 'new\n'); f.commit('edit owned input');
  const report = f.report(f.record(base));
  assert.equal(report.classification, 'affected');
  assert.equal(report.paths[0].kind, 'owned-input');
  assert.deepEqual(report.affected_artifacts, ['other:spare']);
  assert.equal(report.checks.codeql, 'full');
});

test('shared runtime assets retain their global effect even inside an artifact ownership scope', t => {
  const f = fixture(t);
  f.manifest.artifacts[3].root = 'assets';
  f.manifest.artifacts[3].owns = ['include'];
  f.write('manifest.json', JSON.stringify(f.manifest));
  f.write('assets/include/readme.md', 'old\n');
  const base = f.commit('own assets input');
  f.write('assets/include/readme.md', 'new\n'); f.commit('edit shared asset');
  const report = f.report(f.record(base));
  full(report);
  assert.equal(report.paths[0].kind, 'assets');
  assert.deepEqual(report.paths[0].owners, ['app:main', 'other:spare']);
});

test('a documentation filename reached through an owned C++ symlink retains code relevance', t => {
  const f = fixture(t);
  f.write('docs/data.md', '#pragma once\nint answer();\n');
  fs.unlinkSync(path.join(f.root, 'code/include/api.hpp'));
  fs.symlinkSync('../../docs/data.md', path.join(f.root, 'code/include/api.hpp'));
  const base = f.commit('alias header');
  f.write('docs/data.md', '#pragma once\nint changed_answer();\n'); f.commit('edit aliased header');
  const report = f.report(f.record(base));
  assert.equal(report.classification, 'affected', report.reason);
  assert.deepEqual(report.affected_artifacts, closure);
  assert.deepEqual(report.format.files, ['code/include/api.hpp']);
  assert.equal(report.paths[0].kind, 'owned-input');
});

for (const name of ['code/src/api.cpp', 'docs/guide.md']) test(`deletion of ${name} remains conservative`, t => {
  const f = fixture(t);
  f.git('rm', name); f.commit('delete');
  const report = f.report();
  full(report);
  assert.equal(report.paths[0].status, 'D');
  if (name.endsWith('.cpp')) assert.deepEqual(report.paths[0].owners, ['lib:base']);
});

test('rename keeps both identities and cannot hide source behind a documentation extension', t => {
  const f = fixture(t);
  f.git('mv', 'code/src/api.cpp', 'docs/source.md'); f.commit('rename');
  const input = f.record();
  assert.equal(input.changes[0].status, 'R');
  const report = f.report(input);
  full(report);
  assert.deepEqual(report.paths.map(p => p.path), ['code/src/api.cpp', 'docs/source.md']);
});

test('file type changes and symlink additions under docs never become presentation-only', t => {
  const f = fixture(t);
  fs.unlinkSync(path.join(f.root, 'docs/guide.md'));
  fs.symlinkSync('../code/include/api.hpp', path.join(f.root, 'docs/guide.md'));
  f.commit('type change');
  full(f.report());
});

test('a Git submodule with a documentation suffix is not a documentation file', t => {
  const f = fixture(t);
  fs.mkdirSync(path.join(f.root, 'docs/module.md'));
  f.git('update-index', '--add', '--cacheinfo', '160000,' + f.base + ',docs/module.md');
  f.git('commit', '-qm', 'submodule');
  full(f.report());
});

test('two names sharing a namespace retain independent artifact identities', t => {
  const f = fixture(t);
  f.write('code/src/wrapper.cpp', '// wrapper edit\n'); f.commit('wrapper');
  const report = f.report();
  assert.deepEqual(report.paths[0].owners, ['lib:wrapper']);
  assert.deepEqual(report.affected_artifacts, ['app:main', 'lib:wrapper']);
});

test('changes report rejects artifact filters', t => {
  const f = fixture(t);
  assert.throws(() => execFileSync(engels, ['report', 'changes', 'lib:base'],
    {cwd: f.root, stdio: ['ignore', 'pipe', 'pipe']}), /artifact filters/);
});

test('an empty established delta is explicit', t => {
  const f = fixture(t);
  const report = f.report();
  assert.equal(report.classification, 'none');
  assert.deepEqual(report.paths, []);
  assert.deepEqual(report.affected_projects, []);
  assert.equal(report.checks.coverage, 'full');
});

test('PR classification binds the tested merge and compares only the head branch delta', t => {
  const f = fixture(t);
  f.write('docs/guide.md', 'PR docs\n'); const head = f.commit('PR');
  f.git('checkout', '-q', '--detach', f.base);
  f.write('code/src/api.cpp', '// base branch edit\n'); const base = f.commit('advanced base');
  f.git('merge', '--no-ff', '-qm', 'test merge', head);
  const checkout = f.git('rev-parse', 'HEAD');
  const input = changes.resolve({root: f.root, context: {eventName: 'pull_request', sha: checkout,
    payload: {pull_request: {base: {sha: base}, head: {sha: head}}}}});
  const report = f.report(input);
  assert.equal(report.classification, 'presentation', report.reason);
  assert.equal(report.comparison.checkout, checkout);
  assert.equal(report.comparison.comparison_base, f.base);
});

for (const tamper of ['omit', 'status', 'paths', 'duplicate', 'traversal', 'checkout', 'base', 'schema', 'incomplete']) {
  test(`a ${tamper} change record cannot authorize a narrower plan`, t => {
    const f = fixture(t);
    f.write('code/src/api.cpp', '// edit\n'); f.commit('code');
    const input = f.record();
    if (tamper === 'omit') { input.changes = []; input.paths = []; }
    if (tamper === 'status') input.changes[0].status = 'A';
    if (tamper === 'paths') input.paths = ['docs/guide.md'];
    if (tamper === 'duplicate') input.changes.push(input.changes[0]);
    if (tamper === 'traversal') { input.changes[0].path = '../outside.md'; input.paths = ['../outside.md']; }
    if (tamper === 'checkout') input.checkout = f.base;
    if (tamper === 'base') input.comparison_base = input.head;
    if (tamper === 'schema') input.schema = 99;
    if (tamper === 'incomplete') input.complete = false;
    full(f.report(input), false);
  });
}

test('missing, malformed, stale and dirty records remain full without build side effects', t => {
  const f = fixture(t);
  full(f.readReport(), false);
  f.write('.ecosystem/github/changes.json', '{');
  full(f.readReport(), false);
  const input = f.record();
  f.write('docs/guide.md', 'dirty\n');
  full(f.report(input), false);
  f.commit('new checkout');
  full(f.report(input), false);
  assert.equal(fs.existsSync(path.join(f.root, '.ecosystem/build')), false);
});

test('path whitespace and Unicode remain structured, not shell arguments', t => {
  const f = fixture(t);
  const name = 'docs/line\n日本語 "quote.md';
  f.write(name, 'docs\n'); f.commit('unusual path');
  const report = f.report();
  assert.equal(report.classification, 'presentation', report.reason);
  assert.equal(report.paths[0].path, name);
});
