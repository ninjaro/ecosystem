'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const {execFileSync} = require('node:child_process');
const evidence = require(path.resolve(process.argv[2]));

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'manifesto-ci-evidence-'));
  t.after(() => fs.rmSync(root, {recursive: true, force: true}));
  const git = (...args) => execFileSync('git', ['-C', root, '-c', 'commit.gpgsign=false', ...args], {encoding: 'utf8'}).trim();
  const write = (name, data) => {
    fs.mkdirSync(path.dirname(path.join(root, name)), {recursive: true});
    fs.writeFileSync(path.join(root, name), data);
  };
  git('init', '-q'); git('config', 'user.name', 'Fixture'); git('config', 'user.email', 'fixture@example.invalid');
  write('manifest.json', '{"id":"fixture"}');
  write('manifesto.github.vars.json', '{"manifesto_bootstrap":"checkout"}');
  write('.github/workflows/tests.yml', 'checks policy');
  write('.github/workflows/codeql.yml', 'codeql policy');
  write('.gitignore', '.ecosystem/\n');
  git('add', '.'); git('commit', '-qm', 'base');
  const base = git('rev-parse', 'HEAD');
  write('src/main.cpp', 'int main() { return 0; }\n');
  git('add', '.'); git('commit', '-qm', 'feature');
  const head = git('rev-parse', 'HEAD');
  const tree = git('rev-parse', 'HEAD^{tree}');
  const tested = git('commit-tree', tree, '-p', base, '-p', head, '-m', 'test merge');
  git('checkout', '-q', '--detach', tested);
  const repository = {id: 7, full_name: 'owner/project', default_branch: 'main'};
  const pr = {number: 42, head: {sha: head, repo: repository}, base: {sha: base, ref: 'main', repo: repository}};
  const outputs = {};
  const notices = [];
  const core = {setOutput: (key, value) => { outputs[key] = value; }, notice: m => notices.push(m), info: m => notices.push(m)};
  const runs = {};
  const github = {rest: {actions: {getWorkflowRun: async ({run_id}) => ({data: runs[run_id]})}}};
  async function record(kind = 'checks', overrides = {}) {
    delete outputs.name; delete outputs.directory;
    const runId = kind === 'checks' ? 101 : 202;
    runs[runId] = {id: runId, run_attempt: 1, workflow_id: runId + 1000,
      path: `.github/workflows/${kind === 'checks' ? 'tests' : 'codeql'}.yml`,
      repository, head_sha: head, event: 'pull_request', pull_requests: [pr], status: 'completed', conclusion: 'success'};
    const env = {EVIDENCE_KIND: kind, TOOL_SOURCE_ROOT: root, GITHUB_RUN_ATTEMPT: '1',
      CODEQL_INIT_STATUS: 'success', CODEQL_ANALYZE_STATUS: 'success'};
    for (const prefix of ['CHECK', 'COVERAGE', 'REPOSITORY', 'CODEQL_BUILD']) {
      env[prefix + '_OUTCOME'] = 'success'; env[prefix + '_STATUS'] = 'passed'; env[prefix + '_EXIT_CODE'] = '0';
    }
    Object.assign(env, overrides);
    for (const name of evidence.fileNames(kind, env.COVERAGE_STATUS))
      write('.ecosystem/' + name, name === 'reports/coverage.json'
        ? '{"type":"llvm.coverage.json.export","data":[{"files":[{}],"totals":{"lines":{"count":10,"covered":7}}}]}' : 'native evidence\n');
    const context = {repo: {owner: 'owner', repo: 'project'}, runId, sha: tested, eventName: 'pull_request', payload: {repository, pull_request: pr}};
    await evidence.record({github, core, context, env, root});
    if (!outputs.directory) return null;
    return JSON.parse(fs.readFileSync(path.join(outputs.directory, 'receipt.json')));
  }
  return {root, git, write, base, head, tree, tested, pr, repository, outputs, notices, core, github, runs, record};
}

