'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const {fixture, consumerFixture, evidence} = require('./ci_evidence_fixtures.cjs');


test('record successful checks and CodeQL with immutable identity and report hashes', async t => {
  const f = fixture(t);
  for (const kind of ['checks', 'codeql']) {
    const receipt = await f.record(kind);
    assert.ok(!Object.hasOwn(receipt, 'schema'));
    assert.deepEqual(receipt.verification, {scope: 'full',
      selection: {kind: 'whole-tree', tree: f.tree, manifest: f.git('rev-parse', 'HEAD:manifest.json')},
      stages: kind === 'checks'
        ? {repository: 'full', build: 'full', tests: 'full', java: 'full', tidy: 'full', format: 'full', coverage: 'full'}
        : {repository: 'full', build: 'full', analysis: 'full'}});
    assert.equal(receipt.tested_commit, f.tested);
    assert.equal(receipt.tested_tree, f.tree);
    assert.equal(receipt.run_head, f.head);
    assert.equal(receipt.pr.base, f.base);
    assert.equal(receipt.repository.id, 7);
    assert.equal(receipt.run_attempt, 1);
    const selection = JSON.parse(fs.readFileSync(path.join(f.outputs.directory, `github/selection/${kind}.json`)));
    assert.equal(selection.input.event_commit, f.tested);
    assert.equal(selection.report.classification, 'affected');
    assert.equal(selection.report.comparison.manifest_blob, receipt.policy.manifest);
    for (const [name, expected] of Object.entries(receipt.files)) {
      const data = fs.readFileSync(path.join(f.outputs.directory, name));
      assert.deepEqual(expected, {bytes: data.length, sha256: evidence.hash(data)});
    }
    assert.equal(f.outputs.name, evidence.artifactName(kind, receipt.run_id, 1));
  }
});

for (const scope of [undefined, '', 'cheap', 'changed', 'presentation', 'reuse', 'security'])
  test('non-full scope cannot create a reusable receipt: ' + scope, async t => {
    const f = fixture(t);
    for (const kind of ['checks', 'codeql']) {
      await assert.rejects(f.record(kind, {VERIFICATION_SCOPE: scope}), /explicit full verification scope/);
      assert.equal(f.outputs.name, undefined);
    }
  });

for (const kind of ['checks', 'codeql']) {
  test(`${kind} presentation relevance still attests only actual full execution`, async t => {
    const f = fixture(t, 'docs');
    const receipt = await f.record(kind);
    const snapshot = JSON.parse(fs.readFileSync(path.join(f.outputs.directory, `github/selection/${kind}.json`)));
    assert.equal(snapshot.report.classification, 'presentation');
    assert.equal(snapshot.report.checks.codeql, 'none');
    assert.equal(receipt.verification.scope, 'full');
    assert.ok(Object.values(receipt.verification.stages).every(scope => scope === 'full'));
  });
  test(`${kind} incomplete history preserves an explicit full fallback`, async t => {
    const f = fixture(t);
    f.write('.git/shallow', f.tested + '\n');
    f.selection();
    const receipt = await f.record(kind);
    const snapshot = JSON.parse(fs.readFileSync(path.join(f.outputs.directory, `github/selection/${kind}.json`)));
    assert.equal(snapshot.input.reason, 'shallow-history');
    assert.equal(snapshot.input.complete, false);
    assert.equal(snapshot.report.classification, 'full');
    assert.equal(receipt.verification.scope, 'full');
  });
}

for (const [name, change] of Object.entries({
  'missing event': f => fs.unlinkSync(path.join(f.root, '.ecosystem/github/changes.json')),
  'missing classification': f => fs.unlinkSync(path.join(f.root, '.ecosystem/github/classification.json')),
  'malformed classification': f => f.write('.ecosystem/github/classification.json', 'not json'),
  'altered event': f => {
    const file = '.ecosystem/github/changes.json';
    const value = JSON.parse(fs.readFileSync(path.join(f.root, file))); value.base = f.head;
    f.write(file, JSON.stringify(value));
  },
  'altered classification': f => {
    const file = '.ecosystem/github/classification.json';
    const value = JSON.parse(fs.readFileSync(path.join(f.root, file))); value.classification = 'presentation';
    f.write(file, JSON.stringify(value));
  },
  'aliased classification': f => {
    const file = path.join(f.root, '.ecosystem/github/classification.json');
    fs.renameSync(file, file + '.retained'); fs.symlinkSync(file + '.retained', file);
  },
})) test('record rejects ' + name, async t => {
  const f = fixture(t);
  change(f);
  for (const kind of ['checks', 'codeql']) {
    await assert.rejects(f.record(kind));
    assert.equal(f.outputs.name, undefined);
    assert.equal(fs.existsSync(path.join(f.root, '.ecosystem/github/evidence', kind)), false);
  }
});

