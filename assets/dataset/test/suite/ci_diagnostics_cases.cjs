'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const diagnostics = require(path.resolve(process.argv[2]));
const evidence = require(path.resolve(process.argv[3]));
const changes = require(path.join(path.dirname(path.resolve(process.argv[3])), 'changes.cjs'));

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'manifesto-ci-diagnosis-'));
  t.after(() => fs.rmSync(root, {recursive: true, force: true}));
  const write = (name, data) => { const file = path.join(root, name); fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, data); };
  const git = (...args) => execFileSync('git', ['-C', root, '-c', 'commit.gpgsign=false', ...args], {encoding: 'utf8', stdio: 'pipe'}).trim();
  git('init', '-q'); git('config', 'user.name', 'Fixture'); git('config', 'user.email', 'fixture@example.invalid');
  write('.gitignore', '.ecosystem/\n'); write('docs/guide.md', 'Guide\n');
  git('add', '.'); git('commit', '-qm', 'base'); const base = git('rev-parse', 'HEAD');
  const core = {setOutput() {}, summary: {addRaw() {return this;}, async write() {}}};
  return {root, write, git, base, core};
}

for (const event of ['push', 'pull_request']) for (const file of ['custom/src/input.cpp', 'include/api.hpp', 'CMakeLists.txt',
  'custom/build/config.cmake', 'custom/build/config.cmake.in', 'custom/CMakeLists.txt.in', 'manifest.json',
  'assets/templates/input.tpl', '.github/workflows/tests.yml', 'docs/guide.md']) {
  test(`${event}: cheap scope safely promotes ${file}`, t => {
    const f = fixture(t); f.write(file, 'changed\n'); f.git('add', '.'); f.git('commit', '-qm', 'change'); const head = f.git('rev-parse', 'HEAD');
    const context = {eventName: event, sha: head, ref: 'refs/heads/feature', payload: event === 'push'
      ? {before: f.base, after: head} : {pull_request: {base: {sha: f.base}, head: {sha: head}}}};
    const delta = changes.resolve({root: f.root, context}); assert.equal(delta.complete, true);
    f.write('.ecosystem/github/changes.json', JSON.stringify(delta));
    const result = evidence.requiredScope({context, root: f.root, baseScope: 'cheap'});
    assert.equal(result.scope, file === 'docs/guide.md' ? 'cheap' : 'full');
    assert.match(result.reason, file === 'docs/guide.md' ? /no-source/ : /source-or-build/);
  });
}
test('missing or tampered delta cannot leave a source push cheap', t => {
  const f = fixture(t); const context = {eventName: 'push', sha: f.base, ref: 'refs/heads/feature', payload: {before: f.base, after: f.base}};
  assert.equal(evidence.requiredScope({context, root: f.root, baseScope: 'cheap'}).scope, 'full');
  f.write('.ecosystem/github/changes.json', '{"complete":true,"paths":[]}');
  assert.equal(evidence.requiredScope({context, root: f.root, baseScope: 'cheap'}).scope, 'full');
});
test('reuse/security/tag policy is not changed by source promotion', t => {
  const f = fixture(t);
  for (const baseScope of ['full', 'security', 'skip']) assert.equal(evidence.requiredScope({context: {}, root: f.root, baseScope}).scope, baseScope);
});

