'use strict';
// Exercise generated CodeQL gates with real native policy and repository checks.
// CodeQL action outcomes are simulated; no remote scanner is invoked here.
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync, spawnSync} = require('node:child_process');
const evidence = require(path.join(path.dirname(path.resolve(process.argv[2])), 'evidence.cjs'));
const {fixture, changes, engels} = require('./ci_change_fixtures.cjs');
const marx = path.join(path.dirname(engels), 'marx');
const workflow = fs.readFileSync(process.argv[5], 'utf8');

function block(marker) {
  assert.ok(workflow.includes(marker), marker);
  return workflow.slice(workflow.indexOf(marker) + marker.length).split('\n      - ')[0];
}
function program(name, key, indent = 10) {
  const step = block(`      - name: ${name}\n`);
  const match = step.match(new RegExp(` {${indent}}${key}: \\|\\n((?: {${indent + 2}}.*\\n|\\n)*)`));
  assert.ok(match, name);
  return match[1].split('\n').map(line => line.slice(indent + 2)).join('\n');
}
function eligible(marker, steps) {
  const expression = block(marker).match(/        if: \$\{\{ (.*) \}\}/)[1];
  return new Function('steps', `return (${expression});`)(steps);
}
const gate = program('Enforce CodeQL result', 'run', 8);
const summary = program('Summarize CodeQL analysis', 'run', 8);
const repository = program('Verify generated tracked surfaces', 'shell-command')
  .replaceAll('${{ steps.ecosystem.outputs.engels-binary }}', engels);
const classify = new Function('require', 'context', 'core', 'process', program('Classify checked-out changes', 'script'));
const resolve = new Function('require', 'context', 'core', program('Resolve checked-out changes', 'script'));
function shell(f, script, env) {
  const result = spawnSync('bash', ['-e', '-o', 'pipefail', '-c', script], {cwd: f.root,
    env: {...process.env, ...env, GITHUB_STEP_SUMMARY: path.join(f.root, '.ecosystem/step-summary')},
    encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], timeout: 60000});
  assert.ifError(result.error);
  return result;
}

