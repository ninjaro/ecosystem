'use strict';
// Generated full-workflow programs capture native policy against their own Git checkout.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {test} = require('node:test');
const evidence = require(path.join(path.dirname(path.resolve(process.argv[2])), 'evidence.cjs'));
const {fixture, changes, engels} = require('./ci_change_fixtures.cjs');

function program(workflow, step, indentation = 6, bodyIndentation = 12) {
  const marker = `${' '.repeat(indentation)}- name: ${step}\n`;
  assert.ok(workflow.includes(marker), marker);
  const block = workflow.slice(workflow.indexOf(marker) + marker.length).split('\n' + ' '.repeat(indentation) + '- ')[0];
  const body = block.match(new RegExp(' '.repeat(bodyIndentation - 2) + 'script: \\|\n((?: {' + bodyIndentation + '}.*\n|\n)*)'));
  assert.ok(body, step);
  return new Function('require', 'context', 'core', 'process',
    body[1].split('\n').map(line => line.slice(bodyIndentation)).join('\n'));
}

for (const [kind, file] of [['checks', process.argv[4]], ['codeql', process.argv[5]]]) {
  const workflow = fs.readFileSync(file, 'utf8');
  const resolve = program(workflow, 'Resolve checked-out changes');
  const classify = program(workflow, 'Classify checked-out changes');
  for (const scenario of ['source', 'header', 'docs', 'assets', 'unknown', 'manifest',
    'delete', 'rename', 'empty', 'pull-request', 'manual', 'schedule', 'shallow', 'dirty', 'mismatched-event']) {
    test(`${kind} classification: ${scenario}`, t => {
      const f = fixture(t);
      if (scenario === 'header') f.write('code/include/api.hpp', '#pragma once\nint new_answer();\n');
      else if (['docs', 'pull-request'].includes(scenario)) f.write('docs/guide.md', 'New guide\n');
      else if (scenario === 'assets') f.write('assets/data.txt', 'data\n');
      else if (scenario === 'unknown') f.write('inputs.cfg', 'unknown\n');
      else if (scenario === 'manifest') f.write('manifest.json', JSON.stringify({...f.manifest, description: 'new'}));
      else if (scenario === 'delete') f.git('rm', 'docs/guide.md');
      else if (scenario === 'rename') f.git('mv', 'docs/guide.md', 'docs/renamed.md');
      else if (scenario !== 'empty') f.write('program/src/main.cpp', 'int main() { return 1; }\n');
      const head = scenario === 'empty' ? f.base : f.commit('event changes');
      const context = {eventName: 'push', sha: head, ref: 'refs/heads/main', payload: {before: f.base, after: head}};
      if (scenario === 'pull-request') {
        const merge = f.git('commit-tree', f.git('rev-parse', 'HEAD^{tree}'), '-p', f.base, '-p', head, '-m', 'PR merge');
        f.git('checkout', '-q', '--detach', merge);
        Object.assign(context, {eventName: 'pull_request', sha: merge, ref: 'refs/pull/42/merge',
          payload: {pull_request: {base: {sha: f.base}, head: {sha: head}}}});
      }
      if (scenario === 'manual') context.eventName = 'workflow_dispatch';
      if (scenario === 'schedule') context.eventName = 'schedule';
      if (scenario === 'mismatched-event') context.sha = f.base;
      if (scenario === 'shallow') f.write('.git/shallow', head + '\n');
      const outputs = {};
      const core = {setOutput: (key, value) => { outputs[key] = value; }, info: () => {}};
      const requireAdapter = name => {
        if (name.endsWith('/evidence.cjs')) return {requiredScope: args => evidence.requiredScope({...args, root: f.root})};
        assert.equal(name, './.github/actions/select-ecosystem-ci/changes.cjs');
        return {collect: args => changes.collect({...args, root: f.root}),
          classify: args => changes.classify({...args, root: f.root})};
      };
      resolve(requireAdapter, context, core);
      if (scenario === 'dirty') f.write('docs/guide.md', 'Changed after acquisition\n');
      const before = f.git('status', '--porcelain');
      classify(requireAdapter, context, core, {cwd: () => f.root, env: {ENGELS_BINARY: engels, BASE_SCOPE: scenario === 'schedule' ? 'security' : scenario === 'manual' ? 'full' : 'cheap'}});
      const report = JSON.parse(fs.readFileSync(outputs['classification-file']));
      const expected = ['source', 'header'].includes(scenario) ? 'affected'
        : ['docs', 'pull-request'].includes(scenario) ? 'presentation' : scenario === 'empty' ? 'none' : 'full';
      assert.equal(report.classification, expected, report.reason);
      const cheap = ['docs', 'empty', 'pull-request'].includes(scenario) && kind === 'checks' || scenario === 'pull-request';
      assert.equal(outputs.scope, scenario === 'schedule' ? 'security' : cheap ? 'cheap' : 'full');
      const checkout = JSON.parse(fs.readFileSync(path.join(f.root, '.ecosystem/github/reports/00-checkout/result.json')));
      assert.equal(checkout.status, scenario === 'dirty' ? 'dirty' : 'clean');
      if (scenario === 'dirty') assert.match(checkout.porcelain, /docs\/guide.md/);
      assert.equal(outputs.codeql, scenario === 'pull-request' ? 'repository' : 'full');
      assert.deepEqual(report, f.readReport(), 'snapshot must be the native policy without adapter reinterpretation');
      assert.deepEqual(JSON.parse(fs.readFileSync(outputs['changes-file'])), JSON.parse(outputs.changes));
      assert.equal(f.git('status', '--porcelain'), before, 'classification must preserve inputs');
      assert.equal(fs.existsSync(path.join(f.root, '.ecosystem/build')), false);
      assert.equal(fs.existsSync(path.join(f.root, '.ecosystem/github/evidence')), false);
    });
  }
}

