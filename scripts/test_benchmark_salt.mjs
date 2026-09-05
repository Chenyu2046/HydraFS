#!/usr/bin/env node
import assert from 'node:assert/strict';
import { deriveBenchmarkRoundSalt } from './benchmark_salt.mjs';

const baseSalt = 0x12345678;
const salts = [];
for (let client = 0; client < 8; client++) {
  for (let round = 1; round <= 3; round++) {
    salts.push(deriveBenchmarkRoundSalt((baseSalt + client * 2654435761) >>> 0, round, 320));
  }
}
assert.equal(new Set(salts).size, salts.length, 'multi-client rounds must not reuse payload salts');
assert.equal(deriveBenchmarkRoundSalt(baseSalt, 1, 320), deriveBenchmarkRoundSalt(baseSalt, 1, 320));
console.log('PASS: benchmark client/round salt uniqueness regression');
