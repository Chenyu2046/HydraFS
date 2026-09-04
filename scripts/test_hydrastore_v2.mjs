#!/usr/bin/env node
import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import crypto from 'node:crypto';
import { promisify } from 'node:util';

const baseUrl = process.env.HYDRA_BASE_URL || 'https://127.0.0.1';
const user = process.env.HYDRA_USER;
const token = process.env.HYDRA_TOKEN;
const secondUser = process.env.HYDRA_SECOND_USER;
const secondToken = process.env.HYDRA_SECOND_TOKEN;
const execFileAsync = promisify(execFile);
if (!user || !token) {
  console.error('NOT RUN: set HYDRA_BASE_URL, HYDRA_USER, and HYDRA_TOKEN for the V2 integration environment');
  process.exit(2);
}

const bytes = (size, seed = 7) => {
  const output = Buffer.alloc(size);
  const runSalt = Number(process.env.HYDRA_TEST_SALT || Date.now());
  for (let i = 0; i < size; i++) output[i] = (i * 31 + seed + runSalt) & 0xff;
  return output;
};
const sha256 = data => crypto.createHash('sha256').update(data).digest('hex');
const md5 = data => crypto.createHash('md5').update(data).digest('hex');

async function post(path, body) {
  return postAs(path, body, user, token);
}

async function postAs(path, body, requestUser, requestToken) {
  const response = await fetch(`${baseUrl}${path}`, {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ ...body, user: requestUser, token: requestToken }),
    signal: AbortSignal.timeout(Number(process.env.HYDRA_REQUEST_TIMEOUT_MS || 30000))
  });
  assert.equal(response.ok, true, `${path} HTTP ${response.status}`);
  return response.json();
}

async function dbExec(sql) {
  assert.ok(process.env.HYDRA_DB_PASSWORD, 'HYDRA_DB_PASSWORD is required for DB race tests');
  await execFileAsync('docker', [
    'exec', '-e', `MYSQL_PWD=${process.env.HYDRA_DB_PASSWORD}`, 'tc_fcgi_mysql',
    'mysql', '-uroot', '-N', '-D', 'yuncunchu', '-e', sql,
  ], { windowsHide: true, maxBuffer: 1024 * 1024 });
}

async function initAs(data, requestUser, requestToken, extra = {}) {
  const part = { index: 0, size: data.length, sha256: sha256(data) };
  const result = await postAs('/api/object/init', {
    filename: 'adversarial.bin', size: data.length, md5: md5(data), contentDigest: md5(data), parts: [part], ...extra
  }, requestUser, requestToken);
  assert.equal(result.code, 0, JSON.stringify(result));
  return { data, part, ...result };
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

async function sameSessionConcurrentPart() {
  const data = bytes(4096, 37);
  const session = await init(data, { uploadId: `same-session-${Date.now()}` });
  const results = await Promise.all([putPart(session), putPart(session)]);
  assert.equal(results.filter(result => result.code === 0).length, 1,
    `two concurrent attempts both published: ${JSON.stringify(results)}`);
  assert.equal(results.filter(result => result.code !== 0).length, 1,
    `expected the stale attempt to be fenced: ${JSON.stringify(results)}`);
  const committed = await commit(session.uploadId);
  assert.equal(committed.code, 0, JSON.stringify(committed));
}

async function crossUserAuthorization() {
  assert.ok(secondUser && secondToken, 'HYDRA_SECOND_USER and HYDRA_SECOND_TOKEN are required for auth isolation');
  const data = bytes(3072, 53);
  const owner = await init(data);
  if (!owner.instant) {
    assert.equal((await putPart(owner)).code, 0);
    const committed = await commit(owner.uploadId);
    assert.equal(committed.code, 0, JSON.stringify(committed));
    owner.objectId = committed.objectId;
  }
  const attackerInit = await initAs(data, secondUser, secondToken);
  assert.equal(attackerInit.instant, false, 'foreign digest triggered an instant private hit');
  const privateResponse = await fetch(`${baseUrl}/api/object/download?objectId=${encodeURIComponent(owner.objectId)}`, {
    headers: { 'X-Upload-User': secondUser, 'X-Upload-Token': secondToken }
  });
  const privateBody = await privateResponse.json();
  assert.notEqual(privateBody.code, 0, 'foreign user downloaded a private object');
  const shareResponse = await fetch(`${baseUrl}/api/dealfile?cmd=share`, {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ user: secondUser, token: secondToken, md5: md5(data), filename: 'adversarial.bin' })
  });
  const shareBody = await shareResponse.json();
  assert.notEqual(shareBody.code, 0, 'foreign user shared an object they do not own');
}