test('bootstrap failure produces blocked CodeQL stages with the real prerequisite', async t => {
  const f = fixture(t);
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'codeql', scope: 'full', selectionResult: 'success',
    steps: {changes: {outcome: 'success'}, manifesto: {outcome: 'failure'}, classification: {outcome: 'skipped'},
      repository: {outcome: 'skipped'}, codeql_init: {outcome: 'skipped'}, codeql_build: {outcome: 'skipped'}, codeql_analyze: {outcome: 'skipped'}}});
  assert.equal(record.status, 'failed');
  assert.equal(record.stages[1].category, 'actual-failure');
  assert.equal(record.stages[2].status, 'blocked'); assert.match(record.stages[2].reason, /manifesto/);
  for (const stage of record.stages.slice(2)) assert.equal(stage.category, 'unmet-prerequisite');
  assert.equal(record.test_counts.status, 'not-observed');
});
test('validated docs-only PR is not applicable, not a failed native analysis', async t => {
  const f = fixture(t);
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'codeql', scope: 'full', selectionResult: 'success', steps: {
    changes: {outcome: 'success'}, manifesto: {outcome: 'success'}, classification: {outcome: 'success', outputs: {codeql: 'repository'}},
    repository: {outcome: 'success'}, codeql_init: {outcome: 'skipped'}, codeql_build: {outcome: 'skipped'}, codeql_analyze: {outcome: 'skipped'}}});
  assert.equal(record.status, 'passed'); for (const stage of record.stages.slice(4)) assert.equal(stage.status, 'not-applicable');
});
test('full Checks classification failure blocks the required stage at its actual prerequisite', async t => {
  const f = fixture(t);
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'checks', scope: 'full', selectionResult: 'success',
    steps: {changes: {outcome: 'success'}, manifesto: {outcome: 'success'}, classification: {outcome: 'failure'},
      required: {outcome: 'skipped'}, coverage: {outcome: 'skipped'}}});
  assert.equal(record.status, 'failed');
  assert.equal(record.stages[3].category, 'unmet-prerequisite');
  assert.match(record.stages[3].reason, /classification/);
});
test('successful stage with a missing declared log cannot satisfy a report', async t => {
  const f = fixture(t);
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'checks', scope: 'cheap', selectionResult: 'success', steps: {
    changes: {outcome: 'success'}, manifesto: {outcome: 'success'}, required: {outcome: 'success', outputs: {
      status: 'passed', 'report-path': '.ecosystem/github/reports/01-required-checks/summary.md', 'log-path': '.ecosystem/github/reports/01-required-checks/output.log'}}}});
  assert.equal(record.status, 'failed'); assert.equal(record.stages[3].category, 'missing-report');
});
test('unavailable required tool remains a failure with an environment category', async t => {
  const f = fixture(t);
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'checks', scope: 'cheap', selectionResult: 'success', steps: {
    changes: {outcome: 'success'}, manifesto: {outcome: 'success'}, required: {outcome: 'success', outputs: {status: 'failed', 'exit-code': '4'}}}});
  assert.equal(record.status, 'failed'); assert.equal(record.stages[3].category, 'unavailable-environment');
});
test('test counts and full test detail are collected from actual recorded output', async t => {
  const f = fixture(t); f.write('.ecosystem/github/reports/01-required-checks/output.log', '100% tests passed, 0 tests failed out of 5\n');
  f.write('.ecosystem/build/debug/Testing/Temporary/LastTest.log', 'Totals: 7 passed, 0 failed, 1 skipped\n[  PASSED  ] 3 tests.\n');
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'checks', scope: 'cheap', selectionResult: 'success',
    steps: {changes: {outcome: 'success'}, manifesto: {outcome: 'success'}, required: {outcome: 'success'}}});
  assert.deepEqual(record.test_counts.map(row => row.framework), ['CTest', 'QtTest', 'GoogleTest']);
  assert.equal(record.test_counts[0].registered_runners, 5); assert.equal(record.test_counts[1].skipped, 1);
  assert.equal(fs.readFileSync(path.join(f.root, '.ecosystem/github/reports/test-details/debug/Testing/Temporary/LastTest.log'), 'utf8'),
    fs.readFileSync(path.join(f.root, '.ecosystem/build/debug/Testing/Temporary/LastTest.log'), 'utf8'));
});
const context = {repo: {owner: 'fixture', repo: 'project'}, runId: 1};
const unavailable = {rest: {actions: {listJobsForWorkflowRun() {}}}, async paginate() {throw new Error('restricted token');}};
test('change-data-only artifacts are failed evidence, even when a job claims success', async t => {
  const f = fixture(t); f.write('.ecosystem/github/changes.json', '{}');
  const record = await diagnostics.collect({github: unavailable, context, core: f.core, root: f.root, kind: 'checks',
    scope: 'full', workflowResult: 'success', selectionResult: 'success'});
  assert.equal(record.status, 'failed'); assert.match(record.reason, /Missing complete execution report/);
  assert.match(record.logs[0], /restricted token/);
});
test('completed job logs identify the failed bootstrap and explain every downstream skip', async t => {
  const f = fixture(t); const github = {rest: {actions: {listJobsForWorkflowRun() {}, async downloadJobLogsForWorkflowRun() {
    return {data: 'CMake Error: missing IN_LIST policy\nProcess completed with exit code 2.\n'};}}}, async paginate() {return [{id: 42, name: 'checks', conclusion: 'failure', steps: [
      {name: 'Setup manifesto tool', number: 4, conclusion: 'failure'}, {name: 'Run required local checks', number: 7, conclusion: 'skipped'}]}];}};
  const record = await diagnostics.collect({github, context, core: f.core, root: f.root, kind: 'checks', scope: 'full', workflowResult: 'failure', selectionResult: 'success'});
  assert.equal(record.status, 'failed'); assert.match(record.stages[1].reason, /Setup manifesto tool/);
  assert.match(fs.readFileSync(path.join(f.root, '.ecosystem/github/reports/hosted-logs/42.log'), 'utf8'), /IN_LIST/);
});
test('legitimate excluded job is diagnosed; failed selection never becomes an exclusion', async t => {
  const f = fixture(t);
  const args = {github: unavailable, context, core: f.core, root: f.root, kind: 'codeql', scope: 'cheap', workflowResult: 'skipped', selectionResult: 'success', selectionReason: 'no-source-or-build-change'};
  assert.equal((await diagnostics.collect(args)).status, 'not-applicable');
  assert.equal((await diagnostics.collect({...args, selectionResult: 'failure'})).status, 'failed');
});
test('a strict enforcement failure retains the composite command cause and blocked analysis', async t => {
  const f = fixture(t); const log = '.ecosystem/github/reports/01-tracked-surface/output.log';
  f.write(log, 'forbidden repository entries:\n  .clang-format: ignored local generated configuration must not be tracked\n'
    + '  .clang-tidy: ignored local generated configuration must not be tracked\nerror[task_failed]: repository policy\n');
  await diagnostics.snapshot({core: f.core, root: f.root, kind: 'codeql', scope: 'full', selectionResult: 'success', steps: {
    changes: {outcome: 'success'}, manifesto: {outcome: 'success'}, classification: {outcome: 'success'},
    repository: {outcome: 'success', outputs: {status: 'failed', 'exit-code': '5', 'log-path': log, reason: 'Command failed (exit 5).'}},
    codeql_init: {outcome: 'skipped'}, codeql_build: {outcome: 'skipped'}, codeql_analyze: {outcome: 'skipped'}}});
  const github = {rest: {actions: {listJobsForWorkflowRun() {}, async downloadJobLogsForWorkflowRun() {
    return {data: '##[error]Execution diagnosis is failed.\n'};}}}, async paginate() {return [{id: 42, name: 'analyze (cpp)', conclusion: 'failure', steps: [
      {name: 'Verify generated tracked surfaces', number: 7, conclusion: 'success'},
      {name: 'Run CodeQL analysis', number: 10, conclusion: 'skipped'},
      {name: 'Enforce CodeQL result', number: 14, conclusion: 'failure'}]}];}};
  const record = await diagnostics.collect({github, context, core: f.core, root: f.root, kind: 'codeql', scope: 'full', workflowResult: 'failure', selectionResult: 'success'});
  assert.equal(record.status, 'failed'); assert.match(record.reason, /\.clang-format: ignored local generated configuration/);
  assert.match(record.reason, /\.clang-tidy: ignored local generated configuration/);
  assert.equal(record.stages[0].category, 'unmet-prerequisite'); assert.match(record.stages[0].reason, /codeql_build/);
  assert.match(record.stages[1].reason, /Command failed \(exit 5\)/);
});
test('bootstrap diagnosis selects the failed command and preserves multiline CMake context', async t => {
  const f = fixture(t); const directory = '.ecosystem/github/reports/00-toolchain';
  f.write(`${directory}/steps.tsv`, 'paths\t0\nnative-dependencies\t0\njson\t0\nbuild\t2\n');
  f.write(`${directory}/build.log`, 'CMake Error at manifesto_assets_engels.cmake:153 (configure_file):\n'
    + '  Fail to copy destinationfile: No such file or directory\n\n');
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'checks', scope: 'full', selectionResult: 'success',
    steps: {changes: {outcome: 'success'}, manifesto: {outcome: 'failure'}}});
  assert.equal(record.status, 'failed'); assert.match(record.reason, /Fail to copy destinationfile/);
  assert.equal(record.stages[1].log_path, `${directory}/build.log`);
  for (const stage of record.stages.slice(2)) assert.equal(stage.category, 'unmet-prerequisite');
});
test('hosted-only multiline errors keep the failing CMake operation and diagnostic', async t => {
  const f = fixture(t); const github = {rest: {actions: {listJobsForWorkflowRun() {}, async downloadJobLogsForWorkflowRun() {
    return {data: 'CMake Error at assets.cmake:153 (configure_file):\n  Fail to copy destinationfile: No such file or directory\n\n'};}}},
    async paginate() {return [{id: 42, name: 'checks', conclusion: 'failure', steps: [{name: 'Setup manifesto tool', number: 4, conclusion: 'failure'}]}];}};
  const record = await diagnostics.collect({github, context, core: f.core, root: f.root, kind: 'checks', scope: 'full', workflowResult: 'failure', selectionResult: 'success'});
  assert.equal(record.status, 'failed'); assert.match(record.logs.join('\n'), /Fail to copy destinationfile/);
});
