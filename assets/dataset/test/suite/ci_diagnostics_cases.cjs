'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const diagnostics = require(path.resolve(process.argv[2]));
const evidence = require(path.resolve(process.argv[3]));
const engels = path.resolve(process.argv[4]);
const changes = require(path.join(path.dirname(path.resolve(process.argv[3])), 'changes.cjs'));

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'ecosystem-ci-diagnosis-'));
  t.after(() => fs.rmSync(root, {recursive: true, force: true}));
  const write = (name, data) => { const file = path.join(root, name); fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, data); };
  const git = (...args) => execFileSync('git', ['-C', root, '-c', 'commit.gpgsign=false', ...args], {encoding: 'utf8', stdio: 'pipe'}).trim();
  git('init', '-q'); git('config', 'user.name', 'Fixture'); git('config', 'user.email', 'fixture@example.invalid');
  write('.gitignore', '.ecosystem/\n'); write('docs/guide.md', 'Guide\n');
  write('manifest.json', JSON.stringify({id: 'fixture', description: 'Synthetic diagnostic fixture.', facade: 'app:main',
    artifacts: [{id: 'app:main', kind: 'exe', root: 'program', owns: [], entry: 'src/main.cpp'}]}));
  write('program/src/main.cpp', 'int main() { return 0; }\n');
  git('add', '.'); git('commit', '-qm', 'base'); const base = git('rev-parse', 'HEAD');
  const core = {setOutput() {}, info() {}, summary: {addRaw() {return this;}, async write() {}}};
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
    const result = evidence.requiredScope({context, root: f.root, baseScope: 'cheap', binary: engels});
    assert.equal(result.scope, file === 'docs/guide.md' ? 'cheap' : 'full');
    assert.match(result.reason, file === 'docs/guide.md' ? /native-presentation/ : /native-full|unverified-delta/);
  });
}
test('missing or tampered delta cannot leave a source push cheap', t => {
  const f = fixture(t); const context = {eventName: 'push', sha: f.base, ref: 'refs/heads/feature', payload: {before: f.base, after: f.base}};
  assert.equal(evidence.requiredScope({context, root: f.root, baseScope: 'cheap', binary: engels}).scope, 'full');
  f.write('.ecosystem/github/changes.json', '{"complete":true,"paths":[]}');
  assert.equal(evidence.requiredScope({context, root: f.root, baseScope: 'cheap', binary: engels}).scope, 'full');
});
test('reuse/security/tag policy is not changed by source promotion', t => {
  const f = fixture(t);
  for (const baseScope of ['full', 'security', 'skip']) assert.equal(evidence.requiredScope({context: {}, root: f.root, baseScope}).scope, baseScope);
});