test('record requires the native classifier executable', async t => {
  const f = fixture(t);
  await assert.rejects(f.record('checks', {ENGELS_BINARY: ''}), /missing-engels-binary/);
  assert.equal(f.outputs.name, undefined);
});

test('unsupported coverage is recorded explicitly without an old coverage report', async t => {
  const f = fixture(t);
  f.write('.ecosystem/reports/coverage.json', 'old report');
  const receipt = await f.record('checks', {COVERAGE_STATUS: 'skipped', COVERAGE_EXIT_CODE: '3'});
  assert.equal(receipt.coverage, 'skipped');
  assert.ok(!receipt.files['reports/coverage.json']);
});

test('record accepts a GitHub workflow path qualified by its PR merge ref', async t => {
  const f = fixture(t);
  const original = f.github.rest.actions.getWorkflowRun;
  f.github.rest.actions.getWorkflowRun = async args => {
    const result = await original(args);
    result.data.path += '@refs/pull/42/merge';
    return result;
  };
  const receipt = await f.record();
  assert.equal(receipt.workflow, '.github/workflows/tests.yml@refs/pull/42/merge');
});

for (const [kind, env] of [
  ['checks', {CHECK_OUTCOME: 'failure'}], ['checks', {CHECK_EXIT_CODE: '5'}],
  ['checks', {COVERAGE_STATUS: 'skipped', COVERAGE_EXIT_CODE: '4'}],
  ['checks', {COVERAGE_OUTCOME: ''}], ['codeql', {CODEQL_INIT_STATUS: 'failure'}],
  ['codeql', {CODEQL_BUILD_EXIT_CODE: '3'}], ['codeql', {CODEQL_ANALYZE_STATUS: 'cancelled'}],
]) test('failed native results cannot create a receipt: ' + JSON.stringify(env), async t => {
  const f = fixture(t);
  await assert.rejects(f.record(kind, env), /Incomplete/);
  assert.equal(f.outputs.name, undefined);
});

test('dirty and untracked authored inputs cannot receive a receipt', async t => {
  const f = fixture(t);
  f.write('src/main.cpp', 'changed after tests');
  await assert.rejects(f.record(), /Modified authored inputs.*src\/main\.cpp/);
  f.git('restore', 'src/main.cpp');
  f.write('src/untested.cpp', 'new input');
  await assert.rejects(f.record(), /Untracked authored inputs.*src\/untested\.cpp/);
});

test('installer logs outside ignored state block a receipt with their exact path', async t => {
  const f = fixture(t);
  f.write('aqtinstall.log', 'installer output');
  await assert.rejects(f.record('codeql'), /Untracked authored inputs.*aqtinstall\.log/);
  fs.unlinkSync(path.join(f.root, 'aqtinstall.log'));
  f.write('.ecosystem/github/reports/00-toolchain/aqtinstall.log', 'installer output');
  const receipt = await f.record('codeql');
  assert.equal(receipt.kind, 'codeql');
});

test('mutable tooling references cannot receive reusable receipts', async t => {
  const f = fixture(t);
  f.write('ecosystem.github.vars.json', '{"ecosystem_repository":"owner/tools","ecosystem_ref":"main"}');
  assert.equal(await f.record(), null);
  assert.equal(f.outputs.name, undefined);
  assert.match(f.notices.join('\n'), /immutable tooling commit/);
});

test('service aliases are rejected without modifying their destination', async t => {
  const f = fixture(t);
  fs.mkdirSync(path.join(f.root, '.ecosystem/github'), {recursive: true});
  fs.mkdirSync(path.join(f.root, 'retained'));
  // Git does not list empty directories, so this specifically exercises output safety.
  fs.symlinkSync(path.join(f.root, 'retained'), path.join(f.root, '.ecosystem/github/evidence'));
  await assert.rejects(f.record(), /symlink/);
  assert.deepEqual(fs.readdirSync(path.join(f.root, 'retained')), []);
});

