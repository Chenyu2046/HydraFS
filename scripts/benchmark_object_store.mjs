#!/usr/bin/env node
import crypto from 'node:crypto';
import { execFile } from 'node:child_process';
import { createReadStream } from 'node:fs';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { performance } from 'node:perf_hooks';
import { promisify } from 'node:util';
import { createAiMarkdownBlock } from './benchmark_workload.mjs';
import { summarizeBenchmarkFailures } from './benchmark_diagnostics.mjs';
import { deriveBenchmarkRoundSalt } from './benchmark_salt.mjs';
import { readDockerStats as readBoundedDockerStats } from './benchmark_docker_stats.mjs';
import { AdaptiveConcurrencyController, retryDelayMs } from '../picture_bed/src/services/concurrency_controller.mjs';

const baseUrl = process.env.HYDRA_BASE_URL || 'https://127.0.0.1';
const user = process.env.HYDRA_USER;
const token = process.env.HYDRA_TOKEN;
const workload = process.env.HYDRA_BENCHMARK_WORKLOAD || 'binary';
const partSize = Number(process.env.HYDRA_PART_SIZE || 10) * 1024 * 1024;
const defaultSizes = workload === 'ai-md' ? '320,1280' : '240,960';
const sizes = (process.env.HYDRA_BENCHMARK_SIZES || defaultSizes).split(',').map(value => Number(value.trim()) * 1024 * 1024);
const configuredMode = process.env.HYDRA_BENCHMARK_MODE || 'adaptive';
const mode = configuredMode === 'aimd' ? 'adaptive' : configuredMode;
const fixedConcurrency = Number(process.env.HYDRA_BENCHMARK_FIXED_CONCURRENCY || 8);
const initialConcurrency = mode === 'fixed' ? fixedConcurrency : Number(process.env.HYDRA_BENCHMARK_INITIAL || 8);
const minConcurrency = mode === 'fixed' ? fixedConcurrency : Number(process.env.HYDRA_BENCHMARK_MIN || 4);
const maxConcurrency = mode === 'fixed' ? fixedConcurrency : Number(process.env.HYDRA_BENCHMARK_MAX || 16);
const rounds = Number(process.env.HYDRA_BENCHMARK_ROUNDS || 5);
const benchmarkSalt = Number(process.env.HYDRA_BENCHMARK_SALT || Date.now()) >>> 0;
const requestTimeoutMs = Number(process.env.HYDRA_BENCHMARK_REQUEST_TIMEOUT_MS || 120000);
const execFileAsync = promisify(execFile);

if (!user || !token || !['binary', 'ai-md'].includes(workload) || !['fixed', 'adaptive'].includes(mode) ||
    ![partSize, fixedConcurrency, initialConcurrency, minConcurrency, maxConcurrency, rounds, requestTimeoutMs]
      .every(value => Number.isSafeInteger(value) && value > 0) ||
    minConcurrency > initialConcurrency || initialConcurrency > maxConcurrency ||
    sizes.some(size => !Number.isSafeInteger(size) || size <= 0)) {
  console.error('NOT RUN: set HYDRA_BASE_URL, HYDRA_USER, HYDRA_TOKEN, and valid HYDRA_BENCHMARK_SIZES');
  process.exit(2);
}

const percentile = (values, p) => {
  if (!values.length) return null;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.min(sorted.length - 1, Math.ceil(sorted.length * p) - 1)];
};

const retryReason = result => result.timedOut ? 'timeout'
  : result.status === 429 ? 'http_429'
    : result.status >= 500 ? `http_${result.status}`
      : result.status === 408 ? 'http_408' : result.status === 0 ? 'network' : 'unknown';

async function post(endpoint, body) {
  const response = await fetch(`${baseUrl}${endpoint}`, {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ ...body, user, token }),
    signal: AbortSignal.timeout(requestTimeoutMs),
  });
  if (!response.ok) {
    const error = new Error(`${endpoint}: HTTP ${response.status}`);
    error.status = response.status;
    error.retryAfter = response.headers.get('retry-after');
    throw error;
  }
  return response.json();
}

const readDockerStats = () => readBoundedDockerStats(execFileAsync);

