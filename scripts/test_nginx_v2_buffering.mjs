import assert from 'node:assert/strict';
import fs from 'node:fs';

const config = fs.readFileSync(new URL('../docker/nginx_fastdfs/nginx.conf', import.meta.url), 'utf8');
const compose = fs.readFileSync(new URL('../docker/docker-compose.yaml', import.meta.url), 'utf8');
const v2Location = config.match(/location \^~ \/api\/object\/ \{([\s\S]*?)\n        \}/)?.[1] || '';
assert.match(v2Location, /fastcgi_request_buffering\s+on;/,
  'V2 requests must be buffered before a busy FastCGI worker is selected');
assert.doesNotMatch(v2Location, /fastcgi_request_buffering\s+off;/);
assert.doesNotMatch(v2Location, /fastcgi_next_upstream[^;]*\bnon_idempotent\b/,
  'Nginx must not replay non-idempotent V2 POST bodies');
assert.equal((compose.match(/STORAGE_GATEWAY_WORKERS:\s*"16"/g) || []).length, 2,
  'both Gateway instances must provide one worker per M4 request slot');
console.log('PASS: V2 FastCGI request buffering regression');
