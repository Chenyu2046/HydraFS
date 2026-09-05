#!/usr/bin/env node
import { execFile } from 'node:child_process';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { promisify } from 'node:util';
import { summarizeBenchmarkFailures } from './benchmark_diagnostics.mjs';

const execFileAsync = promisify(execFile);
const benchmark = path.join(import.meta.dirname, 'benchmark_object_store.mjs');
const clients = Number(process.env.HYDRA_MULTI_CLIENTS || 2);
const perClientConcurrency = Number(process.env.HYDRA_MULTI_CLIENT_CONCURRENCY || 4);
const rounds = Number(process.env.HYDRA_MULTI_ROUNDS || 5);
const size = process.env.HYDRA_MULTI_SIZE || '320';
const workload = process.env.HYDRA_MULTI_WORKLOAD || process.env.HYDRA_BENCHMARK_WORKLOAD || 'binary';
const baseSalt = Number(process.env.HYDRA_MULTI_SALT || Date.now()) >>> 0;
const breakerNamespace = process.env.HYDRA_MULTI_BREAKER_NAMESPACE || `benchmark-${baseSalt}`;
const output = process.env.HYDRA_MULTI_OUTPUT || path.join(os.tmpdir(), `hydrastore-multi-${Date.now()}.json`);
const startedAt = new Date().toISOString();

if (![clients, perClientConcurrency, rounds].every(value => Number.isSafeInteger(value) && value > 0) || !/^\d+$/.test(size)) {
  console.error('NOT RUN: invalid HYDRA_MULTI_CLIENTS, HYDRA_MULTI_CLIENT_CONCURRENCY, HYDRA_MULTI_ROUNDS, or HYDRA_MULTI_SIZE');
  process.exit(2);
}

const rawDir = await fs.mkdtemp(path.join(os.tmpdir(), 'hydrastore-multi-raw-'));
const runClient = async index => {
  const raw = path.join(rawDir, `client-${index}.json`);
  const env = {
    ...process.env,
    HYDRA_BENCHMARK_WORKLOAD: workload,
    HYDRA_BENCHMARK_MODE: 'fixed',
    HYDRA_BENCHMARK_FIXED_CONCURRENCY: String(perClientConcurrency),
    HYDRA_BENCHMARK_ROUNDS: String(rounds),
    HYDRA_BENCHMARK_SIZES: size,
    HYDRA_BENCHMARK_SALT: String((baseSalt + index * 2654435761) >>> 0),
    HYDRA_BREAKER_NAMESPACE: breakerNamespace,
    HYDRA_BENCHMARK_OUTPUT: raw,
  };
  try {
    await execFileAsync(process.execPath, [benchmark], { env, windowsHide: true, maxBuffer: 16 * 1024 * 1024 });
  } catch {
    // The raw report contains the safety circuit and partial results.
  }
  return JSON.parse(await fs.readFile(raw, 'utf8').catch(error => JSON.stringify({ status: 'NOT_COLLECTED', error: error.message })));
};

const reports = await Promise.all(Array.from({ length: clients }, (_, index) => runClient(index)));
const finishedAt = new Date().toISOString();
const minutes = Math.max(0.001, (Date.parse(finishedAt) - Date.parse(startedAt)) / 60000);
const toMiB = bytes => bytes / 1024 / 1024;
const perClient = reports.map((report, index) => {
  const rows = report.summaries || [];
  const row = rows[0] || {};
  const logicalBytes = (report.results || []).reduce((sum, item) => sum + (item.logicalBytes || 0), 0);
  return { client: index + 1, status: report.status, throughputMedian: row.uploadThroughputMiBPerSec?.median || null,
    endToEndMedian: row.endToEndThroughputMiBPerSec?.median || null, p99: row.partRttMs?.p99 || null,
    retries: row.retries?.total || 0, logicalBytes };
});
const throughputs = perClient.map(item => item.throughputMedian).filter(Number.isFinite).sort((a, b) => a - b);
const median = values => {
  if (!values.length) return null;
  const middle = Math.floor(values.length / 2);
  return values.length % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
};
const totalBytes = perClient.reduce((sum, item) => sum + item.logicalBytes, 0);
const completeThroughputs = perClient.filter(item => Number.isFinite(item.throughputMedian)).map(item => item.throughputMedian);
const fairnessRatio = completeThroughputs.length === clients ? Math.min(...completeThroughputs) / median([...completeThroughputs]) : null;
const traceIds = new Set(reports.flatMap(report => [
  ...(report.results || []).flatMap(row => (row.chunkTraces || []).map(trace => trace.traceId)),
  ...(report.failures || []).map(item => item.trace?.traceId).filter(Boolean),
]));

async function dockerTraceLog(container) {
  return execFileAsync('docker', [
    'exec', container, 'sh', '-lc',
    'cat /app/logs/storage_gateway.trace.log 2>/dev/null || true',
  ], { windowsHide: true, maxBuffer: 32 * 1024 * 1024 })
    .then(result => `${result.stdout}\n${result.stderr}`).catch(() => '');
}
const gatewayLogs = await Promise.all(['tc_hydrastore_gateway_1', 'tc_hydrastore_gateway_2'].map(dockerTraceLog));
const gatewayDistribution = Object.fromEntries(gatewayLogs.map((log, index) => [index === 0 ? 'gateway-1' : 'gateway-2',
  log.split(/\r?\n/).filter(line => line.includes('HYDRA_TRACE') && [...traceIds].some(id => line.includes(`trace_id=${id}`))).length]));
const report = {
  status: reports.every(item => item.status === 'PASS') ? 'PASS' : 'INCOMPLETE', startedAt, finishedAt,
  config: { clients, perClientConcurrency, rounds, sizeMiB: Number(size), workload, baseSalt, breakerNamespace },
  aggregate: { throughputMiBPerSec: toMiB(totalBytes) / (minutes * 60), totalLogicalBytes: totalBytes, wallMinutes: minutes },
  perClient, fairness: { minMedianRatio: fairnessRatio, completeClients: completeThroughputs.length,
    min: completeThroughputs.length ? Math.min(...completeThroughputs) : null,
    median: median([...completeThroughputs].sort((a, b) => a - b)),
    max: completeThroughputs.length ? Math.max(...completeThroughputs) : null },
  gatewayDistribution,
  failureSummary: summarizeBenchmarkFailures(reports.flatMap(report => (report.failures || []).flatMap(failure =>
    failure.trace?.attempts?.map(attempt => ({ status: attempt.status, timedOut: attempt.timedOut,
      message: attempt.errorMessage, responseBody: attempt.errorMessage })) || [failure]))),
  reports,
  limitations: ['Clients share the configured credentials unless per-process credentials are supplied; this measures independent client concurrency, not tenant isolation.'],
};
await fs.writeFile(output, JSON.stringify(report, null, 2));
console.log(JSON.stringify({ ...report, output }, null, 2));
await fs.rm(rawDir, { recursive: true, force: true });
if (report.status !== 'PASS') process.exitCode = 1;