test('bootstrap failure produces blocked CodeQL stages with the real prerequisite', async t => {
  const f = fixture(t);
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'codeql', scope: 'full', selectionResult: 'success',
    steps: {changes: {outcome: 'success'}, ecosystem: {outcome: 'failure'}, classification: {outcome: 'skipped'},
      repository: {outcome: 'skipped'}, codeql_init: {outcome: 'skipped'}, codeql_build: {outcome: 'skipped'}, codeql_analyze: {outcome: 'skipped'}}});
  assert.equal(record.status, 'failed');
  assert.equal(record.stages[1].category, 'actual-failure');
  assert.equal(record.stages[2].status, 'blocked'); assert.match(record.stages[2].reason, /ecosystem/);
  for (const stage of record.stages.slice(2)) assert.equal(stage.category, 'unmet-prerequisite');
  assert.equal(record.test_counts.status, 'not-observed');
});
test('validated docs-only PR is not applicable, not a failed native analysis', async t => {
  const f = fixture(t);
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'codeql', scope: 'full', selectionResult: 'success', steps: {
    changes: {outcome: 'success'}, ecosystem: {outcome: 'success'}, classification: {outcome: 'success', outputs: {codeql: 'repository'}},
    repository: {outcome: 'success'}, codeql_init: {outcome: 'skipped'}, codeql_build: {outcome: 'skipped'}, codeql_analyze: {outcome: 'skipped'}}});
  assert.equal(record.status, 'passed'); for (const stage of record.stages.slice(4)) assert.equal(stage.status, 'not-applicable');
});
test('full Checks classification failure blocks the required stage at its actual prerequisite', async t => {
  const f = fixture(t);
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'checks', scope: 'full', selectionResult: 'success',
    steps: {changes: {outcome: 'success'}, ecosystem: {outcome: 'success'}, classification: {outcome: 'failure'},
      required: {outcome: 'skipped'}, coverage: {outcome: 'skipped'}}});
  assert.equal(record.status, 'failed');
  assert.equal(record.stages[3].category, 'unmet-prerequisite');
  assert.match(record.stages[3].reason, /classification/);
});
test('successful stage with a missing declared log cannot satisfy a report', async t => {
  const f = fixture(t);
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'checks', scope: 'cheap', selectionResult: 'success', steps: {
    changes: {outcome: 'success'}, ecosystem: {outcome: 'success'}, classification: {outcome: 'success'}, required: {outcome: 'success', outputs: {
      status: 'passed', 'report-path': '.ecosystem/github/reports/01-required-checks/summary.md', 'log-path': '.ecosystem/github/reports/01-required-checks/output.log'}}}});
  assert.equal(record.status, 'failed'); assert.equal(record.stages[3].category, 'missing-report');
});
test('unavailable required tool remains a failure with an environment category', async t => {
  const f = fixture(t);
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'checks', scope: 'cheap', selectionResult: 'success', steps: {
    changes: {outcome: 'success'}, ecosystem: {outcome: 'success'}, classification: {outcome: 'success'}, required: {outcome: 'success', outputs: {status: 'failed', 'exit-code': '4'}}}});
  assert.equal(record.status, 'failed'); assert.equal(record.stages[3].category, 'unavailable-environment');
});
test('test counts and full test detail are collected from actual recorded output', async t => {
  const f = fixture(t); f.write('.ecosystem/github/reports/01-required-checks/output.log', '100% tests passed, 0 tests failed out of 5\n');
  f.write('.ecosystem/build/debug/Testing/Temporary/LastTest.log', 'Totals: 7 passed, 0 failed, 1 skipped\n[  PASSED  ] 3 tests.\n');
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'checks', scope: 'cheap', selectionResult: 'success',
    steps: {changes: {outcome: 'success'}, ecosystem: {outcome: 'success'}, required: {outcome: 'success'}}});
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
      {name: 'Setup ecosystem tool', number: 4, conclusion: 'failure'}, {name: 'Run required local checks', number: 7, conclusion: 'skipped'}]}];}};
  const record = await diagnostics.collect({github, context, core: f.core, root: f.root, kind: 'checks', scope: 'full', workflowResult: 'failure', selectionResult: 'success'});
  assert.equal(record.status, 'failed'); assert.match(record.stages[1].reason, /Setup ecosystem tool/);
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
    changes: {outcome: 'success'}, ecosystem: {outcome: 'success'}, classification: {outcome: 'success'},
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
  f.write(`${directory}/build.log`, 'CMake Error at ecosystem_assets_engels.cmake:153 (configure_file):\n'
    + '  Fail to copy destinationfile: No such file or directory\n\n');
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'checks', scope: 'full', selectionResult: 'success',
    steps: {changes: {outcome: 'success'}, ecosystem: {outcome: 'failure'}}});
  assert.equal(record.status, 'failed'); assert.match(record.reason, /Fail to copy destinationfile/);
  assert.equal(record.stages[1].log_path, `${directory}/build.log`);
  for (const stage of record.stages.slice(2)) assert.equal(stage.category, 'unmet-prerequisite');
});
test('hosted-only multiline errors keep the failing CMake operation and diagnostic', async t => {
  const f = fixture(t); const github = {rest: {actions: {listJobsForWorkflowRun() {}, async downloadJobLogsForWorkflowRun() {
    return {data: 'CMake Error at assets.cmake:153 (configure_file):\n  Fail to copy destinationfile: No such file or directory\n\n'};}}},
    async paginate() {return [{id: 42, name: 'checks', conclusion: 'failure', steps: [{name: 'Setup ecosystem tool', number: 4, conclusion: 'failure'}]}];}};
  const record = await diagnostics.collect({github, context, core: f.core, root: f.root, kind: 'checks', scope: 'full', workflowResult: 'failure', selectionResult: 'success'});
  assert.equal(record.status, 'failed'); assert.match(record.logs.join('\n'), /Fail to copy destinationfile/);
});
test('failed authored groups outrank expected negative probe errors and retain actual counts', async t => {
  const f = fixture(t); const log = '.ecosystem/github/reports/01-required-checks/output.log';
  const text = '100% tests passed, 0 tests failed out of 4\n'
    + '[ok] negative_fixture\nCMake Error: expected rejection\n'
    + '[failed] installed_libraries_are_relocatable: incompatible version\n'
    + '[failed] workspace_conformance_reports_all_projects: stale expectation\n'
    + '0% tests passed, 1 tests failed out of 1\nerror[task_failed]: tests failed\n';
  f.write(log, text);f.write('.ecosystem/build/debug/Testing/Temporary/LastTest.log', text);
  const result = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'checks', scope: 'full', selectionResult: 'success', steps: {
    changes: {outcome: 'success'}, ecosystem: {outcome: 'success'}, classification: {outcome: 'success'},
    required: {outcome: 'success', outputs: {status: 'failed', 'exit-code': '5', 'log-path': log}}}});
  assert.equal(result.status, 'failed'); assert.match(result.reason, /installed_libraries_are_relocatable/);
  assert.match(result.reason, /workspace_conformance_reports_all_projects/); assert.doesNotMatch(result.reason, /expected rejection/);
  assert.equal(result.test_counts[0].scope, 'earlier invocation/probe'); assert.equal(result.test_counts[1].scope, 'final invocation');
  assert.equal(result.test_counts[1].failed_runners, 1);
  assert.deepEqual(result.test_counts[2], {source: 'debug/Testing/Temporary/LastTest.log', framework: 'Ecosystem self-tests', passed_group_reports: 1, failed_group_reports: 2});
});
test('successful CodeQL analysis cannot hide a failed receipt or its actual diagnostic', async t => {
  const f = fixture(t);
  await diagnostics.snapshot({core: f.core, root: f.root, kind: 'codeql', scope: 'full', selectionResult: 'success', steps:
    Object.fromEntries(['changes', 'ecosystem', 'classification', 'repository', 'codeql_init', 'codeql_build', 'codeql_analyze'].map(id => [id, {outcome: 'success'}]))});
  const github = {rest: {actions: {listJobsForWorkflowRun() {}, async downloadJobLogsForWorkflowRun() {
    return {data: '\u001b[36;1m echo "::error::not executed"\u001b[0m\nError: Untracked authored inputs after verification: ["unexpected.cpp"]\n'};}}},
    async paginate() {return [{id: 42, name: 'analyze (cpp)', conclusion: 'failure', steps: [{name: 'Record reusable verification', number: 15, conclusion: 'failure'}]}];}};
  const record = await diagnostics.collect({github, context, core: f.core, root: f.root, kind: 'codeql', scope: 'full', workflowResult: 'failure', selectionResult: 'success'});
  assert.equal(record.status, 'failed'); assert.match(record.reason, /unexpected.cpp/); assert.doesNotMatch(record.reason, /not executed/);
  assert.match(record.stages[0].reason, /Untracked authored inputs/);
});

