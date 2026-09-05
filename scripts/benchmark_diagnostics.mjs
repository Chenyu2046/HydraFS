const has = (text, ...terms) => terms.some(term => text.includes(term));

export function classifyBenchmarkFailure({ status = 0, timedOut = false, message = '', responseBody = '' } = {}) {
  const text = `${message} ${responseBody}`.toLowerCase();
  if (timedOut || has(text, 'timeout', 'timed out', 'aborted')) return 'timeout';
  if (has(text, 'econnreset', 'connection reset', 'socket hang up')) return 'connection_reset';
  if (has(text, 'sha256', 'payload mismatch', 'digest mismatch', 'hash')) return 'hash';
  if (has(text, 'lease lost', 'stale lease', 'lease')) return 'lease';
  if (has(text, 'claim', 'being uploaded')) return 'claim';
  if (has(text, 'metadata', 'manifest', 'backend metadata')) return 'metadata';
  if (has(text, 'circuit open', 'breaker')) return 'breaker';
  if (has(text, 'blob upload', 'fastdfs', 'blobstore failure')) return 'fastdfs';
  if (status === 429) return 'http_429';
  if (has(text, 'body', 'stream', 'content-length')) return 'body_stream';
  if (has(text, 'benchmark safety', 'circuit broken', 'too many part retries')) return 'benchmark_abort';
  return 'unknown';
}

export function summarizeBenchmarkFailures(failures = []) {
  const summary = {};
  for (const failure of failures) {
    const category = classifyBenchmarkFailure(failure);
    summary[category] = (summary[category] || 0) + 1;
  }
  return summary;
}
