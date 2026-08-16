# Performance Guidance

## Purpose

This file defines how to evaluate performance-sensitive upload changes in
`AI_YunCunChu`.

## Target Workflow

The main current performance-sensitive workflow is large-file chunk upload:

- frontend orchestrator: `picture_bed/src/services/images.js`
- chunk receiver: `src_cgi/chunk_upload_cgi.c`
- merge path: `src_cgi/chunk_merge_cgi.c`
- worker fan-out: `docker/fastcgi_app/start.sh`

## Benchmark Rule

Do not claim throughput or retransmission improvement without a reproducible
comparison against a fixed-window baseline.

## Recommended Comparison

Use the same file set, environment, and backend worker count, then compare:

### Fixed-Window Baseline

- `INITIAL_CONCURRENCY=8`
- `MIN_CONCURRENCY=8`
- `MAX_CONCURRENCY=8`

### AIMD Target

- `INITIAL_CONCURRENCY=4`
- `MIN_CONCURRENCY=4`
- `MAX_CONCURRENCY=32`

## Metrics To Capture

- file size and chunk count
- total upload duration
- average throughput
- timeout count
- retry count
- timeout-driven retransmission rate
- backend worker count

When possible, also capture:

- P50 / P95 chunk RTT
- frontend peak in-flight chunk count
- backend error count during upload and merge

## Current Requirement KPI

The current user requirement sets these benchmark targets:

- throughput improvement: about `30%`
- timeout retransmission reduction: about `40%`

Treat them as benchmark targets until reproduced in an environment that can run
the full upload stack.