function createSafetyBreaker() {
  const windowMs = Number(process.env.HYDRA_BENCHMARK_SAFETY_WINDOW_MS || 15000);
  const state = {
    enabled: process.env.HYDRA_BENCHMARK_SAFETY !== '0' && Number.isFinite(windowMs) && windowMs > 0,
    windowStarted: Date.now(), window: [], badP99Windows: 0, broken: false, reason: null,
  };
  const gatewayNames = ['tc_hydrastore_gateway_1', 'tc_hydrastore_gateway_2'];
  const trip = reason => {
    state.broken = true;
    state.reason = reason;
    throw new Error(`benchmark safety circuit broken: ${reason}`);
  };
  return {
    async check(result) {
      if (!state.enabled || state.broken) return;
      state.window.push({ ok: result.ok, timeout: result.timedOut, rtt: result.elapsedMs });
      if (Date.now() - state.windowStarted < windowMs) return;
      const window = state.window;
      state.window = [];
      state.windowStarted = Date.now();
      const errorRate = window.filter(item => !item.ok).length / Math.max(1, window.length);
      const timeoutRate = window.filter(item => item.timeout).length / Math.max(1, window.length);
      const p99 = percentile(window.map(item => item.rtt), 0.99) || 0;
      if (errorRate > 0.10) trip(`error rate ${errorRate.toFixed(3)} > 0.10`);
      if (timeoutRate > 0.05) trip(`timeout rate ${timeoutRate.toFixed(3)} > 0.05`);
      if (p99 > 30000) state.badP99Windows++;
      else state.badP99Windows = 0;
      if (state.badP99Windows >= 3) trip('P99 RTT > 30s for three windows');
      const stats = await readDockerStats();
      const memoryPressure = stats.find(item => item.memoryPercent !== null && item.memoryPercent > 90);
      if (memoryPressure) trip(`container memory > 90%: ${memoryPressure.name}`);
      try {
        const { stdout } = await execFileAsync('docker', [
          'inspect', '--format', '{{if .State.Health}}{{.State.Health.Status}}{{else}}running{{end}}', ...gatewayNames,
        ], { windowsHide: true, maxBuffer: 1024 * 1024 });
        if (stdout.trim().split(/\r?\n/).some(status => status.trim() !== 'healthy')) {
          trip('gateway health is not healthy');
        }
      } catch (error) {
        trip(`gateway health check failed: ${error.message}`);
      }
    },
    snapshot() { return { ...state, window: undefined }; },
  };
}

function startContainerSampler() {
  const state = { running: true, samples: [] };
  const loop = async () => {
    while (state.running) {
      const sample = await readDockerStats();
      if (sample.length) state.samples.push({ at: new Date().toISOString(), containers: sample });
      await new Promise(resolve => setTimeout(resolve, 500));
    }
  };
  const promise = loop();
  return {
    async stop() {
      state.running = false;
      await promise;
      const byContainer = {};
      for (const sample of state.samples) for (const item of sample.containers) {
        const values = byContainer[item.name] ||= { cpu: [], memoryPercent: [], samples: 0 };
        if (item.cpuPercent !== null) values.cpu.push(item.cpuPercent);
        if (item.memoryPercent !== null) values.memoryPercent.push(item.memoryPercent);
        values.samples += 1;
      }
      for (const values of Object.values(byContainer)) {
        values.cpuAvgPercent = values.cpu.length ? values.cpu.reduce((a, b) => a + b, 0) / values.cpu.length : null;
        values.cpuPeakPercent = values.cpu.length ? Math.max(...values.cpu) : null;
        values.memoryPeakPercent = values.memoryPercent.length ? Math.max(...values.memoryPercent) : null;
        delete values.cpu;
        delete values.memoryPercent;
      }
      return { samples: state.samples.length, byContainer };
    },
  };
}

