# HydraStore｜高性能分布式对象存储系统

## Status

This document is the design baseline for the V2 migration of the current
`Nginx + FastCGI + Redis + MySQL + FastDFS` application. It records intended
semantics; verification status belongs in the task-local `.ai/` records and in
the final test report.

## Current baseline

The current upload path is `chunk_init -> chunk_upload -> chunk_merge`.
Redis stores upload progress, `chunk_upload` buffers the whole request in
memory and writes `/tmp/chunks`, and `chunk_merge` creates one FastDFS appender
file. `file_info` stores one legacy `file_id`/`url` per whole file. This path is
kept for backward compatibility while the new object API is introduced.

## V2 logical model

```text
upload_session
  -> upload_part(index -> chunk_blob)
  -> object_manifest
       -> manifest_chunk(index -> chunk_blob)
  -> file_info(storage_mode=manifest, manifest_id, object_id)
```

`chunk_blob` is content addressed by `UNIQUE(sha256, size)`. Its lifecycle is
`UPLOADING -> READY -> GC_PENDING -> DELETING`; `owner_upload_id` and
`lease_until` protect an in-progress physical write. `ref_count` is changed in
the same transaction as manifest relationship changes. A physical blob is not
visible to a manifest until it is `READY`.

## Upload and commit semantics

1. `object/init` authenticates the user, creates or resumes an upload session,
   and records ordered `(index, size, sha256)` part metadata in MySQL.
2. For each part, the unique key is the race boundary. A `READY` row is reused;
   an unexpired `UPLOADING` row is reported as waiting; an expired owner lease
   can be taken over. The winner streams the request into FastDFS while hashing
   incrementally, then marks the row `READY` only after the computed SHA-256 and
   byte count match.
3. `object/commit` locks the session, verifies every declared part is `READY`,
   creates the manifest and relationships, increments references, creates the
   user-visible file relation, and marks the session `COMMITTED` in one MySQL
   transaction. A committed session returns its stored object result on repeat.
4. `object/download` reads the manifest in index order and streams each blob
   directly to FastCGI output. Legacy rows continue using their existing URL.

FastDFS failures after a physical write are repaired by deleting the newly
created blob before the part becomes `READY`. Metadata failures leave no
visible manifest; the orphan physical blob is later handled by the lifecycle
worker after its owner lease expires.

## GC and recovery

The GC worker selects only rows with `ref_count=0`, `state=GC_PENDING`, an
expired `gc_after`, no active upload-part/session owner, and a retry time that
has arrived. It atomically claims `DELETING`, deletes the FastDFS blob, then
removes the metadata row. Delete failure records retry count and exponential
backoff. Session expiry, stale upload leases, aborted uploads, and orphan
`READY` rows are separate recovery scans; none may delete a referenced blob.

## Gateway and deployment

The storage gateway is a stateless FastCGI endpoint. MySQL owns durable state;
Redis is an external service for auth/cache/short-lived locks. Two gateway
instances sit behind Nginx `least_conn` with failure thresholds. Upload requests
disable unnecessary FastCGI request buffering; responses use streaming output.
`STORAGE_GATEWAY_WORKERS` bounds gateway process concurrency. FastDFS placement
remains the backend's responsibility; HydraStore does not implement hashing or
replication placement.

## Compatibility and observability

Existing `chunk_*`, legacy upload, list, share, delete, and knowledge-worker
consumers remain readable during migration. New metadata exposes `storage_mode`
and `manifest_id`; consumers choose logical download for manifest objects and
the existing URL for legacy objects. Storage logs contain request/upload/object
identifiers, a hash prefix, part index, gateway instance, and stage latencies;
they never contain tokens, passwords, or full sensitive hashes.

## Deliberate simplifications

- No new queue: FastCGI workers provide bounded concurrency.
- No Raft, CRUSH, DPDK, eBPF, io_uring, or custom wire protocol.
- HTTP Range is deferred until ordered streaming, integrity, recovery, and
  failover tests are green.
