#!/usr/bin/env node
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';

const password = process.env.HYDRA_DB_PASSWORD;
const container = process.env.HYDRA_DB_CONTAINER || 'tc_fcgi_mysql';
const execFileAsync = promisify(execFile);
if (!password) {
  console.error('NOT RUN: set HYDRA_DB_PASSWORD');
  process.exit(2);
}

const checks = {
  ref_count_mismatch: `SELECT COUNT(*) FROM chunk_blob c LEFT JOIN (SELECT mc.chunk_id, COUNT(*) refs FROM manifest_chunk mc JOIN object_manifest m ON m.id=mc.manifest_id WHERE m.state='COMMITTED' GROUP BY mc.chunk_id) r ON r.chunk_id=c.id WHERE c.ref_count<>COALESCE(r.refs,0)`,
  committed_manifest_orphan: `SELECT COUNT(*) FROM object_manifest m LEFT JOIN upload_session s ON s.manifest_id=m.id AND s.state='COMMITTED' WHERE m.state='COMMITTED' AND s.id IS NULL`,
  committed_manifest_relation_mismatch: `SELECT COUNT(*) FROM object_manifest m LEFT JOIN (SELECT mc.manifest_id, COUNT(*) chunk_count, COALESCE(SUM(mc.size),0) total_size FROM manifest_chunk mc GROUP BY mc.manifest_id) r ON r.manifest_id=m.id WHERE m.state='COMMITTED' AND (COALESCE(r.chunk_count,0)<>m.chunk_count OR COALESCE(r.total_size,0)<>m.total_size)`,
  active_upload_using_deleting_chunk: `SELECT COUNT(*) FROM upload_session s JOIN upload_part p ON p.upload_id=s.id JOIN chunk_blob c ON c.id=p.chunk_id WHERE s.state IN ('INIT','UPLOADING','COMMITTING') AND c.state='DELETING'`,
  failed_chunk_with_backend_file: `SELECT COUNT(*) FROM chunk_blob WHERE state='FAILED' AND backend_file_id IS NOT NULL AND backend_file_id<>''`,
  ready_without_backend_file: `SELECT COUNT(*) FROM chunk_blob WHERE state='READY' AND (backend_file_id IS NULL OR backend_file_id='')`,
  manifest_references_non_ready: `SELECT COUNT(*) FROM manifest_chunk mc JOIN chunk_blob c ON c.id=mc.chunk_id WHERE c.state<>'READY'`,
  committing_upload_with_non_ready_part: `SELECT COUNT(DISTINCT s.id) FROM upload_session s JOIN upload_part p ON p.upload_id=s.id WHERE s.state='COMMITTING' AND p.state<>'READY'`,
  committing_upload_missing_backend: `SELECT COUNT(DISTINCT s.id) FROM upload_session s JOIN upload_part p ON p.upload_id=s.id JOIN chunk_blob c ON c.id=p.chunk_id WHERE s.state='COMMITTING' AND (c.state<>'READY' OR c.backend_file_id IS NULL OR c.backend_file_id='')`,
  committed_session_without_manifest: `SELECT COUNT(*) FROM upload_session WHERE state='COMMITTED' AND (manifest_id IS NULL OR object_id IS NULL OR object_id='')`,
};

const values = {};
for (const [name, sql] of Object.entries(checks)) {
  const { stdout } = await execFileAsync('docker', [
    'exec', '-e', `MYSQL_PWD=${password}`, container, 'mysql', '-uroot', '-N', '-D', 'yuncunchu', '-e', sql,
  ], { windowsHide: true, maxBuffer: 1024 * 1024 });
  values[name] = Number(stdout.trim());
}
const failed = Object.entries(values).filter(([, value]) => value !== 0);
console.log(JSON.stringify({ status: failed.length ? 'FAIL' : 'PASS', checks: values }, null, 2));
if (failed.length) process.exitCode = 1;
