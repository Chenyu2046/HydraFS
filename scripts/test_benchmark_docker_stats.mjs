#!/usr/bin/env node
import assert from 'node:assert/strict';
import { readDockerStats } from './benchmark_docker_stats.mjs';

let seenOptions;
const stats = await readDockerStats(
  async (_command, _args, options) => {
    seenOptions = options;
    throw new Error('synthetic docker stats failure');
  }, { timeoutMs: 75 }
);
assert.deepEqual(stats, []);
assert.equal(seenOptions.timeout, 75, 'docker stats must have a bounded subprocess timeout');
console.log('PASS: docker stats sampler timeout regression');
