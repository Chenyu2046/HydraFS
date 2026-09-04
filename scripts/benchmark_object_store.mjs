#!/usr/bin/env node
import crypto from 'node:crypto';
import { execFile } from 'node:child_process';
import { createReadStream } from 'node:fs';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { performance } from 'node:perf_hooks';
import { promisify } from 'node:util';

const baseUrl = process.env.HYDRA_BASE_URL || 'https://127.0.0.1';
const user = process.env.HYDRA_USER;
const token = process.env.HYDRA_TOKEN;
const partSize = Number(process.env.HYDRA_PART_SIZE || 10) * 1024 * 1024;
const sizes = (process.env.HYDRA_BENCHMARK_SIZES || '240,960').split(',').map(value => Number(value.trim()) * 1024 * 1024);
const initialConcurrency = Number(process.env.HYDRA_BENCHMARK_INITIAL || 4);
const minConcurrency = Number(process.env.HYDRA_BENCHMARK_MIN || 4);
const maxConcurrency = Number(process.env.HYDRA_BENCHMARK_MAX || 32);
const benchmarkSalt = Number(process.env.HYDRA_BENCHMARK_SALT || Date.now()) >>> 0;
const requestTimeoutMs = Number(process.env.HYDRA_BENCHMARK_REQUEST_TIMEOUT_MS || 120000);
const execFileAsync = promisify(execFile);

if (!user || !token || sizes.some(size => !Number.isSafeInteger(size) || size <= 0)) {
  console.error('NOT RUN: set HYDRA_BASE_URL, HYDRA_USER, HYDRA_TOKEN, and valid HYDRA_BENCHMARK_SIZES');
  process.exit(2);
}

const percentile = (values, p) => {
  if (!values.length) return null;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.min(sorted.length - 1, Math.ceil(sorted.length * p) - 1)];
};

async function post(endpoint, body) {
  const response = await fetch(`${baseUrl}${endpoint}`, {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ ...body, user, token }),
  });
  if (!response.ok) throw new Error(`${endpoint}: HTTP ${response.status}`);
  return response.json();
}

function parseMetricNumber(value) {
  const match = String(value).replace(',', '.').match(/[0-9]+(?:\.[0-9]+)?/);
  return match ? Number(match[0]) : null;
}

