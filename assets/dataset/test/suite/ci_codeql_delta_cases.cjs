'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const {fixture, evidence} = require('./ci_evidence_fixtures.cjs');

const skipped = {VERIFICATION_SCOPE: 'delta', CODEQL_INIT_STATUS: 'skipped',
  CODEQL_BUILD_OUTCOME: 'skipped', CODEQL_BUILD_STATUS: '', CODEQL_BUILD_EXIT_CODE: '', CODEQL_ANALYZE_STATUS: 'skipped'};

test('non-code PR records repository-only CodeQL evidence', async t => {
  const f = fixture(t, 'docs');
  const receipt = await f.record('codeql', skipped);
  assert.equal(receipt.schema, 1);
  assert.equal(receipt.kind, 'codeql-delta');
  assert.deepEqual(receipt.verification, {scope: 'delta', selection: {kind: 'event-delta', tree: f.tree,
    manifest: receipt.policy.manifest, comparison_base: f.base, head: f.head},
  stages: {repository: 'full', build: 'none', analysis: 'none'}});
  assert.equal(f.outputs.name, 'ecosystem-delta-codeql-202-1');
  assert.deepEqual(Object.keys(receipt.files).sort(), evidence.fileNames('codeql', null, 'delta'));
  assert.ok(!Object.keys(receipt.files).some(name => name.includes('02-codeql-build')));
  assert.equal(receipt.coverage, null);
  const snapshot = JSON.parse(fs.readFileSync(path.join(f.outputs.directory, 'github/selection/codeql.json')));
  assert.equal(snapshot.report.execution.codeql, 'repository');
  assert.equal(snapshot.input.event_commit, f.tested);
  for (const [name, info] of Object.entries(receipt.files))
    assert.equal(evidence.hash(fs.readFileSync(path.join(f.outputs.directory, name))), info.sha256);
  f.write('.ecosystem/doxygen/html/index.html', 'API');
  const presentation = path.resolve(__dirname, '../../../templates/tooling/sphinx_presentation.py.tpl');
  assert.throws(() => execFileSync('python3', ['-c',
    'import pathlib,runpy,sys\nsys.dont_write_bytecode=True\nsys.path.insert(0,str(pathlib.Path(sys.argv[1]).parent))\nm=runpy.run_path(sys.argv[1])\nm["compose"](pathlib.Path(sys.argv[2]),sys.argv[3])',
    presentation, f.root, f.outputs.directory], {stdio: 'pipe'}), /Command failed/);
});

for (const [name, override] of Object.entries({
  'repository failure': {REPOSITORY_STATUS: 'failed'},
  'repository cancellation': {REPOSITORY_OUTCOME: 'cancelled'},
  'repository missing result': {REPOSITORY_EXIT_CODE: ''},
  'repository unsupported': {REPOSITORY_STATUS: 'skipped', REPOSITORY_EXIT_CODE: '3'},
  'init failure': {CODEQL_INIT_STATUS: 'failure'},
  'init missing': {CODEQL_INIT_STATUS: ''},
  'build failure': {CODEQL_BUILD_OUTCOME: 'failure'},
  'build stale status': {CODEQL_BUILD_STATUS: 'passed'},
  'build stale exit': {CODEQL_BUILD_EXIT_CODE: '0'},
  'analysis failure': {CODEQL_ANALYZE_STATUS: 'failure'},
  'analysis missing': {CODEQL_ANALYZE_STATUS: ''},
})) test('delta rejects ' + name, async t => {
  const f = fixture(t, 'docs');
  await assert.rejects(f.record('codeql', {...skipped, ...override}));
  assert.equal(f.outputs.name, undefined);
  assert.equal(fs.existsSync(path.join(f.root, '.ecosystem/github/evidence/codeql-delta')), false);
});

for (const mode of ['source', 'shallow', 'forged-plan', 'missing-plan']) {
  test('delta rejects policy input: ' + mode, async t => {
    const f = fixture(t, mode === 'source' ? 'source' : 'docs');
    if (mode === 'shallow') { f.write('.git/shallow', f.tested + '\n'); f.selection(); }
    if (mode === 'forged-plan' || mode === 'missing-plan') {
      const file = '.ecosystem/github/classification.json';
      const report = JSON.parse(fs.readFileSync(path.join(f.root, file)));
      if (mode === 'missing-plan') delete report.execution;
      else report.execution.codeql = 'full';
      f.write(file, JSON.stringify(report));
    }
    await assert.rejects(f.record('codeql', skipped));
    assert.equal(f.outputs.name, undefined);
  });
}

test('Checks cannot produce a CodeQL delta receipt', async t => {
  const f = fixture(t, 'docs');
  await assert.rejects(f.record('checks', skipped), /explicit full verification scope/);
});

test('empty PR retains explicit delta scope', async t => {
  const f = fixture(t, 'empty');
  const receipt = await f.record('codeql', skipped);
  assert.equal(receipt.verification.scope, 'delta');
  const snapshot = JSON.parse(fs.readFileSync(path.join(f.outputs.directory, 'github/selection/codeql.json')));
  assert.deepEqual(snapshot.input.paths, []);
  assert.equal(snapshot.report.classification, 'none');
});

for (const event of ['push', 'workflow_dispatch', 'schedule']) {
  test(`${event} cannot omit analysis through delta evidence`, async t => {
    const f = fixture(t, 'docs');
    Object.assign(f.record_context, {eventName: event, ref: 'refs/heads/main',
      payload: {repository: f.repository, before: f.base, after: f.tested}});
    f.selection(f.record_context);
    await assert.rejects(f.record('codeql', skipped), /Native policy requires full CodeQL/);
    assert.equal(f.outputs.name, undefined);
  });
}

test('skipped analysis cannot claim full verification', async t => {
  const f = fixture(t, 'docs');
  await assert.rejects(f.record('codeql', {...skipped, VERIFICATION_SCOPE: 'full'}), /Incomplete/);
});