for (const scenario of ['docs', 'empty', 'source', 'header', 'unknown', 'delete', 'dirty',
  'shallow', 'push', 'manual', 'schedule', 'repo-drift']) {
  test(`CodeQL workflow execution: ${scenario}`, t => {
    const f = fixture(t);
    execFileSync(marx, ['sync'], {cwd: f.root, stdio: 'pipe'});
    const base = f.commit('generated surfaces');
    if (scenario === 'source') f.write('program/src/main.cpp', 'int main() { return 1; }\n');
    else if (scenario === 'header') f.write('code/include/api.hpp', '#pragma once\nint next_answer();\n');
    else if (scenario === 'unknown') f.write('input.cfg', 'unknown\n');
    else if (scenario === 'delete') f.git('rm', 'docs/guide.md');
    else if (scenario !== 'empty') f.write('docs/guide.md', 'New guide\n');
    if (scenario === 'empty') f.git('commit', '--allow-empty', '-qm', 'empty PR');
    else f.commit('delta');
    const head = f.git('rev-parse', 'HEAD');
    const merge = f.git('commit-tree', f.git('rev-parse', 'HEAD^{tree}'), '-p', base, '-p', head, '-m', 'PR');
    f.git('checkout', '-q', '--detach', merge);
    const context = {eventName: 'pull_request', sha: merge, ref: 'refs/pull/42/merge',
      payload: {pull_request: {base: {sha: base}, head: {sha: head}}}};
    if (scenario === 'push') Object.assign(context, {eventName: 'push', ref: 'refs/heads/main', payload: {before: base, after: merge}});
    if (scenario === 'manual') context.eventName = 'workflow_dispatch';
    if (scenario === 'schedule') context.eventName = 'schedule';
    if (scenario === 'shallow') f.write('.git/shallow', merge + '\n');
    const outputs = {};
    const core = {setOutput: (key, value) => { outputs[key] = value; }, info: () => {}};
    const adapter = name => {
      if (name.endsWith('evidence.cjs')) return {requiredScope: args => evidence.requiredScope({...args, root: f.root})};
      assert.equal(name, './.github/actions/select-ecosystem-ci/changes.cjs');
      return {collect: args => changes.collect({...args, root: f.root}), classify: args => changes.classify({...args, root: f.root})};
    };
    resolve(adapter, context, core);
    if (scenario === 'dirty') f.write('docs/guide.md', 'Uncommitted\n');
    if (scenario === 'repo-drift') f.write('CMakeLists.txt', '# drift\n');
    classify(adapter, context, core, {cwd: () => f.root, env: {ENGELS_BINARY: engels, BASE_SCOPE: scenario === 'schedule' ? 'security' : 'full'}});
    const narrow = ['docs', 'empty'].includes(scenario);
    assert.equal(outputs.codeql, narrow ? 'repository' : 'full');
    const before = f.git('status', '--porcelain');
    const checked = shell(f, repository, {});
    assert.equal(checked.status, scenario === 'repo-drift' ? 5 : 0, checked.stdout + checked.stderr);
    const steps = {classification: {outputs: {codeql: outputs.codeql}}, repository: {outputs: {status: checked.status === 0 ? 'passed' : 'failed'}},
      codeql_init: {outcome: 'skipped'}, codeql_build: {outcome: 'skipped', outputs: {status: ''}}, codeql_analyze: {outcome: 'skipped'}};
    const run = !narrow && checked.status === 0;
    assert.equal(eligible('        id: codeql_init\n', steps), run);
    if (run) steps.codeql_init.outcome = 'success';
    assert.equal(eligible('      - name: Build project through ecosystem\n', steps), run);
    if (run) Object.assign(steps.codeql_build, {outcome: 'success', outputs: {status: 'passed'}});
    assert.equal(eligible('      - name: Run CodeQL analysis\n', steps), run);
    if (run) steps.codeql_analyze.outcome = 'success';
    const env = {DIAGNOSTIC_OUTCOME: 'success', DIAGNOSTIC_STATUS: 'passed', TOOLCHAIN_STATUS: 'success', CLASSIFICATION_STATUS: 'success', CHECK_SCOPE: scenario === 'schedule' ? 'security' : 'full', EVENT_NAME: context.eventName,
      CODEQL_SCOPE: outputs.codeql, REPOSITORY_OUTCOME: 'success', REPOSITORY_STATUS: steps.repository.outputs.status,
      REPOSITORY_EXIT_CODE: String(checked.status), CODEQL_INIT_STATUS: steps.codeql_init.outcome,
      CODEQL_BUILD_OUTCOME: steps.codeql_build.outcome, CODEQL_BUILD_STATUS: steps.codeql_build.outputs.status,
      CODEQL_BUILD_EXIT_CODE: run ? '0' : '',
      CODEQL_ANALYZE_STATUS: steps.codeql_analyze.outcome};
    f.write('.ecosystem/github/reports/00-workflow/result.json', '{"status":"passed"}');
    for (const stage of ['01-tracked-surface', '02-codeql-build', '03-codeql-analysis']) {
      f.write('.ecosystem/github/reports/' + stage + '/summary.md', 'simulated stage');
      f.write('.ecosystem/github/reports/' + stage + '/output.log', 'simulated action log');
    }
    const result = shell(f, gate, env);
    assert.equal(result.status, scenario === 'repo-drift' ? 1 : 0, result.stdout + result.stderr);
    if (narrow) {
      assert.equal(shell(f, summary, {...env, CODEQL_BUILD_STATUS: 'skipped'}).status, 0);
      assert.match(fs.readFileSync(path.join(f.root, '.ecosystem/github/reports/03-codeql-analysis/summary.md'), 'utf8'), /status: `not-required`/);
    }
    for (const name of ['source', 'build']) {
      const directory = path.join(f.root, '.ecosystem', name);
      assert.deepEqual(fs.existsSync(directory) ? fs.readdirSync(directory) : [], [], 'selection must not configure/build');
    }
    assert.equal(f.git('status', '--porcelain'), before);
  });
}

test('repository-only gate rejects incomplete or unrelated runs', t => {
  const f = fixture(t);
  f.write('.ecosystem/step-summary', '');
  const env = {DIAGNOSTIC_OUTCOME: 'success', DIAGNOSTIC_STATUS: 'passed', TOOLCHAIN_STATUS: 'success', CLASSIFICATION_STATUS: 'success', CHECK_SCOPE: 'full', EVENT_NAME: 'pull_request', CODEQL_SCOPE: 'repository',
    REPOSITORY_OUTCOME: 'success', REPOSITORY_STATUS: 'passed', REPOSITORY_EXIT_CODE: '0',
    CODEQL_INIT_STATUS: 'skipped', CODEQL_BUILD_OUTCOME: 'skipped', CODEQL_BUILD_STATUS: '', CODEQL_ANALYZE_STATUS: 'skipped'};
  f.write('.ecosystem/github/reports/00-workflow/result.json', '{"status":"passed"}');
  assert.equal(shell(f, gate, env).status, 0);
  for (const override of [{CODEQL_SCOPE: ''}, {CODEQL_SCOPE: 'unknown'}, {CHECK_SCOPE: 'security'},
    {EVENT_NAME: 'push'}, {EVENT_NAME: 'schedule'}, {EVENT_NAME: 'workflow_dispatch'},
    {REPOSITORY_OUTCOME: 'failure'}, {REPOSITORY_STATUS: 'skipped'}, {REPOSITORY_EXIT_CODE: ''},
    {CODEQL_INIT_STATUS: ''}, {CODEQL_INIT_STATUS: 'failure'}, {CODEQL_BUILD_OUTCOME: 'failure'},
    {CODEQL_BUILD_STATUS: 'passed'}, {CODEQL_ANALYZE_STATUS: ''}, {CODEQL_ANALYZE_STATUS: 'failure'}])
    assert.notEqual(shell(f, gate, {...env, ...override}).status, 0, JSON.stringify(override));
});
