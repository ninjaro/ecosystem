'use strict';
// Run the generated cheap-Checks programs against real Git, Marx and Engels.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync, spawnSync} = require('node:child_process');
const {test} = require('node:test');
const {fixture, changes, engels} = require('./ci_change_fixtures.cjs');
const marx = path.join(path.dirname(engels), 'marx');
const workflow = fs.readFileSync(process.argv[4], 'utf8');

function program(step, key) {
  const marker = `      - name: ${step}\n`;
  assert.ok(workflow.includes(marker), `Missing generated step: ${step}`);
  const block = workflow.slice(workflow.indexOf(marker) + marker.length).split('\n      - ')[0];
  const match = block.match(new RegExp(`          ${key}: \\|\\n((?: {12}.*\\n|\\n)*)`));
  assert.ok(match && match[1], `Missing generated program: ${step}`);
  return match[1].split('\n').map(line => line.slice(12)).join('\n');
}
const resolveProgram = new Function('require', 'context', 'core', program('Resolve checked-out changes', 'script'));
const checksProgram = program('Run required local checks', 'shell-command')
  .replaceAll('${{ steps.manifesto.outputs.engels-binary }}', engels)
  .replaceAll('${{ steps.manifesto.outputs.marx-binary }}', marx);
assert.ok(!checksProgram.includes('${{'), 'Every generated expression must be bound');

for (const scenario of ['source', 'header', 'docs', 'pull-request', 'selected-drift', 'unknown', 'shallow', 'dirty', 'mismatched-event', 'repository-drift']) {
  test(`generated cheap Checks: ${scenario}`, t => {
    const f = fixture(t);
    const exec = (binary, args) => execFileSync(binary, args, {cwd: f.root,
      encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], timeout: 60000});
    exec(marx, ['sync']);
    const files = f.git('ls-files').split('\n').filter(name => /\.(cpp|hpp)$/.test(name));
    exec('clang-format', ['-i', '-style=file', ...files]);
    f.write('other/src/spare.cpp', 'int  spare( ){return 7;}\n');
    const base = f.commit('tracked surfaces and unrelated formatting drift');
    if (scenario === 'header') {
      f.write('code/include/api.hpp', '#pragma once\nint changed_answer();\n');
      exec('clang-format', ['-i', '-style=file', 'code/include/api.hpp']);
    } else if (scenario === 'docs') f.write('docs/guide.md', 'Updated guide\n');
    else {
      f.write('program/src/main.cpp', 'int  main( ){return 2;}\n');
      if (scenario !== 'selected-drift') exec('clang-format', ['-i', '-style=file', 'program/src/main.cpp']);
      if (scenario === 'unknown') f.write('settings.cfg', '# unknown build effect\n');
    }
    const head = f.commit('event changes');
    if (scenario === 'shallow') f.write('.git/shallow', head + '\n');
    const context = {eventName: 'push', sha: scenario === 'mismatched-event' ? base : head,
      ref: 'refs/heads/feature', payload: {before: base, after: head}};
    if (scenario === 'pull-request') {
      const merge = f.git('commit-tree', f.git('rev-parse', 'HEAD^{tree}'), '-p', base, '-p', head, '-m', 'PR test merge');
      f.git('checkout', '-q', '--detach', merge);
      Object.assign(context, {eventName: 'pull_request', sha: merge, ref: 'refs/pull/42/merge',
        payload: {pull_request: {base: {sha: base}, head: {sha: head}}}});
    }
    const outputs = {};
    resolveProgram(name => {
      assert.equal(name, './.github/actions/select-manifesto-ci/changes.cjs');
      return {collect: args => changes.collect({...args, root: f.root})};
    }, context, {setOutput: (key, value) => { outputs[key] = value; }, info: () => {}});
    const record = JSON.parse(outputs.changes);
    assert.equal(record.complete, !['shallow', 'mismatched-event'].includes(scenario));
    assert.deepEqual(JSON.parse(fs.readFileSync(outputs['changes-file'])), record);
    if (scenario === 'dirty') f.write('docs/guide.md', 'Modified after resolution\n');
    if (scenario === 'repository-drift') f.write('CMakeLists.txt', '# broken tracked facade\n');
    const before = f.git('status', '--porcelain');
    f.write('.ecosystem/cheap-checks.sh', checksProgram);
    const result = spawnSync('bash', ['-e', '-o', 'pipefail', '.ecosystem/cheap-checks.sh'], {
      cwd: f.root, env: {...process.env, CHECK_SCOPE: 'cheap'}, encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'pipe'], timeout: 60000,
    });
    assert.ifError(result.error);
    const output = result.stdout + result.stderr;
    assert.equal(result.status, ['source', 'header', 'docs', 'pull-request'].includes(scenario) ? 0 : 5, output);
    if (scenario === 'repository-drift') {
      assert.doesNotMatch(output, /repository check passed|format selection:/);
    } else {
      assert.match(output, /repository check passed/);
      const scope = scenario === 'docs' ? 'none'
        : ['source', 'header', 'pull-request', 'selected-drift'].includes(scenario) ? 'changed' : 'full';
      assert.ok(output.includes('format selection: ' + scope), output);
    }
    assert.equal(f.git('status', '--porcelain'), before, 'generated verification must not repair inputs');
    for (const name of ['source', 'build']) {
      const directory = path.join(f.root, '.ecosystem', name);
      assert.deepEqual(fs.existsSync(directory) ? fs.readdirSync(directory) : [], [],
        'cheap Checks must not configure or build the project');
    }
    assert.equal(fs.existsSync(path.join(f.root, '.ecosystem/github/evidence')), false, 'cheap Checks cannot issue a full receipt');
  });
}
