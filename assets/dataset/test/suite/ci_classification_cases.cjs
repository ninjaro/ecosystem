'use strict';
// Generated full-workflow programs capture native policy against their own Git checkout.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {test} = require('node:test');
const {fixture, changes, engels} = require('./ci_change_fixtures.cjs');

function program(workflow, step) {
  const marker = `      - name: ${step}\n`;
  assert.ok(workflow.includes(marker), marker);
  const block = workflow.slice(workflow.indexOf(marker) + marker.length).split('\n      - ')[0];
  const body = block.match(/          script: \|\n((?: {12}.*\n|\n)*)/);
  assert.ok(body, step);
  return new Function('require', 'context', 'core', 'process',
    body[1].split('\n').map(line => line.slice(12)).join('\n'));
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
        assert.equal(name, './.github/actions/select-manifesto-ci/changes.cjs');
        return {collect: args => changes.collect({...args, root: f.root}),
          classify: args => changes.classify({...args, root: f.root})};
      };
      resolve(requireAdapter, context, core);
      if (scenario === 'dirty') f.write('docs/guide.md', 'Changed after acquisition\n');
      const before = f.git('status', '--porcelain');
      classify(requireAdapter, context, core, {env: {ENGELS_BINARY: engels}});
      const report = JSON.parse(fs.readFileSync(outputs['classification-file']));
      const expected = ['source', 'header'].includes(scenario) ? 'affected'
        : ['docs', 'pull-request'].includes(scenario) ? 'presentation' : scenario === 'empty' ? 'none' : 'full';
      assert.equal(report.classification, expected, report.reason);
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
