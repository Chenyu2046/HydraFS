#!/usr/bin/env node
import assert from 'node:assert/strict';
import crypto from 'node:crypto';

const baseUrl = process.env.HYDRA_BASE_URL || 'https://127.0.0.1';
const user = process.env.HYDRA_USER;
const token = process.env.HYDRA_TOKEN;
if (!user || !token) {
  console.error('NOT RUN: set HYDRA_BASE_URL, HYDRA_USER, and HYDRA_TOKEN for the V2 integration environment');
  process.exit(2);
}

const bytes = (size, seed = 7) => {
  const output = Buffer.alloc(size);
  for (let i = 0; i < size; i++) output[i] = (i * 31 + seed) & 0xff;
  return output;
};
const sha256 = data => crypto.createHash('sha256').update(data).digest('hex');
const md5 = data => crypto.createHash('md5').update(data).digest('hex');

async function post(path, body) {
  const response = await fetch(`${baseUrl}${path}`, {
    method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ ...body, user, token })
  });
  assert.equal(response.ok, true, `${path} HTTP ${response.status}`);
  return response.json();
}

async function init(data, extra = {}) {
  const part = { index: 0, size: data.length, sha256: sha256(data) };
  const result = await post('/api/object/init', {
    filename: 'adversarial.bin', size: data.length, md5: md5(data), contentDigest: md5(data), parts: [part], ...extra
  });
  assert.equal(result.code, 0, JSON.stringify(result));
  return { data, part, ...result };
}

async function putPart(session, payload = session.data) {
  const response = await fetch(`${baseUrl}/api/object/part?uploadId=${encodeURIComponent(session.uploadId)}&index=0&sha256=${session.part.sha256}`, {
    method: 'POST', headers: { 'content-type': 'application/octet-stream', 'X-Upload-User': user, 'X-Upload-Token': token }, body: payload
  });
  return response.json();
}

async function commit(uploadId) { return post('/api/object/commit', { uploadId }); }
async function status(uploadId) { return post('/api/object/status', { uploadId }); }

async function concurrentDedupAndDoubleCommit() {
  const data = bytes(1024, 11);
  const [a, b] = await Promise.all([init(data), init(data)]);
  const [putA, putB] = await Promise.all([putPart(a), putPart(b)]);
  assert.ok([0, 2].includes(putA.code) && [0, 2].includes(putB.code), JSON.stringify([putA, putB]));
  for (const session of [a, b]) {
    for (let i = 0; i < 20; i++) {
      const state = await status(session.uploadId);
      if ((state.missingParts || []).length === 0 && (state.waitingParts || []).length === 0) break;
      await new Promise(resolve => setTimeout(resolve, 100));
    }
  }
  const [first, second] = await Promise.all([commit(a.uploadId), commit(a.uploadId)]);
  assert.equal(first.code, 0, JSON.stringify(first));
  assert.equal(second.code, 0, JSON.stringify(second));
  assert.equal(first.objectId, second.objectId, 'duplicate commit produced different object');
  const other = await commit(b.uploadId);
  assert.equal(other.code, 0, JSON.stringify(other));
  assert.equal(other.objectId, first.objectId, 'same content did not reuse manifest');
}

async function wrongPayloadAndRetry() {
  const data = bytes(2048, 19);
  const session = await init(data);
  const wrong = await putPart(session, bytes(data.length, 23));
  assert.notEqual(wrong.code, 0, 'same-size wrong SHA payload was accepted');
  const retry = await putPart(session, data);
  assert.equal(retry.code, 0, JSON.stringify(retry));
  const duplicate = await putPart(session, data);
  assert.equal(duplicate.code, 0, JSON.stringify(duplicate));
}

async function incompleteCommit() {
  const data = bytes(512, 29);
  const session = await init(data);
  const result = await commit(session.uploadId);
  assert.notEqual(result.code, 0, 'commit exposed an incomplete session');
}

async function run() {
  await concurrentDedupAndDoubleCommit();
  await wrongPayloadAndRetry();
  await incompleteCommit();
  console.log('PASS: concurrent dedup, duplicate commit, wrong-payload rejection, retry, incomplete commit');
  console.log('PENDING: shared-delete/GC race, crash failpoints, gateway failover, and multi-storage require the fault-injection Compose environment');
}

run().catch(error => { console.error('FAIL:', error.stack || error); process.exitCode = 1; });
