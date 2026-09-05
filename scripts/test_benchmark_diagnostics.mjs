import assert from 'node:assert/strict';
import { classifyBenchmarkFailure, summarizeBenchmarkFailures } from './benchmark_diagnostics.mjs';

const cases = [
  [{ status: 429, message: 'gateway overloaded' }, 'http_429'],
  [{ status: 503, message: 'blobstore circuit open' }, 'breaker'],
  [{ status: 0, timedOut: true, message: 'The operation was aborted' }, 'timeout'],
  [{ status: 0, message: 'read ECONNRESET' }, 'connection_reset'],
  [{ status: 200, message: 'sha256 or part payload mismatch' }, 'hash'],
  [{ status: 200, message: 'part lease lost' }, 'lease'],
  [{ status: 200, message: 'part is being uploaded by another session' }, 'claim'],
  [{ status: 200, message: 'metadata backend failed' }, 'metadata'],
];

for (const [input, expected] of cases) {
  assert.equal(classifyBenchmarkFailure(input), expected, JSON.stringify(input));
}

const summary = summarizeBenchmarkFailures([
  { status: 503, message: 'blobstore circuit open' },
  { status: 0, timedOut: true, message: 'timeout' },
  { status: 0, message: 'read ECONNRESET' },
]);
assert.deepEqual(summary, { breaker: 1, timeout: 1, connection_reset: 1 });
console.log('PASS: benchmark diagnostics classification');
