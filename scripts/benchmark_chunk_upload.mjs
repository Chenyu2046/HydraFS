#!/usr/bin/env node
// Reproducible localhost benchmark for the real chunk-upload HTTP endpoints.
// Usage: node scripts/benchmark_chunk_upload.mjs --trials 3 --chunks 24
import { createHash, randomUUID } from 'node:crypto';
import { execFile } from 'node:child_process';
import { mkdir, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { performance } from 'node:perf_hooks';

const args = Object.fromEntries(process.argv.slice(2).map(item => {
  const [key, value = 'true'] = item.replace(/^--/, '').split('=');
  return [key, value];
}));
const trials = Number(args.trials ?? 3);
const chunks = Number(args.chunks ?? 24);
const chunkMiB = Number(args['chunk-mib'] ?? 10);
const baseUrl = args.url ?? 'https://127.0.0.1';
const timeoutMs = Number(args['timeout-ms'] ?? 30000);

if (!Number.isInteger(trials) || trials < 1 || !Number.isInteger(chunks) || chunks < 2 || chunkMiB < 1) {
  throw new Error('trials must be >= 1, chunks must be >= 2, and chunk-mib must be >= 1');
}

// The repository uses a self-signed certificate for its local HTTPS endpoint.
if (baseUrl.includes('127.0.0.1') || baseUrl.includes('localhost')) {
  process.env.NODE_TLS_REJECT_UNAUTHORIZED = '0';
}

const chunkBytes = chunkMiB * 1024 * 1024;
const password = 'Benchmark_2026!';
const passwordMd5 = md5(Buffer.from(password));

function md5(data) {
  return createHash('md5').update(data).digest('hex');
}

function percentile(values, p) {
  if (values.length === 0) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.min(sorted.length - 1, Math.ceil(sorted.length * p) - 1)];
}

function median(values) {
  return percentile(values, 0.5);
}

function round(value, digits = 2) {
  return Number(value.toFixed(digits));
}

function docker(commandArgs) {
  return new Promise((resolve, reject) => {
    execFile('docker', commandArgs, { windowsHide: true }, (error, stdout, stderr) => {
      if (error) reject(new Error(stderr.trim() || error.message));
      else resolve(stdout.trim());
    });
  });
}

async function request(path, options = {}) {
  const response = await fetch(`${baseUrl}${path}`, options);
  const text = await response.text();
  let data;
  try { data = JSON.parse(text); } catch { throw new Error(`${path} returned non-JSON (${response.status}): ${text.slice(0, 160)}`); }
  if (!response.ok || data.code !== 0) throw new Error(`${path} failed: ${data.msg || data.message || `code ${data.code}`}`);
  return data;
}

async function createUser() {
  const suffix = randomUUID().replace(/-/g, '').slice(0, 12);
  const username = `bench_${suffix}`;
  await request('/api/reg', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ userName: username, firstPwd: passwordMd5, nickName: `bn_${suffix}`, email: `${username}@local.test`, phone: '13800000000' })
  });
  const login = await request('/api/login', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ user: username, pwd: passwordMd5 })
  });
  if (!login.token) throw new Error('login succeeded without token');
  return { username, token: login.token };
}

function makePayload(seed) {
  const payload = Buffer.alloc(chunkBytes, seed);
  const hash = createHash('md5');
  for (let i = 0; i < chunks; i++) hash.update(payload);
  return { payload, md5: hash.digest('hex'), size: chunkBytes * chunks };
}

function createAimdWindow() {
  const samples = [];
  let size = 4;
  return {
    get size() { return size; },
    record(success, rtt, timedOut) {
      samples.push({ success, rtt, timedOut });
      if (samples.length > 16) samples.shift();
      const successes = samples.filter(sample => sample.success);
      const failureRate = samples.filter(sample => !sample.success).length / samples.length;
      const timeoutRate = samples.filter(sample => sample.timedOut).length / samples.length;
      const averageRtt = successes.length ? successes.reduce((total, sample) => total + sample.rtt, 0) / successes.length : rtt;
      const degraded = !success || timedOut || failureRate > 0.2 || timeoutRate > 0.1 || rtt > averageRtt * 2;
      if (degraded) size = Math.max(4, Math.floor(size / 2));
      else if (failureRate === 0 && timeoutRate === 0 && rtt <= averageRtt * 1.5) size = Math.min(32, size + 1);
    }
  };
}