for (const strategy of ['merge', 'squash', 'rebase'])
  test(strategy + ' reuses both verified PR runs when the tested and merged trees match', async t => {
    const f = await consumerFixture(t, strategy);
    for (const kind of ['checks', 'codeql']) {
      await f.select(kind);
      assert.equal(f.outputs.scope, 'reuse', f.notices.join('\n'));
      assert.equal(f.outputs.verified, 'true');
      const source = JSON.parse(f.outputs.source);
      assert.equal(source.checks.artifact_id, 303);
      assert.equal(source.codeql.artifact_id, 404);
      assert.equal(source.tree, f.tree);
      assert.equal(source.checks.verification.scope, 'full');
      assert.equal(source.codeql.verification.stages.analysis, 'full');
      assert.ok(fs.existsSync(path.join(f.outputs.reports, kind === 'checks' ? 'github/reports/01-required-checks/summary.md' : '01-tracked-surface/summary.md')));
    }
  });

const failures = {
  'direct push without a merged PR': f => { f.state.associated = []; },
  'unmerged associated PR': f => { f.state.associated[0].merged_at = null; },
  'PR belongs to another repository': f => { f.state.pr.base = {...f.pr.base, repo: {id: 90}}; },
  'failed Checks run': f => { f.runs[101].conclusion = 'failure'; },
  'incomplete CodeQL run': f => { f.runs[202].status = 'in_progress'; },
  'missing fork PR run association': f => { f.runs[101].pull_requests = []; },
  'wrong workflow': f => { f.runs[101].path = '.github/workflows/unrelated.yml'; },
  'skipped required job': f => { f.state.jobs[101][1].conclusion = 'skipped'; },
  'missing required job': f => { f.state.jobs[202].pop(); },
  'duplicate required job': f => { f.state.jobs[101].push(f.state.jobs[101][1]); },
  'missing producer step': f => { f.state.jobs[101][1].steps.pop(); },
  'cancelled producer step': f => { f.state.jobs[101][1].steps[1].conclusion = 'cancelled'; },
  'missing artifact': f => { delete f.state.artifacts[303]; },
  'expired artifact': f => { f.state.artifacts[404].expired = true; },
  'elapsed artifact retention': f => { f.state.artifacts[303].expires_at = '2020-01-01T00:00:00Z'; },
  'artifact from another repository': f => { f.state.artifacts[303].workflow_run.repository_id = 90; },
  'artifact digest mismatch': f => { f.state.archives[303][0] ^= 1; },
  'artifact has no immutable digest': f => { delete f.state.artifacts[303].digest; },
  'old rerun artifact': f => { f.runs[101].run_attempt = 2; },
  'receipt from another attempt': f => f.changeReceipt('checks', r => { r.run_attempt = 2; }),
  'receipt from another workflow': f => f.changeReceipt('checks', r => { r.workflow_id = 999; }),
  'legacy receipt without scope': f => f.changeReceipt('checks', r => { r.schema = 1; delete r.verification; }),
  'legacy receipt without classification': f => f.changeReceipt('checks', r => { r.schema = 2; delete r.files['github/selection/checks.json']; }),
  'missing selection snapshot': f => f.changeReceipt('checks', r => { delete r.files['github/selection/checks.json']; }),
  'selection checkout mismatch': f => f.changeSelection('checks', s => { s.input.checkout = f.head; }),
  'selection event mismatch': f => f.changeSelection('checks', s => { s.input.event = 'push'; }),
  'selection revision mismatch': f => f.changeSelection('checks', s => { s.input.event_commit = f.head; }),
  'selection base mismatch': f => f.changeSelection('checks', s => { s.input.base = f.head; }),
  'selection head mismatch': f => f.changeSelection('checks', s => { s.input.head = f.base; }),
  'classification manifest mismatch': f => f.changeSelection('codeql', s => { s.report.comparison.manifest_blob = f.head; }),
  'classification project mismatch': f => f.changeSelection('codeql', s => { s.report.project = 'other'; }),
  'classification comparison mismatch': f => f.changeSelection('codeql', s => { s.report.comparison.comparison_base = f.head; }),
  'mutated classification snapshot': f => {
    f.write('.ecosystem/github/evidence/checks/github/selection/checks.json', '{}'); f.repack('checks');
  },
  'unvalidated narrow classification': f => f.changeSelection('checks', s => { s.report.delta_validated = false; }),
  'incomplete input with selected paths': f => f.changeSelection('checks', s => { s.input.complete = false; }),
  'missing verification scope': f => f.changeReceipt('checks', r => { delete r.verification; }),
  'delta CodeQL receipt': f => f.changeReceipt('codeql', r => { r.schema = 1; r.kind = 'codeql-delta'; r.verification.scope = 'delta'; }),
  'delta artifact name': f => { f.state.artifacts[404].name = evidence.artifactName('codeql', 202, 1, 'delta'); },
  'narrow formatting claimed as full': f => f.changeReceipt('checks', r => { r.verification.stages.format = 'changed'; }),
  'missing required stage scope': f => f.changeReceipt('checks', r => { delete r.verification.stages.tidy; }),
  'presentation-only CodeQL': f => f.changeReceipt('codeql', r => { r.verification.scope = 'presentation'; }),
  'skipped CodeQL analysis': f => f.changeReceipt('codeql', r => { r.verification.stages.analysis = 'none'; }),
  'unexpected classification input': f => f.changeReceipt('checks', r => { r.verification.selection.delta = []; }),
  'stale verification input tree': f => f.changeReceipt('checks', r => { r.verification.selection.tree = f.head; }),
  'stale verification manifest': f => f.changeReceipt('checks', r => { r.verification.selection.manifest = f.head; }),
  'receipt workflow configuration mismatch': f => f.changeReceipt('checks', r => { r.policy.github_tree = 'wrong'; }),
  'receipt tested revision is not a merge': f => f.changeReceipt('checks', r => { r.tested_commit = f.head; r.tooling_commit = f.head; }),
  'stale base': f => f.changeReceipt('checks', r => { r.pr.base = f.head; }),
  'missing file digest': f => f.changeReceipt('checks', r => { delete r.files['reports/coverage.json']; }),
  'mutated report': f => { f.write('.ecosystem/github/evidence/checks/reports/coverage.json', 'mutated'); f.repack('checks'); },
  'missing report': f => { fs.unlinkSync(path.join(f.root, '.ecosystem/github/evidence/checks/reports/coverage.json')); f.repack('checks'); },
  'unreceipted stale coverage': f => f.changeReceipt('checks', r => { r.coverage = 'skipped'; delete r.files['reports/coverage.json']; }),
  'API unavailable': f => { f.state.hook = () => { throw new Error('API unavailable'); }; },
  'rerun starts during validation': f => {
    let reads = 0;
    f.state.hook = (route, params) => {
      if (route.endsWith('/runs/{run_id}') && params.run_id === 101 && ++reads === 2) f.runs[101].run_attempt = 2;
    };
  },
  'Checks rerun starts while CodeQL is validated': f => {
    let reads = 0;
    f.state.hook = (route, params) => {
      if (route.endsWith('/runs/{run_id}') && params.run_id === 101 && ++reads === 3) f.runs[101].run_attempt = 2;
    };
  },
  'Checks and CodeQL tested different merge commits': f => {
    const different = f.git('commit-tree', f.tree, '-p', f.base, '-p', f.head, '-m', 'another synthetic merge');
    f.changeReceipt('codeql', r => { r.tested_commit = different; r.tooling_commit = different; });
  },
};
for (const [name, change] of Object.entries(failures))
  test(name + ' falls back to fresh verification', async t => {
    const f = await consumerFixture(t);
    change(f);
    await f.select();
    assert.equal(f.outputs.scope, 'full', f.notices.join('\n'));
    assert.equal(f.outputs.verified, 'false');
    assert.equal(f.outputs.source, undefined);
    assert.ok(f.notices.some(m => m.startsWith('Fresh verification required:')));
  });

