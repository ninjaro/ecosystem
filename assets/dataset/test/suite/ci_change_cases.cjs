'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const {test} = require('node:test');
const changes = require(path.resolve(process.argv[2]));

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'manifesto-ci-changes-'));
  t.after(() => fs.rmSync(root, {recursive: true, force: true}));
  const git = (...args) => execFileSync('git', ['-C', root, '-c', 'commit.gpgsign=false', ...args],
    {encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe']}).trim();
  const write = (name, data = name + '\n') => {
    fs.mkdirSync(path.dirname(path.join(root, name)), {recursive: true});
    fs.writeFileSync(path.join(root, name), data);
  };
  const commit = message => {
    git('add', '-A', '--');
    git('commit', '-qm', message);
    return git('rev-parse', 'HEAD');
  };
  git('init', '-q');
  git('config', 'user.name', 'Fixture');
  git('config', 'user.email', 'fixture@example.invalid');
  write('.gitignore', '.ecosystem/\n');
  write('src/main.cpp', 'int main() { return 0; }\n');
  write('docs/start.md', 'Original documentation\n');
  const base = commit('base');
  const push = (before = base, head = git('rev-parse', 'HEAD')) => ({
    eventName: 'push', ref: 'refs/heads/main', sha: head, payload: {before, after: head},
  });
  const pr = (baseSha, head, checkout = head) => ({eventName: 'pull_request',
    ref: 'refs/pull/42/merge', sha: checkout,
    payload: {pull_request: {base: {sha: baseSha}, head: {sha: head}}},
  });
  const resolve = context => changes.resolve({root, context});
  return {root, git, write, commit, base, push, pr, resolve};
}

function incomplete(result, reason) {
  assert.equal(result.complete, false);
  assert.equal(result.reason, reason);
  assert.deepEqual(result.changes, [], 'no partial diff may look trustworthy');
  assert.deepEqual(result.paths, []);
}

test('push resolves the entire before-to-current range, including earlier commits', t => {
  const f = fixture(t);
  f.write('src/main.cpp', 'int main() { return 1; }\n');
  f.commit('first pushed commit');
  f.write('docs/start.md', 'Updated documentation\n');
  const head = f.commit('second pushed commit');
  const result = f.resolve(f.push());
  assert.equal(result.complete, true);
  assert.equal(result.reason, 'push-before-to-head');
  assert.equal(result.checkout, head);
  assert.equal(result.event_commit, head);
  assert.equal(result.head, head);
  assert.equal(result.base, f.base);
  assert.equal(result.comparison_base, f.base);
  assert.deepEqual(result.changes, [{status: 'M', path: 'docs/start.md'}, {status: 'M', path: 'src/main.cpp'}]);
});

test('rename, deletion, type changes and unusual filenames survive NUL transport', t => {
  const f = fixture(t);
  f.write('rename-me.cpp', Array.from({length: 20}, (_, i) => `// Unique source line ${i}\n`).join(''));
  const base = f.commit('rename candidate');
  f.git('mv', '--', 'rename-me.cpp', 'renamed.cpp');
  fs.appendFileSync(path.join(f.root, 'renamed.cpp'), '// Slightly edited after rename\n');
  f.git('rm', '--', 'src/main.cpp');
  fs.unlinkSync(path.join(f.root, 'docs/start.md'));
  fs.symlinkSync('../renamed.cpp', path.join(f.root, 'docs/start.md'));
  const names = ['docs/space name.md', 'docs/tab\tname.md', 'docs/new\nline.md',
    'docs/quote"back\\slash.md', '--leading-option.cpp', 'docs/\uFEFFbom.md', 'docs/日本語.md'];
  for (const name of names) f.write(name);
  f.commit('rename, delete, type change and add');
  // User diff settings must not turn file transport into external commands or
  // quoted/relative output. The adapter explicitly disables external diffs.
  f.git('config', 'diff.external', 'this-program-must-not-run');
  f.git('config', 'diff.relative', 'true');
  const result = f.resolve(f.push(base));
  assert.equal(result.complete, true, result.reason);
  assert.deepEqual(result.paths, [...names, 'docs/start.md', 'src/main.cpp', 'rename-me.cpp', 'renamed.cpp'].sort());
  assert.ok(result.changes.some(c => c.status === 'R' && c.previous_path === 'rename-me.cpp' && c.path === 'renamed.cpp'));
  assert.ok(result.changes.some(c => c.status === 'D' && c.path === 'src/main.cpp'));
  assert.ok(result.changes.some(c => c.status === 'T' && c.path === 'docs/start.md'));
  assert.equal(result.changes.filter(c => c.status === 'A').length, names.length);
});

for (const checkout of ['merge', 'head']) test(`PR ${checkout} checkout uses unique merge-base, excluding base-only edits`, t => {
  const f = fixture(t);
  f.write('src/main.cpp', 'int main() { return 1; }\n');
  const head = f.commit('PR change');
  f.git('checkout', '-q', '--detach', f.base);
  f.write('docs/start.md', 'Base branch advanced\n');
  const base = f.commit('base branch change');
  f.git('merge', '--no-ff', '-qm', 'synthetic merge', head);
  const merge = f.git('rev-parse', 'HEAD');
  if (checkout === 'head') f.git('checkout', '-q', '--detach', head);
  const result = f.resolve(f.pr(base, head, checkout === 'merge' ? merge : head));
  assert.equal(result.complete, true, result.reason);
  assert.equal(result.reason, 'pull-request-merge-base');
  assert.equal(result.base, base);
  assert.equal(result.comparison_base, f.base);
  assert.equal(result.head, head);
  assert.deepEqual(result.changes, [{status: 'M', path: 'src/main.cpp'}]);
});

test('an established empty delta is distinct from unavailable history', t => {
  const f = fixture(t);
  const result = f.resolve(f.push());
  assert.equal(result.complete, true);
  assert.deepEqual(result.paths, []);
});

for (const [eventName, reason] of [['workflow_dispatch', 'manual-baseline'],
  ['schedule', 'scheduled-baseline'], ['release', 'unsupported-event']]) {
  test(`${eventName} never invents a trusted delta`, t => {
    const f = fixture(t);
    incomplete(f.resolve({...f.push(), eventName}), reason);
  });
}

for (const [before, reason] of [[undefined, 'invalid-base'], ['0'.repeat(40), 'zero-base'],
  ['HEAD~1', 'invalid-base'], ['a'.repeat(40), 'history-unavailable']]) {
  test(`push fallback: ${reason} (${String(before).slice(0, 8)})`, t => {
    const f = fixture(t);
    const context = f.push();
    context.payload.before = before;
    incomplete(f.resolve(context), reason);
  });
}

test('mismatched event, checkout, push head, PR head and PR merge parents are rejected', t => {
  const f = fixture(t);
  f.write('src/main.cpp', 'int main() { return 1; }\n');
  const head = f.commit('head');
  incomplete(f.resolve({...f.push(), sha: 'main'}), 'invalid-event-commit');
  incomplete(f.resolve({...f.push(), sha: f.base}), 'checkout-mismatch');
  const push = f.push();
  push.payload.after = f.base;
  incomplete(f.resolve(push), 'push-head-mismatch');
  incomplete(f.resolve(f.pr(f.base, undefined, head)), 'invalid-pr-commits');
  incomplete(f.resolve(f.pr(f.base, 'a'.repeat(40), head)), 'history-unavailable');
  incomplete(f.resolve(f.pr(head, f.base, head)), 'pr-checkout-mismatch');
  const tree = f.git('rev-parse', head + '^{tree}');
  const staleMerge = f.git('commit-tree', tree, '-p', f.base, '-p', head, '-m', 'stale merge');
  f.git('checkout', '-q', '--detach', staleMerge);
  incomplete(f.resolve(f.pr(head, head, staleMerge)), 'pr-checkout-mismatch');
});

test('forced, non-fast-forward, deleted and tag pushes cannot narrow verification', t => {
  const f = fixture(t);
  const tree = f.git('rev-parse', 'HEAD^{tree}');
  const unrelated = f.git('commit-tree', tree, '-m', 'unrelated history');
  incomplete(f.resolve(f.push(unrelated)), 'rewritten-history');
  const forced = f.push(); forced.payload.forced = true;
  incomplete(f.resolve(forced), 'rewritten-history');
  const deleted = f.push(); deleted.payload.deleted = true;
  incomplete(f.resolve(deleted), 'deleted-ref');
  incomplete(f.resolve({...f.push(), ref: 'refs/tags/v1'}), 'unsupported-push-ref');
});

test('event SHAs must identify commits directly, not annotated tag objects', t => {
  const f = fixture(t);
  f.git('-c', 'tag.gpgsign=false', 'tag', '-a', '-m', 'tag message', 'v1');
  const tag = f.git('rev-parse', 'refs/tags/v1');
  incomplete(f.resolve(f.push(tag)), 'invalid-base');
  incomplete(f.resolve(f.pr(tag, f.base)), 'invalid-pr-commits');
});

test('unrelated and criss-cross PR histories have no trusted comparison base', t => {
  const f = fixture(t);
  const tree = f.git('rev-parse', 'HEAD^{tree}');
  const unrelated = f.git('commit-tree', tree, '-m', 'unrelated history');
  incomplete(f.resolve(f.pr(unrelated, f.base)), 'merge-base-unavailable');
  const a = f.git('commit-tree', tree, '-p', f.base, '-m', 'branch A');
  const b = f.git('commit-tree', tree, '-p', f.base, '-m', 'branch B');
  const ab = f.git('commit-tree', tree, '-p', a, '-p', b, '-m', 'merge AB');
  const ba = f.git('commit-tree', tree, '-p', b, '-p', a, '-m', 'merge BA');
  f.git('checkout', '-q', '--detach', ba);
  incomplete(f.resolve(f.pr(ab, ba)), 'ambiguous-merge-base');
});

test('a shallow checkout returns a conservative record without fetching', t => {
  const f = fixture(t);
  f.write('next.cpp'); f.commit('next');
  const context = f.push();
  const shallow = path.join(f.root, 'shallow');
  f.git('clone', '-q', '--depth=1', 'file://' + f.root, shallow);
  incomplete(changes.resolve({root: shallow, context}), 'shallow-history');
});

for (const dirty of ['modified', 'staged', 'deleted', 'untracked']) test(`a ${dirty} checkout cannot narrow verification`, t => {
  const f = fixture(t);
  if (dirty === 'deleted') fs.unlinkSync(path.join(f.root, 'src/main.cpp'));
  else f.write(dirty === 'untracked' ? 'new.cpp' : 'src/main.cpp', 'changed\n');
  if (dirty === 'staged') f.git('add', '-A');
  incomplete(f.resolve(f.push()), 'dirty-checkout');
});

test('Git replacement objects cannot substitute event history', t => {
  const f = fixture(t);
  f.write('src/main.cpp', 'int main() { return 1; }\n');
  const head = f.commit('actual event');
  const tree = f.git('rev-parse', f.base + '^{tree}');
  const replacement = f.git('commit-tree', tree, '-p', f.base, '-m', 'replacement');
  f.git('replace', head, replacement);
  const result = f.resolve(f.push());
  assert.equal(result.complete, true, result.reason);
  assert.deepEqual(result.paths, ['src/main.cpp']);
});

test('subdirectory invocation cannot silently omit sibling changes', t => {
  const f = fixture(t);
  incomplete(changes.resolve({root: path.join(f.root, 'src'), context: f.push()}), 'checkout-not-root');
});

test('invalid UTF-8 filenames do not become replacement-character paths', t => {
  const f = fixture(t);
  fs.writeFileSync(Buffer.concat([Buffer.from(f.root + '/'), Buffer.from([0xff]), Buffer.from('.cpp')]), 'source\n');
  f.commit('non-UTF8 filename');
  incomplete(f.resolve(f.push()), 'non-utf8-path');
});

for (const [count, long, reason] of [[2001, false, 'too-many-paths'], [400, true, 'change-record-too-large']]) {
  test(`oversized deltas fall back completely: ${reason}`, t => {
    const f = fixture(t);
    for (let i = 0; i < count; ++i) f.write(`docs/${i}-${long ? 'x'.repeat(180) : 'short'}.md`);
    f.commit('many paths');
    incomplete(f.resolve(f.push()), reason);
  });
}

test('collection publishes identical structured outputs and an ignored JSON report, without changing scope', t => {
  const f = fixture(t);
  f.write('new.cpp'); f.commit('feature');
  const outputs = {};
  const notices = [];
  const core = {setOutput: (name, value) => { outputs[name] = value; }, info: value => notices.push(value)};
  const context = f.push();
  const result = changes.collect({root: f.root, context, core});
  assert.equal(result.complete, true);
  assert.deepEqual(JSON.parse(outputs.changes), result);
  assert.deepEqual(JSON.parse(fs.readFileSync(outputs['changes-file'], 'utf8')), result);
  assert.equal(outputs.scope, undefined, 'acquisition alone cannot narrow check policy');
  assert.equal(f.git('status', '--porcelain'), '');
  assert.match(notices[0], /Resolved 1 changed paths/);
  assert.throws(() => changes.collect({root: f.root, context, core}), /EEXIST/);
});

test('incomplete collection emits an explicit reason without raw Git or path output', t => {
  const f = fixture(t);
  const outputs = {};
  const notices = [];
  const core = {setOutput: (name, value) => { outputs[name] = value; }, info: value => notices.push(value)};
  const result = changes.collect({root: f.root, context: f.push('a'.repeat(40)), core});
  incomplete(JSON.parse(outputs.changes), 'history-unavailable');
  assert.equal(result.complete, false);
  assert.match(notices[0], /verification cannot narrow/);
});

for (const part of ['.ecosystem', '.ecosystem/github']) test(`report writing rejects a symlink at ${part}`, t => {
  const f = fixture(t);
  const destination = fs.mkdtempSync(path.join(os.tmpdir(), 'manifesto-ci-changes-target-'));
  t.after(() => fs.rmSync(destination, {recursive: true, force: true}));
  const link = path.join(f.root, part);
  fs.mkdirSync(path.dirname(link), {recursive: true});
  fs.symlinkSync(destination, link);
  assert.throws(() => changes.collect({root: f.root, context: f.push(), core: {}}), /unsafe-change-report-directory/);
  assert.deepEqual(fs.readdirSync(destination), []);
});
