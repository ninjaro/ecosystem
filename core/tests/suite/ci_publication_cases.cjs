'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const {consumerFixture, evidence} = require('./ci_evidence_fixtures.cjs');

async function publicationFixture(t, mode = 'fresh', triggerKind = 'checks') {
  const f = await consumerFixture(t);
  const state = f.state;
  state.tip = f.context.sha;
  state.defaultBranch = 'main';
  state.pushes = {};
  for (const [kind, id] of [['checks', 101], ['codeql', 202]]) {
    const reuse = mode !== 'fresh';
    const pushId = reuse ? id + 400 : id;
    const run = {...f.runs[id], id: pushId, event: 'push', head_sha: f.context.sha,
      head_branch: 'main', head_repository: f.repository, pull_requests: []};
    f.runs[pushId] = run;
    state.pushes[kind] = [pushId];
    if (reuse) {
      state.jobs[pushId] = structuredClone(state.jobs[id]);
      for (const job of state.jobs[pushId]) job.run_id = pushId;
      const job = state.jobs[pushId][1];
      for (const step of job.steps.slice(1)) step.conclusion = 'skipped';
      for (const name of ['Setup manifesto tool', kind === 'checks' ? 'Run required local checks' : 'Verify generated tracked surfaces'])
        job.steps.push({name, status: 'completed', conclusion: 'skipped'});
    } else {
      f.changeReceipt(kind, receipt => {
        receipt.event = 'push'; receipt.pr = null; receipt.run_head = f.context.sha;
        receipt.tested_commit = f.context.sha; receipt.tooling_commit = f.context.sha;
      });
    }
  }
  if (mode === 'mixed') {
    const run = f.runs[501];
    state.jobs[501] = structuredClone(state.jobs[101]);
    for (const job of state.jobs[501]) job.run_id = 501;
    const file = path.join(f.root, '.ecosystem/github/evidence/checks/receipt.json');
    const receipt = JSON.parse(fs.readFileSync(file));
    Object.assign(receipt, {event: 'push', pr: null, run_id: run.id, run_head: f.context.sha,
      tested_commit: f.context.sha, tooling_commit: f.context.sha});
    fs.writeFileSync(file, JSON.stringify(receipt));
    f.repack('checks', 501, 703);
  }
  state.response = (route, params) => {
    if (route === 'GET /repos/{owner}/{repo}') return {...f.repository, default_branch: state.defaultBranch};
    if (route.endsWith('/branches/{branch}')) return {name: state.defaultBranch, commit: {sha: state.tip}};
    if (route.endsWith('/workflows/{workflow_id}/runs') && params.event === 'push')
      return {workflow_runs: state.pushes[params.workflow_id === 'tests.yml' ? 'checks' : 'codeql'].map(id => f.runs[id])};
  };
  f.context.eventName = 'workflow_run';
  f.context.payload.workflow_run = structuredClone(f.runs[state.pushes[triggerKind][0]]);
  const publish = async () => {
    for (const key of Object.keys(f.outputs)) delete f.outputs[key];
    await evidence.publication({github: f.github, context: f.context, core: f.core, root: f.root});
  };
  const revalidate = source => evidence.revalidatePublication({github: f.github, context: f.context, root: f.root,
    env: {PUBLICATION_SOURCE: source}});
  return {...f, publish, revalidate};
}

for (const mode of ['fresh', 'reuse', 'mixed']) for (const trigger of ['checks', 'codeql'])
  test(mode + ' evidence publishes after ' + trigger + ' completes last', async t => {
    const f = await publicationFixture(t, mode, trigger);
    await f.publish();
    assert.equal(f.outputs.ready, 'true', f.notices.join('\n'));
    const source = f.outputs.source;
    const receipt = JSON.parse(fs.readFileSync(path.join(f.outputs.directory, 'receipt.json')));
    assert.equal(receipt.kind, 'publication');
    assert.equal(receipt.tested_commit, f.context.sha);
    assert.equal(receipt.files['github/reports/03-codeql-analysis/summary.md'].bytes, 16);
    assert.equal(receipt.coverage, 'passed');
    await f.revalidate(source);
    f.write('.ecosystem/doxygen/html/index.html', 'fresh API');
    execFileSync('python3', ['-c',
      'import pathlib,runpy,sys\nm=runpy.run_path(sys.argv[1])\nbody,files=m["compose"](pathlib.Path(sys.argv[2]),sys.argv[3])\nassert "70.0%" in body and "03-codeql-analysis" in body and "artifact_digest" in body',
      path.resolve(__dirname, '../../../templates/tooling/sphinx_presentation.py.tpl'), f.root, f.outputs.directory]);
  });

