#!/usr/bin/env node
import fs from 'node:fs/promises';

const reportPath = process.env.HYDRA_PROFILE_REPORT;
const gatewayPaths = (process.env.HYDRA_PROFILE_GATEWAY_LOGS || '').split(';').filter(Boolean);
const nginxPath = process.env.HYDRA_PROFILE_NGINX_LOG;
const outputPath = process.env.HYDRA_PROFILE_OUTPUT || `${reportPath}.profile.json`;

if (!reportPath) {
  console.error('NOT RUN: set HYDRA_PROFILE_REPORT');
  process.exit(2);
}

const report = JSON.parse(await fs.readFile(reportPath, 'utf8'));
const readRequired = async path => {
  if (!path) return { ok: false, text: '' };
  try { return { ok: true, text: await fs.readFile(path, 'utf8') }; }
  catch { return { ok: false, text: '' }; }
};
const gatewayResults = await Promise.all(gatewayPaths.map(readRequired));
const nginxResult = await readRequired(nginxPath);
if (!gatewayPaths.length || gatewayResults.some(result => !result.ok) || !nginxResult.ok) {
  const profile = { status: 'NOT_VERIFIED', report: reportPath,
    logInputs: { gatewayPaths, nginxPath: nginxPath || null },
    reason: 'required profiling log is missing or unreadable' };
  await fs.writeFile(outputPath, JSON.stringify(profile, null, 2));
  console.error(JSON.stringify({ ...profile, output: outputPath }, null, 2));
  process.exit(1);
}
const gatewayLogs = gatewayResults.map(result => result.text).join('\n');
const nginxLogs = nginxResult.text;

function fields(line) {
  const result = {};
  for (const key of ['trace_id', 'gateway_instance', 'worker_pid', 'client_attempt', 'claim_part_ms',
    'blob_put_ms', 'metadata_ready_ms', 'gateway_total_ms', 'breaker_state', 'storage_success', 'hash_ok',
    'request_time', 'upstream_response_time', 'upstream_status', 'upstream_addr']) {
    const match = line.match(new RegExp(`${key}=([^\\s]+)`));
    if (match) result[key] = match[1];
  }
  return result;
}

const gatewayByTrace = new Map();
for (const line of gatewayLogs.split(/\r?\n/)) {
  if (!line.includes('HYDRA_TRACE')) continue;
  const value = fields(line);
  if (!value.trace_id) continue;
  const list = gatewayByTrace.get(value.trace_id) || [];
  list.push(value);
  gatewayByTrace.set(value.trace_id, list);
}
const nginxByTrace = new Map();
for (const line of nginxLogs.split(/\r?\n/)) {
  const value = fields(line);
  if (!value.trace_id || value.trace_id === '-') continue;
  const list = nginxByTrace.get(value.trace_id) || [];
  list.push(value);
  nginxByTrace.set(value.trace_id, list);
}

const number = value => Number.isFinite(Number(value)) ? Number(value) : null;
const traces = report.results?.flatMap(row => row.chunkTraces || []) || [];
const slow = traces.map(trace => {
  const attempts = trace.attempts || [];
  const worst = attempts.reduce((a, b) => !a || b.requestElapsedMs > a.requestElapsedMs ? b : a, null);
  const gatewayRecords = gatewayByTrace.get(trace.traceId) || [];
  // Do not attribute a failed slow attempt to a later successful retry.  The
  // fallback remains visible as supporting evidence, but classification must
  // use the exact client attempt whenever possible.
  const gateway = gatewayRecords.find(item =>
    number(item.client_attempt) === number(worst?.attempt));
  const gatewayFallback = gatewayRecords.at(-1);
  const nginxRecords = nginxByTrace.get(trace.traceId) || [];
  const nginx = nginxRecords.find(item => number(item.client_attempt) === number(worst?.attempt)) ||
    (nginxRecords.length === 1 ? nginxRecords[0] : undefined);
  let category = 'H';
  let evidence = 'unattributed';
  if (trace.clientQueueWaitMs > 5000) category = 'A';
  else if (trace.retryBackoffMs > 5000) category = 'F';
  else if (gateway?.breaker_state === 'open') category = 'G';
  else if (!gateway) category = 'B';
  else if (number(gateway.blob_put_ms) > 5000) category = 'E';
  else if (number(gateway.claim_part_ms) > 5000 || number(gateway.metadata_ready_ms) > 5000) category = 'D';
  else if (number(nginx?.upstream_response_time) > number(gateway.gateway_total_ms) + 0.5) category = 'C';
  else if (number(gateway.gateway_total_ms) > 5000) category = 'C';
  if (category === 'A') evidence = 'client_queue_wait';
  else if (category === 'B') evidence = 'no_gateway_trace_for_slowest_attempt';
  else if (category === 'C') evidence = 'nginx_or_gateway_wait';
  else if (category === 'D') evidence = 'metadata_or_claim';
  else if (category === 'E') evidence = 'blob_put';
  else if (category === 'F') evidence = 'retry_backoff';
  else if (category === 'G') evidence = 'breaker_open';
  return { traceId: trace.traceId, partIndex: trace.partIndex, category,
    evidence,
    clientQueueWaitMs: trace.clientQueueWaitMs, retryBackoffMs: trace.retryBackoffMs,
    worstAttempt: worst, gateway, gatewayFallback, nginx };
}).filter(item => item.worstAttempt && item.worstAttempt.requestElapsedMs > 5000)
  .sort((a, b) => b.worstAttempt.requestElapsedMs - a.worstAttempt.requestElapsedMs);

const categoryCounts = Object.fromEntries(['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'].map(category => [category,
  slow.filter(item => item.category === category).length]));
const profile = {
  report: reportPath,
  traceCount: traces.length,
  slowChunkCount: slow.length,
  top20: slow.slice(0, 20),
  categoryCounts,
  unknownRateTop20: slow.length ? slow.slice(0, 20).filter(item => item.category === 'H').length / Math.min(20, slow.length) : 0,
  logInputs: { gatewayPaths, nginxPath: nginxPath || null },
};
await fs.writeFile(outputPath, JSON.stringify(profile, null, 2));
console.log(JSON.stringify({ ...profile, output: outputPath }, null, 2));
if (profile.unknownRateTop20 > 0.2) process.exitCode = 1;