async function collectPhysicalBytes(manifestId) {
  const container = process.env.HYDRA_DB_CONTAINER || 'tc_fcgi_mysql';
  const password = process.env.HYDRA_DB_PASSWORD;
  if (!password || !manifestId) return { status: 'NOT_COLLECTED', reason: 'set HYDRA_DB_PASSWORD' };
  const query = `SELECT COALESCE(SUM(c.size),0),COUNT(*) FROM manifest_chunk m JOIN chunk_blob c ON c.id=m.chunk_id WHERE m.manifest_id=${Number(manifestId)}`;
  try {
    const { stdout } = await execFileAsync('docker', [
      'exec', '-e', `MYSQL_PWD=${password}`, container, 'mysql', '-uroot', '-N', '-D', 'yuncunchu', '-e', query,
    ], { windowsHide: true, maxBuffer: 1024 * 1024 });
    const [bytes, chunks] = stdout.trim().split(/\s+/).map(Number);
    return { status: 'COLLECTED', bytes, chunks };
  } catch (error) {
    return { status: 'NOT_COLLECTED', reason: error.message };
  }
}

async function makeFile(filePath, size, salt) {
  const handle = await fs.open(filePath, 'w');
  const wholeHash = crypto.createHash('md5');
  const parts = [];
  let offset = 0;
  try {
    while (offset < size) {
      const partStart = offset;
      const partEnd = Math.min(size, partStart + partSize);
      const partHash = crypto.createHash('sha256');
      while (offset < partEnd) {
        const length = Math.min(1024 * 1024, partEnd - offset);
        const block = workload === 'ai-md'
          ? createAiMarkdownBlock(length, offset, salt)
          : Buffer.allocUnsafe(length);
        if (workload === 'binary') {
          for (let i = 0; i < length; i++) {
            const position = offset + i;
            const partIndex = Math.floor(position / partSize);
            const partOffset = position - partIndex * partSize;
            block[i] = partOffset < 16
              ? ((salt >>> ((partOffset % 4) * 8)) ^ partIndex ^ partOffset) & 0xff
              : (position * 31 + partIndex * 17 + salt) & 0xff;
          }
        }
        await handle.write(block);
        wholeHash.update(block);
        partHash.update(block);
        offset += length;
      }
      parts.push({ index: parts.length, start: partStart, size: partEnd - partStart, sha256: partHash.digest('hex') });
    }
  } finally {
    await handle.close();
  }
  return { contentDigest: wholeHash.digest('hex'), parts };
}

async function uploadPart(filePath, uploadId, part, traceId, attempt, reason) {
  const started = performance.now();
  try {
    const response = await fetch(
      `${baseUrl}/api/object/part?uploadId=${encodeURIComponent(uploadId)}&index=${part.index}&sha256=${part.sha256}`,
      {
        method: 'POST',
        headers: {
          'content-type': 'application/octet-stream', 'content-length': String(part.size),
          'X-Upload-User': user, 'X-Upload-Token': token,
          'X-Hydra-Trace-Id': traceId, 'X-Hydra-Attempt': String(attempt),
          'X-Hydra-Retry-Reason': reason,
        },
        body: createReadStream(filePath, { start: part.start, end: part.start + part.size - 1 }),
        duplex: 'half',
        signal: AbortSignal.timeout(requestTimeoutMs),
      },
    );
    const result = await response.json();
    return { part, traceId, attempt, reason, result, status: response.status,
      retryAfter: response.headers.get('retry-after'), ok: response.ok && result.code === 0,
      elapsedMs: performance.now() - started };
  } catch (error) {
    return { part, traceId, attempt, reason, result: { code: -1, msg: error.message }, status: error.status || 0,
      errorMessage: error.message, retryAfter: error.retryAfter, ok: false,
      timedOut: error.name === 'TimeoutError' || error.name === 'AbortError',
      elapsedMs: performance.now() - started };
  }
}

function createAimdWindow() {
  return new AdaptiveConcurrencyController({
    mode, initial: initialConcurrency, min: minConcurrency, max: maxConcurrency,
  });
}

