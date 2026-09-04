# HydraStore V2 Implementation Plan

## Working rule

Every stage begins with a test designed to break a happy-path implementation.
The stage is not complete until the targeted test runs, or the exact
environment blocker is recorded. No benchmark value is written before it is
measured.

## Stages

1. **Contract and schema** — add repeatable additive migration and storage
   types. Verify clean-schema creation, rerun safety, legacy columns, unique
   chunk identity, and indexes.
2. **Storage core** — add BlobStore interface, FastDFS callback implementation,
   incremental SHA-256, and metadata/service boundaries. Verify hash mismatch
   cleanup, bounded callback reads, and duplicate state transitions with a fake
   BlobStore.
3. **Object gateway** — add init/part/status/commit/abort/download endpoints;
   wire the Makefile and startup script. Verify concurrent dedup, stale lease
   takeover, duplicate part retry, duplicate commit, commit failure atomicity,
   and crash-before-commit recovery.
4. **GC** — add a standalone worker and retry/backoff path. Verify shared chunk
   deletion safety, last-reference grace-period deletion, active-upload
   protection, orphan cleanup, and delete retry.
5. **Deployment** — split Redis, run two stateless gateways, configure Nginx
   balancing/buffering/health behavior, and add a multi-storage profile. Verify
   cross-gateway resume, gateway failover, Nginx config, and distribution.
6. **Frontend and compatibility** — switch only the new upload path to object
   APIs while preserving AIMD, file listing, sharing, download, and AI
   consumers. Verify exact duplicates, partial overlap, 960 MiB integrity, and
   legacy regression.
7. **Adversarial review and benchmark** — run implementation review, repair
   findings, run the full integration matrix, run final adversarial review,
   then collect 240/960 MiB measurements for 0/50/100% sharing. Report only
   measured values and explicit not-run items.

## Initial acceptance matrix

| Behavior | Break test | Required invariant |
|---|---|---|
| Concurrent dedup | two writers race on one `(sha256,size)` | one logical row and one physical blob |
| `ref_count` | delete one of two manifests | shared blob remains referenced |
| Atomic commit | fail one metadata operation | no visible partial object and no ref leak |
| GC | run before grace / with active upload | blob is not deleted |
| Crash recovery | kill after blob put or before commit | resume/takeover repairs state |
| Gateway failover | route adjacent parts to two instances, stop one | durable session resumes and commits |