async function uploadChunk({ fileMd5, index, payload, stats }) {
  for (let attempt = 0; attempt <= 3; attempt++) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    const startedAt = performance.now();
    try {
      const response = await fetch(`${baseUrl}/api/chunk_upload?md5=${encodeURIComponent(fileMd5)}&index=${index}`, {
        method: 'POST', headers: { 'Content-Type': 'application/octet-stream' }, body: payload, signal: controller.signal
      });
      const data = await response.json();
      const rtt = performance.now() - startedAt;
      if (!response.ok || data.code !== 0) throw new Error(data.msg || `code ${data.code}`);
      stats.rtts.push(rtt);
      stats.attempts++;
      return rtt;
    } catch (error) {
      const timedOut = controller.signal.aborted;
      stats.attempts++;
      if (timedOut) stats.timeouts++;
      if (attempt === 3) throw error;
      stats.retries++;
    } finally {
      clearTimeout(timer);
    }
  }
}

function parseMiB(value) {
  const match = /([\d.]+)\s*(KiB|MiB|GiB|B)/.exec(value);
  if (!match) return 0;
  const amount = Number(match[1]);
  return round(amount * ({ B: 1 / 1048576, KiB: 1 / 1024, MiB: 1, GiB: 1024 }[match[2]]));
}

function createResourceSampler() {
  const peak = {};
  let busy = false;
  const sample = async () => {
    if (busy) return;
    busy = true;
    try {
      const output = await docker(['stats', '--no-stream', '--format', '{{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}', 'tc_fcgi_app', 'tc_fcgi_nginx_fastdfs', 'tc_fcgi_mysql']);
      for (const line of output.split(/\r?\n/)) {
        const [name, cpu, memory] = line.split('\t');
        if (!name) continue;
        peak[name] ??= { cpuPct: 0, memoryMiB: 0 };
        peak[name].cpuPct = Math.max(peak[name].cpuPct, Number.parseFloat(cpu) || 0);
        peak[name].memoryMiB = Math.max(peak[name].memoryMiB, parseMiB(memory));
      }
    } finally { busy = false; }
  };
  const timer = setInterval(sample, 1000);
  return { sample, stop: () => clearInterval(timer), peak };
}

async function runTrial(mode, index, user) {
  const { payload, md5: fileMd5, size } = makePayload(65 + index + (mode === 'aimd' ? 40 : 0));
  const filename = `benchmark-${mode}-${index}.bin`;
  const stats = { rtts: [], attempts: 0, retries: 0, timeouts: 0, maxInFlight: 0, peakWindow: mode === 'fixed' ? 8 : 4 };
  const scheduler = mode === 'aimd' ? createAimdWindow() : { size: 8, record() {} };
  const sampler = createResourceSampler();
  await sampler.sample();
  const startedAt = performance.now();
  try {
    await request('/api/chunk_init', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ user: user.username, token: user.token, filename, md5: fileMd5, size, chunkCount: chunks }) });
    const active = new Set();
    let next = 0;
    const start = chunkIndex => {
      let task;
      task = uploadChunk({ fileMd5, index: chunkIndex, payload, stats })
        .then(rtt => { scheduler.record(true, rtt, false); })
        .catch(error => { scheduler.record(false, timeoutMs, error?.name === 'AbortError'); throw error; })
        .finally(() => active.delete(task));
      active.add(task);
      stats.maxInFlight = Math.max(stats.maxInFlight, active.size);
      stats.peakWindow = Math.max(stats.peakWindow, scheduler.size);
    };
    while (next < chunks || active.size) {
      while (next < chunks && active.size < scheduler.size) start(next++);
      if (active.size) await Promise.race(active);
    }
    await request('/api/chunk_merge', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ user: user.username, token: user.token, md5: fileMd5, filename }) });
  } finally {
    sampler.stop();
    await sampler.sample();
  }
  const durationMs = performance.now() - startedAt;
  return {
    mode, trial: index, fileMiB: round(size / 1048576), chunks, durationMs: round(durationMs), throughputMiBps: round((size / 1048576) / (durationMs / 1000)),
    chunkRttMs: { p50: round(percentile(stats.rtts, 0.5)), p95: round(percentile(stats.rtts, 0.95)), p99: round(percentile(stats.rtts, 0.99)) },
    retries: stats.retries, timeouts: stats.timeouts, attempts: stats.attempts, retransmissionRatePct: round((stats.retries / stats.attempts) * 100),
    maxInFlight: stats.maxInFlight, peakWindow: stats.peakWindow, containerPeak: sampler.peak
  };
}