async function uploadWithAimd(filePath, uploadId, parts, initialPending, safety) {
  const queuedAt = performance.now();
  const pending = initialPending.map(part => ({ part, queuedAt }));
  const aimdWindow = createAimdWindow();
  const successfulRtts = [];
  const allAttemptRtts = [];
  let retries = 0;
  let timeouts = 0;
  let attempts = 0;
  const chunkTraces = [];
  let inFlight = 0;
  let settled = false;
  let failureError = null;
  return new Promise((resolve, reject) => {
    const finish = () => {
      if (inFlight !== 0 || settled) return;
      settled = true;
      if (failureError) reject(failureError);
      else resolve({ retries, timeouts, attempts, finalConcurrency: aimdWindow.size,
        successfulRtts, allAttemptRtts, chunkTraces });
    };
    const launchNext = () => {
      if (settled) return;
      if (failureError) { finish(); return; }
      while (inFlight < aimdWindow.size && pending.length) {
        const pendingPart = pending.shift();
        const part = pendingPart.part;
        const traceId = `hydra-${process.pid}-${crypto.randomUUID()}`;
        inFlight++;
        uploadPartWithRetry(filePath, uploadId, part, aimdWindow, safety, traceId,
          performance.now() - pendingPart.queuedAt).then(attempt => {
          successfulRtts.push(attempt.result.elapsedMs);
          allAttemptRtts.push(...attempt.attemptRtts);
          chunkTraces.push(attempt.trace);
          attempts += attempt.attempts;
          retries += attempt.retries;
          timeouts += attempt.timeouts;
          if (retries > parts.length * 3) throw new Error(`too many part retries: ${retries}`);
        }).catch(error => {
          if (!failureError) failureError = error;
          allAttemptRtts.push(...(error.attemptRtts || []));
          if (error.trace) chunkTraces.push(error.trace);
          attempts += error.attempts || 0;
          retries += error.retries || 0;
          timeouts += error.timeouts || 0;
        }).finally(() => {
          inFlight--;
          if (failureError || !pending.length) finish();
          else launchNext();
        });
      }
      if (!pending.length) finish();
    };
    launchNext();
  });
}

async function uploadPartWithRetry(filePath, uploadId, part, aimdWindow, safety, traceId, clientQueueWaitMs) {
  const maxRetries = 3;
  let retries = 0;
  let timeouts = 0;
  const attemptRtts = [];
  const trace = { traceId, partIndex: part.index, clientQueueWaitMs, retryBackoffMs: 0,
    retryReasons: [], attempts: [] };
  for (let attempt = 0; attempt <= maxRetries; attempt++) {
    const reason = attempt === 0 ? 'initial' : trace.retryReasons.at(-1) || 'retry';
    const result = await uploadPart(filePath, uploadId, part, traceId, attempt + 1, reason);
    attemptRtts.push(result.elapsedMs);
    trace.attempts.push({ attempt: attempt + 1, requestElapsedMs: result.elapsedMs,
      status: result.status, ok: result.ok, timedOut: Boolean(result.timedOut),
      retryReason: result.ok ? reason : retryReason(result),
      errorMessage: result.errorMessage || result.result?.msg || null });
    aimdWindow.record({ success: result.ok, timeout: result.timedOut,
      status: result.status, rtt: result.elapsedMs });
    try {
      await safety.check(result);
    } catch (error) {
      error.attempts = attempt + 1;
      error.retries = retries;
      error.timeouts = timeouts + (result.timedOut ? 1 : 0);
      error.attemptRtts = attemptRtts;
      error.trace = trace;
      throw error;
    }
    if (result.ok) {
      trace.finalRequestElapsedMs = result.elapsedMs;
      trace.totalRequestElapsedMs = trace.attempts.reduce((sum, item) => sum + item.requestElapsedMs, 0);
      return { result, retries, timeouts, attempts: attempt + 1, attemptRtts, trace };
    }
    const retryable = result.timedOut || result.status === 0 || result.status === 408 ||
      result.status === 429 || result.status >= 500;
    if (!retryable) throw Object.assign(new Error(`part ${part.index} failed with HTTP ${result.status}: ${result.result?.msg || result.errorMessage || 'unknown error'}`), {
      attempts: attempt + 1, retries, timeouts, attemptRtts,
      trace, status: result.status,
    });
    if (result.timedOut) timeouts++;
    if (attempt < maxRetries) {
      retries++;
      const delayMs = retryDelayMs({ attempt, retryAfter: result.retryAfter });
      trace.retryReasons.push(retryReason(result));
      trace.retryBackoffMs += delayMs;
      await new Promise(resolve => setTimeout(resolve, delayMs));
    }
  }
  const error = new Error(`part ${part.index} failed after ${maxRetries + 1} attempts`);
  error.attempts = maxRetries + 1;
  error.retries = retries;
  error.timeouts = timeouts;
  error.attemptRtts = attemptRtts;
  error.trace = trace;
  throw error;
}