test('a later base change changes the merged tree and requires fresh verification', async t => {
  const f = await consumerFixture(t);
  f.write('src/base.cpp', 'int base() { return 1; }');
  f.git('add', 'src/base.cpp'); f.git('commit', '-qm', 'base changed');
  f.context.sha = f.git('rev-parse', 'HEAD');
  f.state.pr.merge_commit_sha = f.context.sha;
  await f.select();
  assert.equal(f.outputs.scope, 'full');
  assert.equal(f.outputs.verified, 'false');
});

test('cheap, PR, manual and scheduled paths do not consult reuse APIs', async t => {
  const f = await consumerFixture(t);
  for (const [event, scope] of [['push', 'cheap'], ['pull_request', 'full'], ['workflow_dispatch', 'full'], ['schedule', 'security']]) {
    f.context.eventName = event;
    await f.select('checks', scope);
    assert.equal(f.outputs.scope, scope === 'cheap' ? 'full' : scope);
    assert.equal(f.outputs.verified, 'false');
  }
  assert.equal(f.state.calls.length, 0);
});

test('GitHub run metadata may identify the PR head or its synthetic merge commit', async t => {
  const f = await consumerFixture(t);
  for (const [kind, id] of [['checks', 101], ['codeql', 202]]) {
    f.runs[id].head_sha = f.tested;
    f.changeReceipt(kind, receipt => { receipt.run_head = f.tested; });
  }
  await f.select();
  assert.equal(f.outputs.scope, 'reuse', f.notices.join('\n'));
});