function aggregate(mode, results) {
  const allRtts = results.flatMap(result => [result.chunkRttMs.p50, result.chunkRttMs.p95, result.chunkRttMs.p99]);
  return {
    mode, trials: results.length, throughputMiBps: round(median(results.map(result => result.throughputMiBps))),
    durationMs: round(median(results.map(result => result.durationMs))), p50ChunkRttMs: round(median(results.map(result => result.chunkRttMs.p50))),
    p95ChunkRttMs: round(median(results.map(result => result.chunkRttMs.p95))), p99ChunkRttMs: round(median(results.map(result => result.chunkRttMs.p99))),
    retries: results.reduce((total, result) => total + result.retries, 0), timeouts: results.reduce((total, result) => total + result.timeouts, 0),
    retransmissionRatePct: round(results.reduce((total, result) => total + result.retries, 0) / results.reduce((total, result) => total + result.attempts, 0) * 100),
    maxInFlight: Math.max(...results.map(result => result.maxInFlight)), peakWindow: Math.max(...results.map(result => result.peakWindow)),
    containerPeak: results.reduce((acc, result) => {
      for (const [name, value] of Object.entries(result.containerPeak)) {
        acc[name] ??= { cpuPct: 0, memoryMiB: 0 };
        acc[name].cpuPct = Math.max(acc[name].cpuPct, value.cpuPct);
        acc[name].memoryMiB = Math.max(acc[name].memoryMiB, value.memoryMiB);
      }
      return acc;
    }, {})
  };
}

function reportMarkdown(report) {
  const fixed = report.summary.fixed;
  const aimd = report.summary.aimd;
  const throughputChange = round((aimd.throughputMiBps / fixed.throughputMiBps - 1) * 100);
  const retransmissionChange = fixed.retransmissionRatePct === 0 ? 'N/A (both 0%)' : `${round((1 - aimd.retransmissionRatePct / fixed.retransmissionRatePct) * 100)}%`;
  const row = metric => `| ${metric} | ${fixed[metric]} | ${aimd[metric]} |`;
  return `# 分片上传压测报告\n\n> 实测时间：${report.generatedAt}\n> 环境：Docker Desktop + 本机 HTTPS 回环；绝对吞吐不代表线上容量，仅比较同机、同文件集、同 worker 配置下的相对结果。\n\n## 工作负载\n\n- 每轮文件：${chunkMiB} MiB × ${chunks} 分片 = ${chunkMiB * chunks} MiB\n- 每组：${trials} 轮；固定并发为 8，AIMD 为 4→32\n- 后端：chunk_upload 默认 8 workers；真实接口：/api/chunk_init、/api/chunk_upload、/api/chunk_merge。\n\n## 核心结果（各组中位数）\n\n| 指标 | 固定并发 8 | AIMD 4→32 |\n| --- | ---: | ---: |\n${row('throughputMiBps')}\n${row('durationMs')}\n${row('p50ChunkRttMs')}\n${row('p95ChunkRttMs')}\n${row('p99ChunkRttMs')}\n${row('retries')}\n${row('timeouts')}\n${row('retransmissionRatePct')}\n${row('maxInFlight')}\n${row('peakWindow')}\n\n## 对比结论\n\n- AIMD 吞吐相对固定并发：**${throughputChange >= 0 ? '+' : ''}${throughputChange}%**。\n- 超时驱动重传变化：**${retransmissionChange}**。\n- 本轮实际峰值窗口：${aimd.peakWindow}；实际最大在途请求：${aimd.maxInFlight}。\n\n## 容器峰值资源\n\n| 容器 | 固定 CPU% | 固定内存 MiB | AIMD CPU% | AIMD 内存 MiB |\n| --- | ---: | ---: | ---: | ---: |\n${['tc_fcgi_app', 'tc_fcgi_nginx_fastdfs', 'tc_fcgi_mysql'].map(name => `| ${name} | ${fixed.containerPeak[name]?.cpuPct ?? 0} | ${fixed.containerPeak[name]?.memoryMiB ?? 0} | ${aimd.containerPeak[name]?.cpuPct ?? 0} | ${aimd.containerPeak[name]?.memoryMiB ?? 0} |`).join('\n')}\n\n## 可复现命令\n\n\`\`\`powershell\nnode scripts/benchmark_chunk_upload.mjs --trials ${trials} --chunks ${chunks} --chunk-mib ${chunkMiB}\n\`\`\`\n\n完整逐轮数据见同名 JSON 文件。\n`;
}

const user = await createUser();
const results = { fixed: [], aimd: [] };
for (const mode of ['fixed', 'aimd']) {
  for (let trial = 1; trial <= trials; trial++) {
    process.stdout.write(`[${mode}] trial ${trial}/${trials}\n`);
    results[mode].push(await runTrial(mode, trial, user));
  }
}
const report = { generatedAt: new Date().toISOString(), baseUrl, workload: { trials, chunks, chunkMiB, timeoutMs }, results, summary: { fixed: aggregate('fixed', results.fixed), aimd: aggregate('aimd', results.aimd) } };
const stamp = report.generatedAt.replace(/[:.]/g, '-');
await mkdir('reports', { recursive: true });
await writeFile(join('reports', `chunk-upload-benchmark-${stamp}.json`), JSON.stringify(report, null, 2));
await writeFile(join('reports', `chunk-upload-benchmark-${stamp}.md`), reportMarkdown(report));
console.log(JSON.stringify(report.summary, null, 2));