async function downloadObject(objectId, expectedDigest, expectedSize) {
  const started = performance.now();
  const response = await fetch(`${baseUrl}/api/object/download?objectId=${encodeURIComponent(objectId)}`, {
    headers: { 'X-Upload-User': user, 'X-Upload-Token': token },
    signal: AbortSignal.timeout(requestTimeoutMs),
  });
  if (!response.ok) throw new Error(`download: HTTP ${response.status}`);
  const digest = crypto.createHash('md5');
  let bytes = 0;
  for await (const chunk of response.body) { digest.update(chunk); bytes += chunk.length; }
  return { elapsedMs: performance.now() - started, bytes, valid: bytes === expectedSize && digest.digest('hex') === expectedDigest };
}

async function measure(size, directory, salt, round, safety) {
  const extension = workload === 'ai-md' ? 'md' : 'bin';
  const filePath = path.join(directory, `object-${size}-${round}-${Date.now()}.${extension}`);
  const started = performance.now();
  const metadata = await makeFile(filePath, size, salt);
  let objectId = null;
  try {
    const init = await post('/api/object/init', {
      filename: path.basename(filePath), size, md5: metadata.contentDigest, contentDigest: metadata.contentDigest,
      parts: metadata.parts.map(({ index, size: partBytes, sha256 }) => ({ index, size: partBytes, sha256 })),
    });
    if (init.code !== 0 || init.instant) throw new Error(`unexpected init result: ${JSON.stringify(init)}`);
    const pendingIndices = init.uploadableParts?.length ? init.uploadableParts : (init.missingParts || []);
    const pending = pendingIndices.map(index => metadata.parts[index]);
    const uploadStarted = performance.now();
    const upload = await uploadWithAimd(filePath, init.uploadId, metadata.parts, pending, safety);
    const uploadElapsedMs = performance.now() - uploadStarted;
    const commit = await post('/api/object/commit', { uploadId: init.uploadId });
    if (commit.code !== 0) throw new Error(`commit: ${JSON.stringify(commit)}`);
    objectId = commit.objectId;
    const physical = await collectPhysicalBytes(commit.manifestId);
    const download = await downloadObject(commit.objectId, metadata.contentDigest, size);
    if (!download.valid) throw new Error(`download digest/size mismatch: ${JSON.stringify(download)}`);
    const elapsedMs = performance.now() - started;
    return {
      round, logicalBytes: size, parts: metadata.parts.length, elapsedMs,
      uploadElapsedMs,
      endToEndThroughputMiBPerSec: size / 1024 / 1024 / (elapsedMs / 1000),
      uploadThroughputMiBPerSec: size / 1024 / 1024 / (uploadElapsedMs / 1000),
      downloadElapsedMs: download.elapsedMs,
      downloadThroughputMiBPerSec: size / 1024 / 1024 / (download.elapsedMs / 1000),
      partRttMs: { p50: percentile(upload.allAttemptRtts, 0.50), p95: percentile(upload.allAttemptRtts, 0.95), p99: percentile(upload.allAttemptRtts, 0.99), samples: upload.allAttemptRtts.length },
      partRttSamplesMs: upload.successfulRtts,
      allAttemptRttSamplesMs: upload.allAttemptRtts,
      chunkTraces: upload.chunkTraces,
      attempts: upload.attempts, retries: upload.retries, timeouts: upload.timeouts, finalConcurrency: upload.finalConcurrency,
      physicalBytes: physical,
    };
  } catch (error) {
    failureDiagnostics.push({
      round, logicalBytes: size, stage: error.trace ? 'part_upload' : 'measure',
      message: error.message, status: error.status || 0, timedOut: Boolean(error.timedOut),
      attempts: error.attempts || 0, retries: error.retries || 0, timeouts: error.timeouts || 0,
      trace: error.trace || null,
      failureSummary: error.trace ? summarizeBenchmarkFailures(error.trace.attempts) : {},
    });
    throw error;
  } finally {
    if (objectId) await post('/api/object/delete', { objectId }).catch(() => {});
    await fs.unlink(filePath).catch(() => {});
  }
}

