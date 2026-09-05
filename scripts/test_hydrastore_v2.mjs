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

async function dbQuery(sql) {
  assert.ok(process.env.HYDRA_DB_PASSWORD, 'HYDRA_DB_PASSWORD is required for DB assertions');
  const { stdout } = await execFileAsync('docker', [
    'exec', '-e', `MYSQL_PWD=${process.env.HYDRA_DB_PASSWORD}`, 'tc_fcgi_mysql',
    'mysql', '-uroot', '-N', '-D', 'yuncunchu', '-e', sql,
  ], { windowsHide: true, maxBuffer: 1024 * 1024 });
  return stdout.trim();
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
  return putPartAs(session, payload, user, token);
}

async function putPartAs(session, payload, requestUser, requestToken) {
  const response = await fetch(`${baseUrl}/api/object/part?uploadId=${encodeURIComponent(session.uploadId)}&index=0&sha256=${session.part.sha256}`, {
    method: 'POST', headers: { 'content-type': 'application/octet-stream', 'X-Upload-User': requestUser, 'X-Upload-Token': requestToken }, body: payload
  });
  return response.json();
}

async function commit(uploadId) { return post('/api/object/commit', { uploadId }); }
async function status(uploadId) { return post('/api/object/status', { uploadId }); }

async function concurrentDedupAndDoubleCommit() {
  const data = bytes(1024, 11);
  const [a, b] = await Promise.all([init(data), init(data)]);
  const [putA, putB] = await Promise.all([
    a.instant ? Promise.resolve({ code: 0 }) : putPart(a),
    b.instant ? Promise.resolve({ code: 0 }) : putPart(b),
  ]);
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
  assert.ok(results.some(result => result.code === 0),
    `concurrent part attempts did not publish: ${JSON.stringify(results)}`);
  // code 0 from both requests is valid when the second request observes the
  // already READY part; the invariant we need to protect is one physical blob.
  assert.ok(results.every(result => [0, 1, 2].includes(result.code)),
    `concurrent part attempt failed unexpectedly: ${JSON.stringify(results)}`);
  const committed = await commit(session.uploadId);
  assert.equal(committed.code, 0, JSON.stringify(committed));
  assert.equal(await dbQuery(
    `SELECT COUNT(*) FROM chunk_blob WHERE sha256='${session.part.sha256}' AND size=${data.length}`
  ), '1', 'same-session race created duplicate physical blobs');
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

async function crossUserCommitForgery() {
  assert.ok(secondUser && secondToken, 'HYDRA_SECOND_USER and HYDRA_SECOND_TOKEN are required for auth isolation');
  const secret = bytes(6144, 121);
  const unrelated = bytes(6144, 127);
  const owner = await init(secret, { filename: `owner-${Date.now()}.bin` });
  if (!owner.instant) {
    assert.equal((await putPart(owner)).code, 0);
    const committed = await commit(owner.uploadId);
    assert.equal(committed.code, 0, JSON.stringify(committed));
    owner.objectId = committed.objectId;
  }
  assert.ok(owner.objectId, `owner init did not return objectId: ${JSON.stringify(owner)}`);
  const digest = md5(secret);
  const manifestBefore = await dbQuery(`SELECT COUNT(*) FROM object_manifest WHERE content_digest='${digest}'`);
  const fileBefore = await dbQuery(`SELECT COUNT(*) FROM file_info WHERE md5='${digest}'`);
  const forged = await initAs(unrelated, secondUser, secondToken, {
    filename: `forged-${Date.now()}.bin`, md5: digest, contentDigest: digest,
    uploadId: `forged-${Date.now()}`
  });
  assert.equal((await putPartAs(forged, unrelated, secondUser, secondToken)).code, 0);
  const forgedCommit = await postAs('/api/object/commit', { uploadId: forged.uploadId }, secondUser, secondToken);
  if (forgedCommit.code === 0) {
    await postAs('/api/object/delete', { objectId: forgedCommit.objectId }, secondUser, secondToken);
  }
  assert.notEqual(forgedCommit.code, 0, `forged commit was accepted: ${JSON.stringify(forgedCommit)}`);
  const relationCount = await dbQuery(
    `SELECT COUNT(*) FROM user_file_list WHERE user='${secondUser}' AND md5='${digest}' AND file_name LIKE 'forged-%'`
  );
  assert.equal(relationCount, '0', 'forged commit created a private relation');
  assert.equal(await dbQuery(`SELECT COUNT(*) FROM object_manifest WHERE content_digest='${digest}'`), manifestBefore,
    'forged rollback leaked an object manifest');
  assert.equal(await dbQuery(`SELECT COUNT(*) FROM file_info WHERE md5='${digest}'`), fileBefore,
    'forged rollback leaked file metadata');
  assert.equal(await dbQuery(`SELECT state FROM upload_session WHERE id='${forged.uploadId}'`), 'UPLOADING');
  const privateResponse = await fetch(`${baseUrl}/api/object/download?objectId=${encodeURIComponent(owner.objectId)}`, {
    headers: { 'X-Upload-User': secondUser, 'X-Upload-Token': secondToken }
  });
  const privateBody = await privateResponse.json();
  assert.notEqual(privateBody.code, 0, 'forged user downloaded the owner object');
}

async function aiTaskAndManifestReadRegression() {
  const runId = `${Date.now()}-${Math.random()}`;
  const text = Buffer.from(`HydraStore V2 internal reader regression ${runId}\n[[Manifest]]\n`);
  const textSession = await init(text, {
    filename: `v2-regression-${Date.now()}.txt`, uploadId: `ai-txt-${Date.now()}`
  });
  assert.equal((await putPart(textSession)).code, 0);
  const textCommit = await commit(textSession.uploadId);
  assert.equal(textCommit.code, 0, JSON.stringify(textCommit));
  const textDigest = md5(text);
  assert.equal(await dbQuery(`SELECT type FROM file_info WHERE md5='${textDigest}'`), 'txt');
  const textTask = await dbQuery(
    `SELECT COUNT(*) FROM ai_parse_task WHERE user='${user}' AND md5='${textDigest}' AND source='hydrastore_v2'`
  );
  assert.equal(textTask, '1', 'V2 txt commit did not persist an AI task');
  const textResponse = await fetch(
    `${baseUrl}/api/object/download?objectId=${encodeURIComponent(textCommit.objectId)}`,
    { headers: { 'X-Upload-User': user, 'X-Upload-Token': token } }
  );
  assert.equal(textResponse.status, 200);
  assert.equal(md5(Buffer.from(await textResponse.arrayBuffer())), textDigest,
    'manifest reconstruction changed txt content');

  const pdf = Buffer.from(`%PDF-1.4\nHydraStore V2 PDF regression ${runId}\n`);
  const pdfSession = await init(pdf, {
    filename: `v2-regression-${Date.now()}.pdf`, uploadId: `ai-pdf-${Date.now()}`
  });
  assert.equal((await putPart(pdfSession)).code, 0);
  assert.equal((await commit(pdfSession.uploadId)).code, 0);
  const pdfDigest = md5(pdf);
  assert.equal(await dbQuery(`SELECT type FROM file_info WHERE md5='${pdfDigest}'`), 'pdf');
  assert.equal(await dbQuery(
    `SELECT COUNT(*) FROM ai_parse_task WHERE user='${user}' AND md5='${pdfDigest}' AND source='hydrastore_v2'`
  ), '1', 'V2 pdf commit did not persist an AI task');

  const zip = Buffer.from(`PK\\x03\\x04 unsupported archive regression ${runId}`);
  const zipSession = await init(zip, {
    filename: `v2-regression-${Date.now()}.zip`, uploadId: `ai-zip-${Date.now()}`
  });
  assert.equal((await putPart(zipSession)).code, 0);
  assert.equal((await commit(zipSession.uploadId)).code, 0);
  const zipDigest = md5(zip);
  assert.equal(await dbQuery(`SELECT type FROM file_info WHERE md5='${zipDigest}'`), 'zip');
  assert.equal(await dbQuery(
    `SELECT status FROM ai_parse_task WHERE user='${user}' AND md5='${zipDigest}' AND source='hydrastore_v2'`
  ), 'skipped', 'unsupported zip was scheduled for parsing');
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
  await crossUserCommitForgery();
  await aiTaskAndManifestReadRegression();
  await deleteRevokesShare();
  await gcDeletingDoesNotStealUpload();
  console.log('PASS: concurrent dedup, duplicate commit, wrong-payload rejection, retry, incomplete commit, same-session fencing, cross-user auth/share isolation, delete/share revocation, GC deleting race');
}

run().catch(error => { console.error('FAIL:', error.stack || error); process.exitCode = 1; });