test('native timing, command and reuse inventories are retained in uploaded diagnostics', async t => {
  const f = fixture(t);
  f.write('.ecosystem/reports/ci.json', JSON.stringify([
    {phase: 'repository', status: 'executed', seconds: 1},
    {phase: 'build-and-tests', status: 'executed', seconds: 2},
    {phase: 'java', status: 'not-applicable', reason: 'no managed capability'},
    {phase: 'tidy', status: 'executed', seconds: 3},
    {phase: 'format', status: 'blocked', reason: 'blocked by tidy'}]));
  f.write('.ecosystem/reports/ci-commands.log', '> clang-tidy src/main.cpp\n< 0 seconds=2.1\n');
  f.write('.ecosystem/reports/tidy.json', JSON.stringify({
    execution: [{status: 'reused', reason: 'matching inputs'}],
    tidy_execution: [{status: 'executed', seconds: 2.1, command: ['clang-tidy', 'src/main.cpp']}]}));
  const result = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'checks', scope: 'full', selectionResult: 'success',
    steps: {changes: {outcome: 'success'}, ecosystem: {outcome: 'success'}, classification: {outcome: 'success'},
      required: {outcome: 'success', outputs: {status: 'failed', 'exit-code': '5'}}}});
  assert.deepEqual(result.execution_counts.ast, {executed: 0, reused: 1, skipped: 0, blocked: 0});
  assert.deepEqual(result.execution_counts.phases, {executed: 3, reused: 0, skipped: 1, blocked: 1});
  assert.equal(result.execution_counts.tidy.executed, 1);
  assert.equal(result.native_report_error, null);
  assert.match(fs.readFileSync(path.join(f.root, '.ecosystem/github/reports/native/ci-commands.log'), 'utf8'), /seconds=2.1/);
  assert.equal(result.status, 'failed');
});
test('a claimed passing full check without native execution reports cannot succeed', async t => {
  const f = fixture(t);
  const result = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'checks', scope: 'full', selectionResult: 'success',
    steps: {changes: {outcome: 'success'}, ecosystem: {outcome: 'success'}, classification: {outcome: 'success'},
      required: {outcome: 'success', outputs: {status: 'passed'}}, coverage: {outcome: 'success'}}});
  assert.equal(result.status, 'failed'); assert.match(result.reason, /Missing native verification report/);
});

