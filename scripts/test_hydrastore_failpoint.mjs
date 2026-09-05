#!/usr/bin/env node
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';

const baseUrl = process.env.HYDRA_BASE_URL || 'https://127.0.0.1';
const user = process.env.HYDRA_USER;
const token = process.env.HYDRA_TOKEN;
const dbPassword = process.env.HYDRA_DB_PASSWORD;
const composeFile = 'docker/docker-compose.yaml';
const execFileAsync = promisify(execFile);
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
if (!user || !token || !dbPassword) {
  console.error('NOT RUN: set HYDRA_BASE_URL, HYDRA_USER, HYDRA_TOKEN, and HYDRA_DB_PASSWORD');
  process.exit(2);
}

const data = crypto.randomBytes(2 * 1024 * 1024);
const sha256 = crypto.createHash('sha256').update(data).digest('hex');
const md5 = crypto.createHash('md5').update(data).digest('hex');
const uploadId = `failpoint-${Date.now()}-${crypto.randomUUID()}`;

async function post(path, body) {
  const response = await fetch(`${baseUrl}${path}`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ ...body, user, token }),
    signal: AbortSignal.timeout(30000),
  });
  return { response, body: await response.json() };
}

async function putPart() {
  const response = await fetch(
    `${baseUrl}/api/object/part?uploadId=${encodeURIComponent(uploadId)}&index=0&sha256=${sha256}`,
    {
      method: 'POST',
      headers: {
        'content-type': 'application/octet-stream', 'content-length': String(data.length),
        'X-Upload-User': user, 'X-Upload-Token': token,
      },
      body: data,
      signal: AbortSignal.timeout(30000),
    },
  );
  return { status: response.status, body: await response.json() };
}

async function waitHealthy(name) {
  for (let i = 0; i < 30; i++) {
    const { stdout } = await execFileAsync('docker', [
      'inspect', '--format', '{{if .State.Health}}{{.State.Health.Status}}{{else}}running{{end}}', name,
    ], { windowsHide: true });
    if (stdout.trim() === 'healthy') return;
    await sleep(1000);
  }
  throw new Error(`${name} did not become healthy`);
}

async function dbQuery(sql) {
  const { stdout } = await execFileAsync('docker', [
    'exec', '-e', `MYSQL_PWD=${dbPassword}`, 'tc_fcgi_mysql', 'mysql', '-uroot', '-N',
    '-D', 'yuncunchu', '-e', sql,
  ], { windowsHide: true, maxBuffer: 1024 * 1024 });
  return stdout.trim();
}

let firstAttempt;
try {
  await execFileAsync('docker', ['stop', '-t', '1', 'tc_hydrastore_gateway_2'], { windowsHide: true });
  const initialized = await post('/api/object/init', {
    uploadId, filename: 'failpoint.bin', size: data.length, md5, contentDigest: md5,
    parts: [{ index: 0, size: data.length, sha256 }],
  });
  assert.equal(initialized.body.code, 0, JSON.stringify(initialized.body));
  assert.equal(initialized.body.instant, false, 'failpoint fixture unexpectedly deduplicated');
  try {
    firstAttempt = await putPart();
  } catch (error) {
    firstAttempt = { error: error.message };
  }
  assert.ok(firstAttempt.error || firstAttempt.status >= 500 || firstAttempt.body?.code !== 0,
    `after_blob_put failpoint did not break first attempt: ${JSON.stringify(firstAttempt)}`);
  console.log(JSON.stringify({ firstAttempt }));
  const backendBeforeRecovery = await dbQuery(
    `SELECT COALESCE(c.backend_file_id,'') FROM chunk_blob c JOIN upload_part p ON p.chunk_id=c.id ` +
    `WHERE p.upload_id='${uploadId}' AND p.part_index=0`,
  );
  assert.notEqual(backendBeforeRecovery, '', 'failpoint lost the durable backend file ID');

  await execFileAsync('docker', ['compose', '-f', composeFile, 'up', '-d', '--force-recreate',
    'storage_gateway_1', 'storage_gateway_2'], { windowsHide: true, maxBuffer: 4 * 1024 * 1024 });
  await waitHealthy('tc_hydrastore_gateway_1');
  await waitHealthy('tc_hydrastore_gateway_2');
  const retry = await putPart();
  assert.equal(retry.status, 200, JSON.stringify(retry));
  assert.equal(retry.body.code, 0, JSON.stringify(retry));
  const backendAfterRecovery = await dbQuery(
    `SELECT COALESCE(c.backend_file_id,'') FROM chunk_blob c JOIN upload_part p ON p.chunk_id=c.id ` +
    `WHERE p.upload_id='${uploadId}' AND p.part_index=0`,
  );
  assert.equal(backendAfterRecovery, backendBeforeRecovery, 'recovery created a second physical blob');
  const committed = await post('/api/object/commit', { uploadId });
  assert.equal(committed.body.code, 0, JSON.stringify(committed.body));
  const invariant = await dbQuery(
    `SELECT CONCAT(us.state, ':', COUNT(*), ':', COALESCE(SUM(up.state='READY'),0)) ` +
    `FROM upload_session us JOIN upload_part up ON up.upload_id=us.id WHERE us.id='${uploadId}' GROUP BY us.state`,
  );
  assert.match(invariant, /^COMMITTED:1:1$/, `unexpected failpoint metadata state: ${invariant}`);
  console.log(JSON.stringify({ status: 'PASS', firstAttempt, retry, backendReused: true,
    objectId: committed.body.objectId, invariant }, null, 2));
} finally {
  await execFileAsync('docker', ['compose', '-f', composeFile, 'up', '-d', '--force-recreate', 'storage_gateway_1', 'storage_gateway_2'], {
    windowsHide: true, maxBuffer: 4 * 1024 * 1024,
  }).catch(() => {});
}
