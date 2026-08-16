# AIMD Adaptive Chunk Upload

## Objective

Deliver the repository's AIMD-based adaptive chunk-upload capability as a
large-mode tracked task, with explicit spec, plan, verification evidence, and
residual-risk reporting.

## Requested Outcome

- dynamic chunk-upload window in the `4..32` range
- window tuning based on chunk RTT, failure rate, and timeout rate
- better large-file throughput than fixed concurrency
- lower timeout-driven retransmission than fixed concurrency

## Constraint

Do not overclaim benchmark results that were not reproduced in the current
environment.