const failures = {
  'PR completion': f => { f.context.payload.workflow_run.event = 'pull_request'; },
  'scheduled completion': f => { f.context.payload.workflow_run.event = 'schedule'; },
  'foreign repository': f => { f.context.payload.workflow_run.repository.id = 90; },
  'foreign head repository': f => { f.context.payload.workflow_run.head_repository.id = 90; },
  'nondefault branch': f => { f.context.payload.workflow_run.head_branch = 'feature'; },
  'failed triggering run': f => { f.context.payload.workflow_run.conclusion = 'failure'; },
  'wrong workflow': f => { f.context.payload.workflow_run.path = '.github/workflows/other.yml'; },
  'old default revision': f => { f.state.tip = f.head; },
  'renamed default branch': f => { f.state.defaultBranch = 'trunk'; },
  'unfinished Checks': f => { f.runs[101].status = 'in_progress'; },
  'failed CodeQL': f => { f.runs[202].conclusion = 'failure'; },
  'missing CodeQL run': f => { f.state.pushes.codeql = []; },
  'wrong attempt': f => { f.runs[101].run_attempt = 2; },
  'missing gate': f => { f.state.jobs[202][1].steps.shift(); },
  'missing required job': f => { f.state.jobs[101].pop(); },
  'missing artifact': f => { delete f.state.artifacts[303]; },
  'expired artifact': f => { f.state.artifacts[404].expired = true; },
  'archive changed': f => { f.state.archives[303][0] ^= 1; },
  'PR receipt substituted for push': f => f.changeReceipt('checks', r => { r.pr = f.pr; r.tested_commit = f.tested; }),
  'config differs': f => f.changeReceipt('codeql', r => { r.policy.github_tree = 'wrong'; }),
  'receipt revision differs': f => f.changeReceipt('checks', r => { r.tested_commit = f.head; }),
  'API unavailable': f => { f.state.hook = () => { throw Error('unavailable'); }; },
  'incomplete fresh result labeled reuse': f => { f.state.jobs[101][1].steps[1].conclusion = 'skipped'; },
  'newer failed workflow run': f => {
    f.runs[303] = {...f.runs[101], id: 303, conclusion: 'failure'};
    f.state.pushes.checks.unshift(303);
  },
  'branch advances during validation': f => {
    let reads = 0;
    f.state.hook = route => { if (route.endsWith('/branches/{branch}') && ++reads === 2) f.state.tip = f.head; };
  },
  'rerun starts while other workflow is validated': f => {
    f.state.hook = (route, params) => {
      if (route.endsWith('/artifacts/{artifact_id}') && params.artifact_id === 404) f.runs[101].run_attempt = 2;
    };
  },
};
for (const [name, change] of Object.entries(failures)) test(name + ' defers automatic publication', async t => {
  const f = await publicationFixture(t);
  change(f);
  await f.publish();
  assert.equal(f.outputs.ready, 'false', f.notices.join('\n'));
  assert.equal(f.outputs.directory, undefined);
  assert.equal(f.outputs.source, undefined);
});

for (const mode of ['fresh', 'reuse'])
  test(mode + ' evidence is revalidated after docs and before deployment', async t => {
    const f = await publicationFixture(t, mode);
    await f.publish();
    assert.equal(f.outputs.ready, 'true', f.notices.join('\n'));
    const source = f.outputs.source;
    f.state.tip = f.head;
    await assert.rejects(f.revalidate(source), /tip/);
    f.state.tip = f.context.sha;
    f.runs[f.state.pushes.codeql[0]].run_attempt = 2;
    await assert.rejects(f.revalidate(source));
  });

test('manual deployment also requires the current default-branch revision', async t => {
  const f = await publicationFixture(t);
  f.context.eventName = 'workflow_dispatch';
  await f.revalidate('');
  await evidence.checkPublicationTip({github: f.github, context: f.context});
  f.state.tip = f.head;
  await assert.rejects(f.revalidate(''), /tip/);
  await assert.rejects(evidence.checkPublicationTip({github: f.github, context: f.context}), /tip/);
});

test('the second workflow completion retries publication after the first defers', async t => {
  const f = await publicationFixture(t);
  f.runs[202].status = 'in_progress';
  await f.publish();
  assert.equal(f.outputs.ready, 'false');
  f.runs[202].status = 'completed';
  f.context.payload.workflow_run = structuredClone(f.runs[202]);
  await f.publish();
  assert.equal(f.outputs.ready, 'true', f.notices.join('\n'));
});

for (const change of ['expired', 'rerun']) test('green reuse pushes still reject ' + change + ' PR evidence', async t => {
  const f = await publicationFixture(t, 'reuse');
  if (change === 'expired') f.state.artifacts[404].expired = true;
  else f.runs[202].run_attempt = 2;
  await f.publish();
  assert.equal(f.outputs.ready, 'false');
});

test('unsupported coverage remains explicit in automatic publication', async t => {
  const f = await publicationFixture(t);
  fs.unlinkSync(path.join(f.root, '.ecosystem/github/evidence/checks/reports/coverage.json'));
  f.changeReceipt('checks', receipt => {
    receipt.coverage = 'skipped'; delete receipt.files['reports/coverage.json'];
  });
  await f.publish();
  assert.equal(f.outputs.ready, 'true', f.notices.join('\n'));
  assert.equal(f.outputs.coverage, 'skipped');
  assert.ok(!fs.existsSync(path.join(f.outputs.directory, 'reports/coverage.json')));
  await assert.rejects(f.revalidate(''), /source/);
  await assert.rejects(f.revalidate(JSON.stringify({commit: f.head})), /source/);
});

test('GitHub ref-qualified workflow paths retain exact receipt identity', async t => {
  const f = await publicationFixture(t);
  for (const [kind, id] of [['checks', 101], ['codeql', 202]]) {
    f.runs[id].path += '@main';
    f.changeReceipt(kind, receipt => { receipt.workflow = f.runs[id].path; });
  }
  f.context.payload.workflow_run = structuredClone(f.runs[101]);
  await f.publish();
  assert.equal(f.outputs.ready, 'true', f.notices.join('\n'));
  f.runs[202].path = '.github/workflows/codeql.yml@unrelated';
  await assert.rejects(f.revalidate(f.outputs.source));
});