for (const mode of ['traversal', 'absolute', 'symlink', 'duplicate', 'unexpected', 'expansion'])
  test('unsafe archive ' + mode + ' is rejected before publication', async t => {
    const f = await consumerFixture(t);
    const zip = path.join(f.root, '.ecosystem/checks.zip');
    execFileSync('python3', ['-c', `
import pathlib, stat, struct, sys, zipfile
p, mode = sys.argv[1:]
if mode == 'expansion':
    data = bytearray(pathlib.Path(p).read_bytes())
    index = data.index(b'PK\\x01\\x02')
    struct.pack_into('<I', data, index + 24, 256 * 1024 * 1024 + 1)
    pathlib.Path(p).write_bytes(data)
else:
    name = {'traversal':'../escape', 'absolute':'/escape', 'symlink':'link',
            'duplicate':'receipt.json', 'unexpected':'script.cjs'}[mode]
    entry = zipfile.ZipInfo(name)
    if mode == 'symlink':
        entry.create_system = 3
        entry.external_attr = (stat.S_IFLNK | 0o777) << 16
    with zipfile.ZipFile(p, 'a') as archive:
        archive.writestr(entry, b'not executable evidence')
`, zip, mode], {stdio: 'pipe'});
    f.state.archives[303] = fs.readFileSync(zip);
    f.state.artifacts[303].digest = 'sha256:' + evidence.hash(f.state.archives[303]);
    f.state.artifacts[303].size_in_bytes = f.state.archives[303].length;
    await f.select();
    assert.equal(f.outputs.scope, 'full');
    assert.equal(f.outputs.verified, 'false');
    assert.equal(f.outputs.source, undefined);
    assert.ok(!fs.existsSync(path.join(f.root, '.ecosystem/github/reuse')));
  });

for (const name of ['unknown environment', 'changed compiler', 'changed dependencies', 'changed ignored configuration']) {
  test('unchanged Git inputs cannot reuse ' + name, async t => {
    const f = await consumerFixture(t);
    const identity = name === 'unknown environment' ? {known: false}
      : name === 'changed compiler' ? {...f.runtime, tools: {...f.runtime.tools, clang: 'different'}}
      : name === 'changed dependencies' ? {...f.runtime, dependencies: 'b'.repeat(64)}
      : {...f.runtime, configuration: {...f.runtime.configuration, '.clang-tidy': 'c'.repeat(64)}};
    await f.select('checks', 'full', identity);
    assert.equal(f.outputs.scope, 'full'); assert.equal(f.outputs.verified, 'false');
    assert.match(f.outputs.reason, /identity|unknown|differ/i);
  });
}

test('preliminary event selection never accepts compiled evidence or requires SDK variables', async t => {
  const f = await consumerFixture(t);
  const previous = Object.fromEntries(['ImageVersion', 'QT_ROOT_DIR', 'CMAKE_PREFIX_PATH'].map(name => [name, process.env[name]]));
  try {
    for (const name of Object.keys(previous)) delete process.env[name];
    for (const scope of ['cheap', 'full', 'security', 'skip']) {
      evidence.preliminary({core: f.core, env: {BASE_SCOPE: scope}});
      assert.equal(f.outputs.scope, scope);
      assert.equal(f.outputs.verified, 'false');
    }
    assert.equal(f.state.calls.length, 0);
    const started = performance.now();
    await evidence.select({github: f.github, context: f.context, core: f.core, root: f.root,
      env: {BASE_SCOPE: 'full', EVIDENCE_KIND: 'checks'}});
    assert.equal(f.outputs.scope, 'full');
    assert.equal(f.outputs.verified, 'false');
    assert.equal(f.state.calls.length, 0);
    t.diagnostic(`Unknown identity chooses execution in ${(performance.now() - started).toFixed(2)}ms; no artifact calls`);
  } finally {
    for (const [key, value] of Object.entries(previous))
      if (value === undefined) delete process.env[key]; else process.env[key] = value;
  }
});

