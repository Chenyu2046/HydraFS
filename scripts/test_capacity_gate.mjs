import assert from 'node:assert/strict';
import { validateCapacityRun } from './capacity_gate.mjs';

const valid = {
  status: 'PASS',
  config: { mode: 'fixed', fixedConcurrency: 8, rounds: 2 },
  results: [{ round: 1, logicalBytes: 320 * 1024 * 1024 }, { round: 2, logicalBytes: 320 * 1024 * 1024 }],
};
assert.deepEqual(validateCapacityRun(valid, { concurrency: 8, sizesMiB: [320], rounds: 2 }), []);
assert.deepEqual(validateCapacityRun({ ...valid, results: valid.results.slice(0, 1) },
  { concurrency: 8, sizesMiB: [320], rounds: 2 }), ['round_count:320:1/2', 'missing_round:320:2']);
assert.deepEqual(validateCapacityRun({ ...valid, config: { ...valid.config, fixedConcurrency: 4 } },
  { concurrency: 8, sizesMiB: [320], rounds: 2 }), ['fixed_concurrency:4/8']);
console.log('PASS: capacity gate completeness regression');
