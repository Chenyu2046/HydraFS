#!/usr/bin/env node
import { execFile } from 'node:child_process';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { promisify } from 'node:util';
import { validateCapacityRun } from './capacity_gate.mjs';

const execFileAsync = promisify(execFile);
const benchmark = path.join(import.meta.dirname, 'benchmark_object_store.mjs');
const workload = process.env.HYDRA_BENCHMARK_WORKLOAD || 'ai-md';
const sizes = (process.env.HYDRA_SWEEP_SIZES || '320,1280').split(',').map(value => value.trim()).filter(Boolean);
const concurrency = (process.env.HYDRA_SWEEP_CONCURRENCY || '2,4,8,12,16,24').split(',').map(Number);
const rounds = Number(process.env.HYDRA_SWEEP_ROUNDS || 5);
const sweepSalt = process.env.HYDRA_SWEEP_SALT || '20260905';
const output = process.env.HYDRA_SWEEP_OUTPUT || path.join(os.tmpdir(), `hydrastore-capacity-sweep-${Date.now()}.json`);

if (!concurrency.every(value => Number.isSafeInteger(value) && value > 0) ||
    !sizes.length || !Number.isSafeInteger(rounds) || rounds < 5) {
  console.error('NOT RUN: invalid HYDRA_SWEEP_CONCURRENCY, HYDRA_SWEEP_SIZES, or HYDRA_SWEEP_ROUNDS');
  process.exit(2);
}

const runs = [];
try {
  for (const fixedConcurrency of concurrency) {
    const rawOutput = path.join(os.tmpdir(), `hydrastore-sweep-${fixedConcurrency}-${Date.now()}.json`);
    const env = {
      ...process.env,
      HYDRA_BENCHMARK_WORKLOAD: workload,
      HYDRA_BENCHMARK_MODE: 'fixed',
      HYDRA_BENCHMARK_FIXED_CONCURRENCY: String(fixedConcurrency),
      HYDRA_BENCHMARK_ROUNDS: String(rounds),
      HYDRA_BENCHMARK_SIZES: sizes.join(','),
      HYDRA_BENCHMARK_SALT: sweepSalt,
      HYDRA_BENCHMARK_OUTPUT: rawOutput,
    };
    try {
      await execFileAsync(process.execPath, [benchmark], { env, windowsHide: true, maxBuffer: 16 * 1024 * 1024 });
    } catch (error) {
      const diagnostic = await fs.readFile(rawOutput, 'utf8').catch(() => JSON.stringify({ status: 'NOT_COLLECTED', error: error.message }));
      runs.push(JSON.parse(diagnostic));
      throw new Error(`capacity sweep stopped at fixed concurrency ${fixedConcurrency}`);
    }
    const run = JSON.parse(await fs.readFile(rawOutput, 'utf8'));
    const gateErrors = validateCapacityRun(run, {
      concurrency: fixedConcurrency,
      sizesMiB: sizes.map(Number),
      rounds,
    });
    runs.push(run);
    if (gateErrors.length) throw new Error(
      `capacity run ${fixedConcurrency}x${sizes.join(',')} incomplete: ${gateErrors.join(', ')}`);
  }
  const rows = runs.flatMap(run => (run.summaries || []).map(summary => ({
    fixedConcurrency: run.config.fixedConcurrency,
    logicalBytes: summary.logicalBytes,
    uploadMedian: summary.uploadThroughputMiBPerSec?.median,
    partP99: summary.partRttMs?.p99,
    retries: summary.retries?.total,
    timeouts: summary.retries?.timeouts,
    resource: run.containerStats?.byContainer || null,
    status: run.status,
  })));
  const validRows = rows.filter(row => row.status === 'PASS' && Number.isFinite(row.uploadMedian) &&
    Number.isFinite(row.partP99) && row.retries === 0 && row.timeouts === 0);
  const minP99BySize = new Map();
  for (const row of validRows) {
    const current = minP99BySize.get(row.logicalBytes);
    if (current === undefined || row.partP99 < current) minP99BySize.set(row.logicalBytes, row.partP99);
  }
  const bestBySize = new Map();
  for (const row of validRows) {
    const current = bestBySize.get(row.logicalBytes);
    if (!current || row.uploadMedian > current.uploadMedian) bestBySize.set(row.logicalBytes, row);
  }
  const sweetSpots = rows.filter(row => {
    const best = bestBySize.get(row.logicalBytes);
    return row.status === 'PASS' && row.retries === 0 && row.timeouts === 0 && row.partP99 <= 1.5 * (minP99BySize.get(row.logicalBytes) || Infinity) &&
      row.uploadMedian >= 0.95 * (best?.uploadMedian || 0);
  });
  const report = { status: validRows.length === rows.length && validRows.length > 0 ? 'PASS' : 'INCOMPLETE', generatedAt: new Date().toISOString(), config: { workload, sizes, concurrency, rounds, sweepSalt }, runs, rows, sweetSpots };
  await fs.writeFile(output, JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ ...report, rawOutput: output }, null, 2));
  if (report.status !== 'PASS') process.exitCode = 1;
} catch (error) {
  const report = { status: 'INCOMPLETE', generatedAt: new Date().toISOString(), config: { workload, sizes, concurrency, rounds }, error: error.message, runs };
  await fs.writeFile(output, JSON.stringify(report, null, 2));
  console.error(JSON.stringify({ ...report, rawOutput: output }, null, 2));
  process.exitCode = 1;
}
