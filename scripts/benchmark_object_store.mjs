#!/usr/bin/env node
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import { performance } from 'node:perf_hooks';

const baseUrl = process.env.HYDRA_BASE_URL || 'https://127.0.0.1';
const user = process.env.HYDRA_USER;
const token = process.env.HYDRA_TOKEN;
const sizes = (process.env.HYDRA_BENCHMARK_SIZES || '240,960').split(',').map(Number).map(mib => mib * 1024 * 1024);
if (!user || !token) {
  console.error('NOT RUN: set HYDRA_BASE_URL, HYDRA_USER, and HYDRA_TOKEN; no benchmark data was generated');
  process.exit(2);
}

const sha256 = data => crypto.createHash('sha256').update(data).digest('hex');
const md5 = data => crypto.createHash('md5').update(data).digest('hex');
async function post(path, body) {
  const response = await fetch(`${baseUrl}${path}`, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ ...body, user, token }) });
  if (!response.ok) throw new Error(`${path}: HTTP ${response.status}`);
  return response.json();
}

async function makeFile(path, size) {
  const handle = await fs.open(path, 'w');
  const block = Buffer.alloc(1024 * 1024, 0x5a);
  for (let remaining = size; remaining > 0; remaining -= block.length) await handle.write(block, 0, Math.min(remaining, block.length));
  await handle.close();
}

async function measure(size) {
  const path = `.hydrastore-benchmark-${size}.bin`;
  await makeFile(path, size);
  const data = await fs.readFile(path);
  const chunkSize = 10 * 1024 * 1024;
  const parts = [];
  for (let offset = 0, index = 0; offset < size; offset += chunkSize, index++) {
    const part = data.subarray(offset, Math.min(offset + chunkSize, size));
    parts.push({ index, size: part.length, sha256: sha256(part) });
  }
  const started = performance.now();
  const init = await post('/api/object/init', { filename: path, size, md5: md5(data), contentDigest: md5(data), parts });
  for (const index of init.missingParts || []) {
    const part = parts[index];
    const payload = data.subarray(index * chunkSize, index * chunkSize + part.size);
    const response = await fetch(`${baseUrl}/api/object/part?uploadId=${encodeURIComponent(init.uploadId)}&index=${index}&sha256=${part.sha256}`, { method: 'POST', headers: { 'content-type': 'application/octet-stream', 'X-Upload-User': user, 'X-Upload-Token': token }, body: payload });
    const result = await response.json();
    if (result.code !== 0) throw new Error(`part ${index}: ${JSON.stringify(result)}`);
  }
  const commit = await post('/api/object/commit', { uploadId: init.uploadId });
  if (commit.code !== 0) throw new Error(`commit: ${JSON.stringify(commit)}`);
  await fs.unlink(path);
  return { logicalBytes: size, elapsedMs: performance.now() - started, objectId: commit.objectId, physicalBytes: 'requires DB query', dedupSavedBytes: 'requires DB query' };
}

const results = [];
for (const size of sizes) results.push(await measure(size));
console.log(JSON.stringify({ generatedAt: new Date().toISOString(), results, note: 'This script reports only measured elapsed time; physical bytes, percentiles, CPU, memory, and GC latency require the integration collector.' }, null, 2));