test('default runtime identity becomes usable only after explicit synthetic environment setup', async t => {
  const os = require('node:os');
  const setup = fs.mkdtempSync(path.join(os.tmpdir(), 'synthetic-ci-sdk-'));
  t.after(() => fs.rmSync(setup, {recursive: true, force: true}));
  const bin = path.join(setup, 'bin'), sdk = path.join(setup, 'sdk');
  fs.mkdirSync(bin); fs.mkdirSync(sdk);
  fs.writeFileSync(path.join(sdk, 'synthetic-qt.hpp'), '// synthetic SDK, not a real Qt installation\n');
  for (const name of ['clang', 'clang++', 'cmake', 'ctest', 'clang-tidy', 'clang-format', 'llvm-cov', 'llvm-profdata', 'dpkg-query'])
    fs.writeFileSync(path.join(bin, name), '#!/bin/sh\nprintf "synthetic-' + name + '=1\\n"\n', {mode: 0o755});
  const saved = Object.fromEntries(['PATH', 'ImageVersion', 'QT_ROOT_DIR', 'CMAKE_PREFIX_PATH', 'CMAKE_TOOLCHAIN_FILE',
    'LLVM_DIR', 'Clang_DIR'].map(name => [name, process.env[name]]));
  try {
    Object.assign(process.env, {PATH: bin + path.delimiter + saved.PATH,
      ImageVersion: 'synthetic-test-runner-not-hosted-evidence', QT_ROOT_DIR: sdk, CMAKE_PREFIX_PATH: sdk});
    delete process.env.CMAKE_TOOLCHAIN_FILE;
    const f = await consumerFixture(t, 'squash', 'default');
    assert.equal(f.runtime.known, true);
    assert.match(f.runtime.image, /^synthetic-/);
    const start = performance.now();
    const actual = evidence.runtimeIdentity(f.root);
    t.diagnostic(`Synthetic one-file SDK identity: ${(performance.now() - start).toFixed(2)}ms; real Qt-tree cost unmeasured`);
    assert.deepEqual(actual, f.runtime);
    const selectDefault = () => evidence.select({github: f.github, context: f.context, core: f.core, root: f.root,
      env: {BASE_SCOPE: 'full', EVIDENCE_KIND: 'checks'}});
    for (const name of ['ImageVersion', 'QT_ROOT_DIR', 'CMAKE_PREFIX_PATH']) {
      const value = process.env[name]; delete process.env[name];
      await selectDefault();
      assert.equal(f.outputs.scope, 'full', name);
      assert.equal(f.outputs.verified, 'false', name);
      process.env[name] = value;
    }
    assert.equal(f.state.calls.length, 0, 'unknown identities must not download artifacts');
    for (const name of ['LLVM_DIR', 'Clang_DIR']) {
      process.env[name] = path.join(setup, 'different-synthetic-sdk', name);
      await selectDefault();
      assert.equal(f.outputs.scope, 'full', name + ' changes the selected SDK');
      assert.equal(f.outputs.verified, 'false', name);
      if (saved[name] === undefined) delete process.env[name]; else process.env[name] = saved[name];
    }
    fs.appendFileSync(path.join(bin, 'clang++'), '# different synthetic compiler\n');
    await selectDefault();
    assert.equal(f.outputs.scope, 'full'); assert.equal(f.outputs.verified, 'false');
    fs.writeFileSync(path.join(bin, 'clang++'), '#!/bin/sh\nprintf "synthetic-clang++=1\\n"\n');
    fs.appendFileSync(path.join(sdk, 'synthetic-qt.hpp'), '// different SDK\n');
    await selectDefault();
    assert.equal(f.outputs.scope, 'full'); assert.equal(f.outputs.verified, 'false');
    fs.writeFileSync(path.join(sdk, 'synthetic-qt.hpp'), '// synthetic SDK, not a real Qt installation\n');
    await selectDefault();
    assert.equal(f.outputs.scope, 'reuse', f.notices.join('\n'));
    assert.equal(f.outputs.verified, 'true');
  } finally {
    for (const [key, value] of Object.entries(saved))
      if (value === undefined) delete process.env[key]; else process.env[key] = value;
  }
});