for (const mode of ['missing-actor', 'unbound-report', 'missing-comparison', 'missing-checks'])
  test('native classification cannot narrow without trustworthy inputs: ' + mode, t => {
    const f = fixture(t); f.write('docs/guide.md', 'changed\n'); const head = (f.git('add', '.'), f.git('commit', '-qm', 'docs'), f.git('rev-parse', 'HEAD'));
    const context = {eventName: 'push', sha: head, ref: 'refs/heads/feature', payload: {before: f.base, after: head}};
    changes.collect({context, core: f.core, root: f.root});
    const classification = mode === 'missing-actor' ? undefined : changes.inspect({root: f.root, binary: engels});
    if (mode === 'unbound-report') classification.comparison.head = f.base;
    if (mode === 'missing-comparison') delete classification.comparison;
    if (mode === 'missing-checks') delete classification.checks;
    assert.equal(evidence.requiredScope({context, root: f.root, baseScope: 'cheap',
      binary: '/nonexistent-native-classifier', classification}).scope, 'full');
  });

test('cheap execution still requires native classification to finish', async t => {
  const f = fixture(t);
  const record = await diagnostics.snapshot({core: f.core, root: f.root, kind: 'checks', scope: 'cheap', selectionResult: 'success',
    steps: {changes: {outcome: 'success'}, ecosystem: {outcome: 'success'}, classification: {outcome: 'skipped'},
      required: {outcome: 'skipped'}, coverage: {outcome: 'skipped'}}});
  assert.equal(record.status, 'failed');
  assert.equal(record.stages[2].category, 'missing-result');
  assert.equal(record.stages[3].category, 'unmet-prerequisite');
});

test('manifest-owned Markdown retains full verification', t => {
  const f = fixture(t);
  f.write('manifest.json', JSON.stringify({id: 'fixture', description: 'Synthetic owned input.', facade: 'app:main',
    artifacts: [{id: 'app:main', kind: 'exe', root: 'program', owns: ['include'], entry: 'src/main.cpp'}]}));
  f.write('program/include/input.md', 'old\n'); f.git('add', '.'); f.git('commit', '-qm', 'owned input'); const base = f.git('rev-parse', 'HEAD');
  f.write('program/include/input.md', 'changed\n'); f.git('add', '.'); f.git('commit', '-qm', 'change'); const head = f.git('rev-parse', 'HEAD');
  const context = {eventName: 'push', sha: head, ref: 'refs/heads/feature', payload: {before: base, after: head}};
  changes.collect({context, core: f.core, root: f.root});
  assert.equal(evidence.requiredScope({context, root: f.root, baseScope: 'cheap', binary: engels}).scope, 'full');
});
