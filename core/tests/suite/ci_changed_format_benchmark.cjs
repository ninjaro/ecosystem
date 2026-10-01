'use strict';

// Opt-in local measurement, separate from ordinary regressions. Arguments:
// <generated changes.cjs> <engels binary>. No network or project compilation.
const os = require('node:os');
const {execFileSync} = require('node:child_process');
const {fixture, engels} = require('./ci_change_fixtures.cjs');
const cleanup = [];
const f = fixture({after: callback => cleanup.push(callback)});
const exec = (program, args, options = {}) => execFileSync(program, args,
  {cwd: f.root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], timeout: 60000, ...options});
try {
  f.write('.clang-format', 'BasedOnStyle: LLVM\n');
  const text = 'namespace {\n' + Array.from({length: 80}, (_, i) =>
    `int operation_${i}(int input) { if (input > ${i}) { return input * 2; } return input + ${i}; }\n`).join('') + '}\n';
  f.write('.ecosystem/format-sample.cpp', text);
  const formatted = exec('clang-format', ['-style=LLVM', '.ecosystem/format-sample.cpp']);
  for (let i = 0; i < 200; ++i) f.write(`other/src/spare/unit_${i}.cpp`, formatted);
  const files = f.git('ls-files').split('\n').filter(name => /\.(cpp|hpp)$/.test(name));
  exec('clang-format', ['-i', '-style=file', ...files]);
  const base = f.commit('formatted benchmark baseline');
  f.write('program/src/main.cpp', 'int main() { return 2; }\n');
  f.commit('one changed source');
  f.write('.ecosystem/github/changes.json', JSON.stringify(f.record(base)));
  const samples = {full: [], changed: []};
  const measure = mode => {
    process.stderr.write(`Measuring ${mode} formatting\n`);
    const start = process.hrtime.bigint();
    exec(engels, ['check', 'format', ...(mode === 'changed' ? ['--changes'] : [])]);
    return Number(process.hrtime.bigint() - start) / 1e6;
  };
  for (const mode of ['full', 'changed']) measure(mode);
  for (let i = 0; i < 5; ++i)
    for (const mode of i % 2 ? ['changed', 'full'] : ['full', 'changed']) samples[mode].push(measure(mode));
  const median = data => [...data].sort((a, b) => a - b)[Math.floor(data.length / 2)];
  console.log(JSON.stringify({schema: 1, measured_at: new Date().toISOString(),
    platform: `${os.platform()} ${os.arch()}`, node: process.version,
    git: exec('git', ['--version']).trim(), formatter: exec('clang-format', ['--version']).trim(),
    fixture: {owned_cpp_files: files.length + 200, changed_cpp_files: 1,
      added_translation_units: 200, functions_per_added_unit: 80},
    successful_runs: 12, samples_ms: samples,
    median_ms: {full: median(samples.full), changed: median(samples.changed)},
    limits: 'Local formatting only; includes per-command manifest/Git validation, excludes event acquisition, checkout, bootstrap and all other CI stages.'}, null, 2));
} finally {
  for (const callback of cleanup.reverse()) callback();
}
