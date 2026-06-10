# Spec

## Problem

Large-file chunk uploads should not stay on a fixed concurrency level when
network and backend conditions vary. Fixed concurrency either leaves bandwidth
unused or pushes the system into timeout-heavy retry behavior.

## Functional Requirement

1. Replace fixed chunk-upload concurrency with an AIMD-style window.
2. Window range must be bounded to `4..32`.
3. Adjustment inputs must include:
   - per-chunk RTT
   - recent failure rate
   - recent timeout rate
4. Healthy samples should increase the window gradually.
5. Failures, timeouts, or degraded RTT should reduce the window aggressively.
6. Per-chunk retries and timeout cancellation must remain bounded.
7. Backend correctness must hold under concurrent chunk uploads and merge
   requests.

## Performance Target

These numbers come from the user requirement and are treated as target KPIs for
pressure testing, not as locally reproduced measurements in this run:

- large-file upload throughput: about `+30%` versus a fixed-concurrency
  baseline
- timeout retransmission rate: about `-40%` versus a fixed-concurrency
  baseline

## Compatibility Constraints

- existing HTTP endpoints remain unchanged:
  `chunk_init`, `chunk_upload`, `chunk_merge`
- chunk threshold and chunk size behavior remain unchanged unless explicitly
  changed later
- progress reporting must stay monotonic

## Verification Standard

- targeted frontend scheduler regression tests
- targeted production build check for the frontend
- code-path inspection for backend concurrent upload and merge safety
- explicit note of benchmark work not run locally