async function consumerFixture(t, strategy = 'squash') {
  const f = fixture(t);
  await f.record('checks'); await f.record('codeql');
  const target = f.git('commit-tree', f.tree, '-p', f.base,
    ...(strategy === 'merge' ? ['-p', f.head] : []), '-m', strategy + ' result');
  f.git('checkout', '-q', '--detach', target);
  const context = {repo: {owner: 'owner', repo: 'project'}, runId: 999,
    sha: target, ref: 'refs/heads/main', eventName: 'push', payload: {repository: f.repository}};
  const pr = {...f.pr, merged: true, merged_at: '2026-09-27T00:00:00Z', merge_commit_sha: target};
  const state = {associated: [pr], pr, jobs: {}, artifacts: {}, archives: {}, calls: [], runs: f.runs};
  function repack(kind, runId = kind === 'checks' ? 101 : 202, id = kind === 'checks' ? 303 : 404) {
    const directory = path.join(f.root, '.ecosystem/github/evidence', kind);
    const zip = path.join(f.root, '.ecosystem', kind + '.zip');
    execFileSync('python3', ['-c',
      'import pathlib,sys,zipfile\nr=pathlib.Path(sys.argv[1])\nwith zipfile.ZipFile(sys.argv[2],"w") as z:\n for p in r.rglob("*"):\n  if p.is_file(): z.write(p,p.relative_to(r))',
      directory, zip]);
    const data = fs.readFileSync(zip);
    const run = f.runs[runId];
    state.archives[id] = data;
    state.artifacts[id] = {id, name: evidence.artifactName(kind, run.id, run.run_attempt),
      expired: false, expires_at: '2100-01-01T00:00:00Z', size_in_bytes: data.length,
      digest: 'sha256:' + evidence.hash(data), workflow_run: {id: run.id, repository_id: 7, head_sha: run.head_sha}};
  }
  function changeReceipt(kind, change) {
    const file = path.join(f.root, '.ecosystem/github/evidence', kind, 'receipt.json');
    const receipt = JSON.parse(fs.readFileSync(file));
    change(receipt);
    fs.writeFileSync(file, JSON.stringify(receipt));
    repack(kind);
  }
  for (const kind of ['checks', 'codeql']) {
    const run = f.runs[kind === 'checks' ? 101 : 202];
    const name = kind === 'checks' ? 'checks' : 'analyze (cpp)';
    state.jobs[run.id] = ['select', name].map(job => ({name: job, run_id: run.id, status: 'completed', conclusion: 'success',
      steps: [kind === 'checks' ? 'Enforce required result' : 'Enforce CodeQL result',
        'Record reusable verification', 'Upload verification evidence'].map(step => ({name: step, status: 'completed', conclusion: 'success'}))}));
    repack(kind);
  }
  f.github.request = async (route, params) => {
    state.calls.push([route, params]);
    if (state.hook) state.hook(route, params);
    if (state.response) {
      const data = state.response(route, params);
      if (data !== undefined) return {data: structuredClone(data)};
    }
    let data;
    if (route.endsWith('/commits/{commit_sha}/pulls')) data = state.associated;
    else if (route.endsWith('/pulls/{pull_number}')) data = state.pr;
    else if (route.endsWith('/workflows/{workflow_id}/runs'))
      data = {workflow_runs: [f.runs[params.workflow_id === 'tests.yml' ? 101 : 202]]};
    else if (route.endsWith('/attempts/{attempt_number}/jobs')) {
      assert.equal(params.attempt_number, f.runs[params.run_id].run_attempt);
      data = {jobs: state.jobs[params.run_id]};
    } else if (route.endsWith('/runs/{run_id}/artifacts'))
      data = {artifacts: Object.values(state.artifacts).filter(a => a.workflow_run.id === params.run_id)};
    else if (route.endsWith('/runs/{run_id}')) data = f.runs[params.run_id];
    else if (route.endsWith('/artifacts/{artifact_id}')) data = state.artifacts[params.artifact_id];
    else if (route.endsWith('/{archive_format}')) data = state.archives[params.artifact_id];
    else if (route.endsWith('/git/commits/{commit_sha}')) data = {
      sha: params.commit_sha, tree: {sha: f.git('rev-parse', params.commit_sha + '^{tree}')},
      parents: f.git('show', '-s', '--format=%P', params.commit_sha).split(' ').map(sha => ({sha})),
    };
    else throw new Error('Unexpected API route: ' + route);
    return {data: structuredClone(data)};
  };
  const select = async (kind = 'checks', scope = 'full') => evidence.select({github: f.github, context,
    core: f.core, root: f.root, env: {BASE_SCOPE: scope, EVIDENCE_KIND: kind}});
  return {...f, state, context, repack, changeReceipt, select};
}

module.exports = {fixture, consumerFixture, evidence};