async function deleteRevokesShare() {
  const data = bytes(3584, 71);
  const owner = await init(data);
  if (!owner.instant) {
    assert.equal((await putPart(owner)).code, 0);
    const committed = await commit(owner.uploadId);
    assert.equal(committed.code, 0, JSON.stringify(committed));
    owner.objectId = committed.objectId;
  }
  const shareResponse = await fetch(`${baseUrl}/api/dealfile?cmd=share`, {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ user, token, md5: md5(data), filename: 'adversarial.bin' })
  });
  assert.equal((await shareResponse.json()).code, 0, 'owner could not create a share');
  const sharesResponse = await fetch(`${baseUrl}/api/sharefiles?cmd=normal`, {
    method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ start: 0, count: 100 })
  });
  const shares = await sharesResponse.json();
  const shared = (shares.files || []).find(file => file.md5 === md5(data) && file.file_name === 'adversarial.bin');
  assert.ok(shared?.shareToken, 'share listing did not expose a token');
  const deleted = await post('/api/object/delete', { objectId: owner.objectId });
  assert.equal(deleted.code, 0, JSON.stringify(deleted));
  const staleShare = await fetch(`${baseUrl}/api/object/share-download?shareToken=${encodeURIComponent(shared.shareToken)}`);
  const staleBody = await staleShare.json();
  assert.notEqual(staleBody.code, 0, 'share token survived owner deletion');
}

async function gcDeletingDoesNotStealUpload() {
  const data = bytes(4608, 89);
  const uploadId = `gc-race-${Date.now()}`;
  const session = await init(data, { uploadId });
  const digest = session.part.sha256;
  await dbExec(`UPDATE chunk_blob SET state='DELETING' WHERE sha256='${digest}' AND size=${data.length}`);
  try {
    const rejected = await post('/api/object/init', {
      uploadId, filename: 'adversarial.bin', size: data.length, md5: md5(data), contentDigest: md5(data),
      parts: [{ index: 0, size: data.length, sha256: digest }]
    });
    assert.notEqual(rejected.code, 0, 'init bound an upload to a DELETING chunk');
  } finally {
    await dbExec(`UPDATE chunk_blob SET state='GC_PENDING', gc_after=DATE_ADD(NOW(), INTERVAL 1 HOUR) WHERE sha256='${digest}' AND size=${data.length}`);
    await post('/api/object/abort', { uploadId });
  }
}

async function crashRecoveryCommit(stage) {
  const uploadId = process.env.HYDRA_CRASH_UPLOAD_ID || `crash-recovery-${Date.now()}`;
  if (stage === 'prepare') {
    const data = bytes(5120, 107);
    const session = await init(data, { uploadId });
    assert.equal((await putPart(session)).code, 0);
    try {
      await commit(uploadId);
    } catch {
      // The failpoint intentionally terminates the FastCGI worker after the DB commit.
    }
    console.log(`CRASH_UPLOAD_ID=${uploadId}`);
    return;
  }
  const committed = await commit(uploadId);
  assert.equal(committed.code, 0, JSON.stringify(committed));
  const recovered = await status(uploadId);
  assert.equal((recovered.missingParts || []).length, 0, JSON.stringify(recovered));
  console.log('PASS: crash recovery after committed DB state');
}

async function run() {
  if (process.env.HYDRA_CRASH_RECOVERY_STAGE) {
    await crashRecoveryCommit(process.env.HYDRA_CRASH_RECOVERY_STAGE);
    return;
  }
  await concurrentDedupAndDoubleCommit();
  await wrongPayloadAndRetry();
  await incompleteCommit();
  await sameSessionConcurrentPart();
  await crossUserAuthorization();
  await deleteRevokesShare();
  await gcDeletingDoesNotStealUpload();
  console.log('PASS: concurrent dedup, duplicate commit, wrong-payload rejection, retry, incomplete commit, same-session fencing, cross-user auth/share isolation, delete/share revocation, GC deleting race');
}

run().catch(error => { console.error('FAIL:', error.stack || error); process.exitCode = 1; });