async function readDockerStats() {
  try {
    const { stdout } = await execFileAsync('docker', [
      'stats', '--no-stream', '--format', '{{.Name}}\\t{{.CPUPerc}}\\t{{.MemUsage}}\\t{{.MemPerc}}',
    ], { windowsHide: true, maxBuffer: 1024 * 1024 });
    return stdout.trim().split(/\r?\n/).filter(Boolean).map(line => {
      const [name, cpu, memory, memoryPercent] = line.split('\t');
      return { name, cpuPercent: parseMetricNumber(cpu), memory, memoryPercent: parseMetricNumber(memoryPercent) };
    });
  } catch {
    return [];
  }
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

async function makeFile(filePath, size) {
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
        const block = Buffer.allocUnsafe(length);
        for (let i = 0; i < length; i++) {
          const position = offset + i;
          const partIndex = Math.floor(position / partSize);
          const partOffset = position - partIndex * partSize;
          block[i] = partOffset < 16
            ? ((benchmarkSalt >>> ((partOffset % 4) * 8)) ^ partIndex ^ partOffset) & 0xff
            : (position * 31 + partIndex * 17 + benchmarkSalt) & 0xff;
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

async function uploadPart(filePath, uploadId, part) {
  const started = performance.now();
  try {
    const response = await fetch(
      `${baseUrl}/api/object/part?uploadId=${encodeURIComponent(uploadId)}&index=${part.index}&sha256=${part.sha256}`,
      {
        method: 'POST',
        headers: {
          'content-type': 'application/octet-stream', 'content-length': String(part.size),
          'X-Upload-User': user, 'X-Upload-Token': token,
        },
        body: createReadStream(filePath, { start: part.start, end: part.start + part.size - 1 }),
        duplex: 'half',
        signal: AbortSignal.timeout(requestTimeoutMs),
      },
    );
    const result = await response.json();
    return { part, result, ok: response.ok && result.code === 0, elapsedMs: performance.now() - started };
  } catch (error) {
    return { part, result: { code: -1, msg: error.message }, ok: false,
      timedOut: error.name === 'TimeoutError' || error.name === 'AbortError',
      elapsedMs: performance.now() - started };
  }
}

async function uploadWithAimd(filePath, uploadId, parts, initialPending) {
  const pending = [...initialPending];
  const successfulRtts = [];
  let concurrency = initialConcurrency;
  let retries = 0;
  let timeouts = 0;
  while (pending.length) {
    const batch = pending.splice(0, concurrency);
    const results = [];
    let cursor = 0;
    const workers = Array.from({ length: Math.min(concurrency, batch.length) }, async () => {
      while (cursor < batch.length) results.push(await uploadPart(filePath, uploadId, batch[cursor++]));
    });
    await Promise.all(workers);
    const failed = results.filter(result => !result.ok);
    timeouts += results.filter(result => result.timedOut).length;
    for (const result of results) if (result.ok) successfulRtts.push(result.elapsedMs);
    if (failed.length) {
      retries += failed.length;
      pending.unshift(...failed.map(result => result.part));
      concurrency = Math.max(minConcurrency, Math.floor(concurrency / 2));
      if (retries > parts.length * 3) throw new Error(`too many part retries: ${retries}`);
    } else {
      concurrency = Math.min(maxConcurrency, concurrency + 1);
    }
  }
  return { retries, timeouts, finalConcurrency: concurrency, successfulRtts };
}

async function downloadObject(objectId, expectedDigest, expectedSize) {
  const started = performance.now();
  const response = await fetch(`${baseUrl}/api/object/download?objectId=${encodeURIComponent(objectId)}`, {
    headers: { 'X-Upload-User': user, 'X-Upload-Token': token },
  });
  if (!response.ok) throw new Error(`download: HTTP ${response.status}`);
  const digest = crypto.createHash('md5');
  let bytes = 0;
  for await (const chunk of response.body) { digest.update(chunk); bytes += chunk.length; }
  return { elapsedMs: performance.now() - started, bytes, valid: bytes === expectedSize && digest.digest('hex') === expectedDigest };
}

async function measure(size, directory) {
  const filePath = path.join(directory, `object-${size}-${Date.now()}.bin`);
  const started = performance.now();
  const metadata = await makeFile(filePath, size);
  try {
    const init = await post('/api/object/init', {
      filename: path.basename(filePath), size, md5: metadata.contentDigest, contentDigest: metadata.contentDigest,
      parts: metadata.parts.map(({ index, size: partBytes, sha256 }) => ({ index, size: partBytes, sha256 })),
    });
    if (init.code !== 0 || init.instant) throw new Error(`unexpected init result: ${JSON.stringify(init)}`);
    const pendingIndices = init.uploadableParts?.length ? init.uploadableParts : (init.missingParts || []);
    const pending = pendingIndices.map(index => metadata.parts[index]);
    const uploadStarted = performance.now();
    const upload = await uploadWithAimd(filePath, init.uploadId, metadata.parts, pending);
    const uploadElapsedMs = performance.now() - uploadStarted;
    const commit = await post('/api/object/commit', { uploadId: init.uploadId });
    if (commit.code !== 0) throw new Error(`commit: ${JSON.stringify(commit)}`);
    const physical = await collectPhysicalBytes(commit.manifestId);
    const download = await downloadObject(commit.objectId, metadata.contentDigest, size);
    if (!download.valid) throw new Error(`download digest/size mismatch: ${JSON.stringify(download)}`);
    const elapsedMs = performance.now() - started;
    return {
      logicalBytes: size, parts: metadata.parts.length, elapsedMs,
      uploadElapsedMs,
      endToEndThroughputMiBPerSec: size / 1024 / 1024 / (elapsedMs / 1000),
      uploadThroughputMiBPerSec: size / 1024 / 1024 / (uploadElapsedMs / 1000),
      downloadElapsedMs: download.elapsedMs,
      downloadThroughputMiBPerSec: size / 1024 / 1024 / (download.elapsedMs / 1000),
      partRttMs: { p50: percentile(upload.successfulRtts, 0.50), p95: percentile(upload.successfulRtts, 0.95), p99: percentile(upload.successfulRtts, 0.99), samples: upload.successfulRtts.length },
      retries: upload.retries, timeouts: upload.timeouts, finalConcurrency: upload.finalConcurrency,
      physicalBytes: physical,
    };
  } finally {
    await fs.unlink(filePath).catch(() => {});
  }
}

const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'hydrastore-benchmark-'));
const sampler = startContainerSampler();
try {
  const results = [];
  for (const size of sizes) results.push(await measure(size, directory));
  const containerStats = await sampler.stop();
  console.log(JSON.stringify({
    generatedAt: new Date().toISOString(), config: { partSize, initialConcurrency, minConcurrency, maxConcurrency, requestTimeoutMs, benchmarkSalt }, results,
    containerStats,
    limitations: [
      'This is a single-client AIMD run; compare with a fixed-window run using INITIAL=MIN=MAX.',
      'Container CPU and memory are sampled through docker stats; host-level CPU, disk latency, and GC latency are not collected.',
    ],
  }, null, 2));
} finally {
  if (sampler) await sampler.stop().catch(() => {});
  await fs.rm(directory, { recursive: true, force: true });
}