test('classification fails on tool errors and malformed output', t => {
  const f = fixture(t);
  const binary = path.join(f.root, '.ecosystem/tool');
  const core = {setOutput: () => {}, info: () => {}};
  for (const script of ['exit 5', "printf 'not json'", "printf '{\"schema\":2}'"]) {
    f.write('.ecosystem/tool', '#!/bin/sh\n' + script + '\n'); fs.chmodSync(binary, 0o755);
    assert.throws(() => changes.classify({root: f.root, binary, core}));
    assert.equal(fs.existsSync(path.join(f.root, '.ecosystem/github/classification.json')), false);
  }
});

test('classification never overwrites a report or follows its alias', t => {
  const f = fixture(t);
  f.report();
  const core = {setOutput: () => {}, info: () => {}};
  const report = path.join(f.root, '.ecosystem/github/classification.json');
  fs.symlinkSync(path.join(f.root, 'docs/guide.md'), report);
  assert.throws(() => changes.classify({root: f.root, binary: engels, core}), /EEXIST/);
  assert.equal(fs.readFileSync(path.join(f.root, 'docs/guide.md'), 'utf8'), '# Guide\n');
  fs.unlinkSync(report);
  changes.classify({root: f.root, binary: engels, core});
  const before = fs.readFileSync(report, 'utf8');
  assert.throws(() => changes.classify({root: f.root, binary: engels, core}), /EEXIST/);
  assert.equal(fs.readFileSync(report, 'utf8'), before);
});

for (const [scenario, expected] of [['docs', 'cheap'], ['owned-docs', 'full'], ['source', 'full'], ['template', 'full']])
  test('generated checkout-to-native-decision pipeline: ' + scenario, t => {
    const f = fixture(t);
    let base = f.base;
    if (scenario === 'owned-docs') {
      f.manifest.artifacts[3].root = 'docs'; f.manifest.artifacts[3].owns = ['include'];
      f.write('manifest.json', JSON.stringify(f.manifest));
      f.write('docs/include/input.md', 'old\n'); base = f.commit('owned doc input');
    }
    const file = scenario === 'source' ? 'code/src/api.cpp' : scenario === 'template'
      ? 'assets/templates/build.tpl' : scenario === 'owned-docs' ? 'docs/include/input.md' : 'docs/guide.md';
    f.write(file, 'changed\n'); const head = f.commit('change');
    const context = {eventName: 'push', sha: head, ref: 'refs/heads/feature', payload: {before: base, after: head}};
    const outputs = {}, core = {setOutput: (key, value) => { outputs[key] = value; }, info() {}};
    const action = fs.readFileSync(path.join(path.dirname(path.resolve(process.argv[2])), 'action.yml'), 'utf8');
    const preliminary = program(action, 'Select preliminary execution', 4, 10);
    preliminary(() => ({preliminary: args => evidence.preliminary({...args, env: {BASE_SCOPE: 'cheap'}})}), context, core,
      {env: {EVIDENCE_MODULE: 'synthetic-adapter-path; no SDK or identity injected'}});
    assert.equal(outputs.scope, 'cheap', 'preliminary selection does not classify paths or install Qt');
    assert.equal(outputs.verified, 'false');
    const workflow = fs.readFileSync(process.argv[4], 'utf8');
    assert.ok(workflow.indexOf('Setup ecosystem tool') < workflow.indexOf('Classify checked-out changes'));
    assert.ok(workflow.indexOf('Classify checked-out changes') < workflow.indexOf('Setup project verification dependencies'));
    const adapter = name => name.endsWith('evidence.cjs')
      ? {requiredScope: args => evidence.requiredScope({...args, root: f.root})}
      : {collect: args => changes.collect({...args, root: f.root}), classify: args => changes.classify({...args, root: f.root})};
    program(workflow, 'Resolve checked-out changes')(adapter, context, core);
    // Synthetic bootstrap supplies only the actual already-built native actor,
    // never a synthetic known execution identity or a Qt installation.
    program(workflow, 'Classify checked-out changes')(adapter, context, core,
      {cwd: () => f.root, env: {ENGELS_BINARY: engels, BASE_SCOPE: 'cheap'}});
    assert.equal(outputs.scope, expected);
    assert.equal(outputs.verified, 'false');
    assert.equal(fs.existsSync(path.join(f.root, '.ecosystem/build')), false);
  });
