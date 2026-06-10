# Evaluation

## Status

The requested AIMD adaptive chunk-upload behavior is already implemented in the
current repository codebase and has now been formalized under large mode.

## Verified Against Request

- adaptive concurrency window bounded to `4..32`
- tuning inputs include RTT, failure rate, and timeout rate
- additive increase and multiplicative decrease behavior exists
- per-chunk timeout and retry controls exist
- backend chunk publication is idempotent under concurrent upload attempts
- backend merge is guarded by a Redis lock

## Not Verified In This Run

- the user-specified pressure-test KPI of about `+30%` throughput
- the user-specified pressure-test KPI of about `-40%` timeout retransmission
- Docker-backed backend build / full-stack upload pressure test

These were not reproduced locally because Docker-backed full-stack execution and
pressure testing are unavailable in this session. Fresh environment checks in
this run returned `docker-not-found` and `sh-not-found`.

## Recommended Follow-Up

Run a reproducible benchmark in a Docker-capable environment using:

- fixed-window baseline: `INITIAL=8`, `MIN=8`, `MAX=8`
- AIMD target: `INITIAL=4`, `MIN=4`, `MAX=32`

Compare:

- wall-clock upload duration
- average throughput
- timeout count
- retry count
- timeout-driven retransmission rate