const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'hydrastore-benchmark-'));
const runStartedAt = new Date().toISOString();
const sampler = startContainerSampler();
const safety = createSafetyBreaker();
const results = [];
const failureDiagnostics = [];
let report;
try {
  for (let round = 1; round <= rounds; round++) {
    for (const size of sizes) {
      const salt = deriveBenchmarkRoundSalt(benchmarkSalt, round, size / 1024 / 1024);
      results.push(await measure(size, directory, salt, round, safety));
    }
  }
      const summaries = sizes.map(size => {
    const rows = results.filter(result => result.logicalBytes === size);
    const values = key => rows.map(row => row[key]);
    const partRtts = rows.flatMap(row => row.allAttemptRttSamplesMs || row.partRttSamplesMs);
    return {
      logicalBytes: size,
      rounds: rows.length,
      uploadThroughputMiBPerSec: { median: percentile(values('uploadThroughputMiBPerSec'), 0.5), p5: percentile(values('uploadThroughputMiBPerSec'), 0.05) },
      endToEndThroughputMiBPerSec: { median: percentile(values('endToEndThroughputMiBPerSec'), 0.5), p5: percentile(values('endToEndThroughputMiBPerSec'), 0.05) },
      partRttMs: { p50: percentile(partRtts, 0.5), p95: percentile(partRtts, 0.95), p99: percentile(partRtts, 0.99), samples: partRtts.length },
      attempts: rows.reduce((sum, row) => sum + row.attempts, 0),
      retries: { total: rows.reduce((sum, row) => sum + row.retries, 0), timeouts: rows.reduce((sum, row) => sum + row.timeouts, 0) },
    };
  });
  const containerStats = await sampler.stop();
  report = {
    status: 'PASS',
    generatedAt: new Date().toISOString(), runStartedAt,
    config: { workload, mode, fixedConcurrency, initialConcurrency, minConcurrency, maxConcurrency, rounds, requestTimeoutMs, benchmarkSalt }, results, summaries,
    containerStats,
    safety: safety.snapshot(), failures: failureDiagnostics,
    limitations: [
      'This is a single-client run; compare mode=adaptive with mode=fixed and the configured fixed concurrency.',
      workload === 'ai-md'
        ? 'AI MD is a synthetic knowledge-base Markdown workload with prose, code blocks, tables, and links; it is not a corpus-quality embedding benchmark.'
        : 'Binary is a deterministic payload baseline, not representative of document parsing cost.',
      'Container CPU and memory are sampled through docker stats; host-level CPU, disk latency, and GC latency are not collected.',
    ],
  };
} catch (error) {
  const containerStats = await sampler.stop().catch(() => ({ samples: 0, byContainer: {} }));
  report = {
    status: safety.snapshot().broken ? 'CIRCUIT_BROKEN' : 'FAILED',
    generatedAt: new Date().toISOString(), runStartedAt,
    config: { workload, mode, fixedConcurrency, initialConcurrency, minConcurrency, maxConcurrency, rounds, requestTimeoutMs, benchmarkSalt },
    error: error.message, safety: safety.snapshot(), results, failures: failureDiagnostics, containerStats,
    failureSummary: summarizeBenchmarkFailures(failureDiagnostics.flatMap(item =>
      item.trace?.attempts?.map(attempt => ({ status: attempt.status, timedOut: attempt.timedOut,
        message: attempt.errorMessage, responseBody: attempt.errorMessage })) || [])),
    limitations: ['Partial or failed runs are diagnostic evidence only and must not be used as formal capacity results.'],
  };
} finally {
  if (sampler) await sampler.stop().catch(() => {});
  await fs.rm(directory, { recursive: true, force: true });
}

const outputPath = process.env.HYDRA_BENCHMARK_OUTPUT || path.join(os.tmpdir(), `hydrastore-benchmark-${Date.now()}.json`);
await fs.writeFile(outputPath, JSON.stringify(report, null, 2));
console.log(JSON.stringify({ ...report, rawOutput: outputPath }, null, 2));
if (report.status !== 'PASS') process.exitCode = 1;
