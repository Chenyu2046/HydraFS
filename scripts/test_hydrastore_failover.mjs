#!/usr/bin/env node
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';

const baseUrl = process.env.HYDRA_BASE_URL || 'https://127.0.0.1';
const user = process.env.HYDRA_USER;
const token = process.env.HYDRA_TOKEN;
const dbPassword = process.env.HYDRA_DB_PASSWORD;
const execFileAsync = promisify(execFile);
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
if (!user || !token || !dbPassword) {
  console.error('NOT RUN: set HYDRA_BASE_URL, HYDRA_USER, HYDRA_TOKEN, and HYDRA_DB_PASSWORD');
  process.exit(2);
}

const size = 10 * 1024 * 1024;
const data = Buffer.alloc(size);
for (let i = 0; i < data.length; i++) data[i] = (i * 31 + 97) & 0xff;
const sha256 = crypto.createHash('sha256').update(data).digest('hex');
const md5 = crypto.createHash('md5').update(data).digest('hex');
const traceId = `failover-${process.pid}-${crypto.randomUUID()}`;

async function post(path, body) {
  const response = await fetch(`${baseUrl}${path}`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ ...body, user, token }),
    signal: AbortSignal.timeout(30000),
  });
  return { response, body: await response.json() };
}

async function dbQuery(sql) {
  const { stdout } = await execFileAsync('docker', [
    'exec', '-e', `MYSQL_PWD=${dbPassword}`, 'tc_fcgi_mysql', 'mysql', '-uroot', '-N',
    '-D', 'yuncunchu', '-e', sql,
  ], { windowsHide: true, maxBuffer: 1024 * 1024 });
  return stdout.trim();
}

async function waitHealthy(name) {
  for (let i = 0; i < 30; i++) {
    try {
      const { stdout } = await execFileAsync('docker', [
        'inspect', '--format', '{{if .State.Health}}{{.State.Health.Status}}{{else}}running{{end}}', name,
      ], { windowsHide: true });
      if (stdout.trim() === 'healthy') return;
    } catch {}
    await sleep(1000);
  }
  throw new Error(`${name} did not become healthy`);
}

async function slowBody(killAfterBytes) {
  let offset = 0;
  let killPromise;
  const stream = new ReadableStream({
    async pull(controller) {
      if (offset >= data.length) {
        controller.close();
        return;
      }
      await sleep(50);
      const end = Math.min(data.length, offset + 64 * 1024);
      controller.enqueue(data.subarray(offset, end));
      offset = end;
      if (offset >= killAfterBytes && !killPromise) {
        killPromise = execFileAsync('docker', ['stop', '-t', '1', 'tc_hydrastore_gateway_1'], {
          windowsHide: true,
        });
      }
    },
  });
  return { stream, getKillPromise: () => killPromise };
}

async function downloadAndVerify(objectId) {
  const response = await fetch(`${baseUrl}/api/object/download?objectId=${encodeURIComponent(objectId)}`, {
    headers: { 'X-Upload-User': user, 'X-Upload-Token': token },
    signal: AbortSignal.timeout(30000),
  });
  assert.equal(response.status, 200, `download HTTP ${response.status}`);
  const digest = crypto.createHash('md5');
  let bytes = 0;
  for await (const chunk of response.body) {
    digest.update(chunk);
    bytes += chunk.length;
  }
  assert.equal(bytes, data.length, 'failover object size changed');
  assert.equal(digest.digest('hex'), md5, 'failover object digest changed');
}

let uploadId;
try {
  const initialized = await post('/api/object/init', {
    uploadId: `failover-${Date.now()}`,
    filename: 'failover.bin',
    size: data.length,
    md5,
    contentDigest: md5,
    parts: [{ index: 0, size: data.length, sha256 }],
  });
  assert.equal(initialized.response.status, 200, JSON.stringify(initialized.body));
  assert.equal(initialized.body.code, 0, JSON.stringify(initialized.body));
  assert.equal(initialized.body.instant, false, 'failover test unexpectedly deduplicated');
  uploadId = initialized.body.uploadId;

  await execFileAsync('docker', ['stop', '-t', '1', 'tc_hydrastore_gateway_2'], { windowsHide: true });
  await sleep(500);
  const slow = await slowBody(1024 * 1024);
  let firstError = null;
  let firstResponse = null;
  try {
    firstResponse = await fetch(
      `${baseUrl}/api/object/part?uploadId=${encodeURIComponent(uploadId)}&index=0&sha256=${sha256}`,
      {
        method: 'POST',
        headers: {
          'content-type': 'application/octet-stream',
          'content-length': String(data.length),
          'X-Upload-User': user,
          'X-Upload-Token': token,
          'X-Hydra-Trace-Id': traceId,
          'X-Hydra-Attempt': '1',
          'X-Hydra-Retry-Reason': 'mid_body_gateway_stop',
        },
        body: slow.stream,
        duplex: 'half',
        signal: AbortSignal.timeout(20000),
      },
    );
  } catch (error) {
    firstError = error;
  }
  if (slow.getKillPromise()) await slow.getKillPromise();
  assert.ok(firstError || !firstResponse?.ok, 'mid-body Gateway stop did not break the first POST');

  await execFileAsync('docker', ['start', 'tc_hydrastore_gateway_1'], { windowsHide: true });
  await execFileAsync('docker', ['start', 'tc_hydrastore_gateway_2'], { windowsHide: true });
  await waitHealthy('tc_hydrastore_gateway_1');
  await waitHealthy('tc_hydrastore_gateway_2');

  const retry = await fetch(
    `${baseUrl}/api/object/part?uploadId=${encodeURIComponent(uploadId)}&index=0&sha256=${sha256}`,
    {
      method: 'POST',
      headers: {
        'content-type': 'application/octet-stream',
        'content-length': String(data.length),
        'X-Upload-User': user,
        'X-Upload-Token': token,
        'X-Hydra-Trace-Id': `${traceId}-retry`,
        'X-Hydra-Attempt': '2',
        'X-Hydra-Retry-Reason': 'network',
      },
      body: data,
      signal: AbortSignal.timeout(30000),
    },
  );
  const retryBody = await retry.json();
  assert.equal(retry.status, 200, JSON.stringify(retryBody));
  assert.equal(retryBody.code, 0, JSON.stringify(retryBody));
  const committed = await post('/api/object/commit', { uploadId });
  assert.equal(committed.response.status, 200, JSON.stringify(committed.body));
  assert.equal(committed.body.code, 0, JSON.stringify(committed.body));
  await downloadAndVerify(committed.body.objectId);

  const invariant = await dbQuery(
    `SELECT CONCAT(us.state, ':', COUNT(*), ':', COALESCE(SUM(up.state='READY'),0)) ` +
    `FROM upload_session us JOIN upload_part up ON up.upload_id=us.id WHERE us.id='${uploadId}' GROUP BY us.state`,
  );
  assert.match(invariant, /^COMMITTED:1:1$/, `unexpected failover metadata state: ${invariant}`);
  console.log(JSON.stringify({
    status: 'PASS',
    firstAttempt: firstError ? { error: firstError.message } : { httpStatus: firstResponse.status },
    retry: { httpStatus: retry.status, code: retryBody.code },
    objectId: committed.body.objectId,
    invariant,
  }, null, 2));
} finally {
  await execFileAsync('docker', ['start', 'tc_hydrastore_gateway_1'], { windowsHide: true }).catch(() => {});
  await execFileAsync('docker', ['start', 'tc_hydrastore_gateway_2'], { windowsHide: true }).catch(() => {});
}
