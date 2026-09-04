# AGENTS.md

## Purpose

This repository uses an adapted `Auto_AICoding_Harness` small-mode workflow.

The goal is to make AI agents reliable in this repository without pretending
that this project is a generic `C++ / CMake` template. All guidance must follow
the real stack here: `Docker Compose + FastCGI C/C++ + React`.

## Repository Map

- `src_cgi/`, `common/`, `include/`: backend FastCGI and shared C/C++ code
- `picture_bed/`: React frontend
- `docker/`: the primary full-stack build and runtime entrypoint
- `conf/`: local configuration samples
- `README.md`, `chunked_upload.md`, `ai_search.md`, `test-cases-slice1.md`:
  durable design and verification references

## Required Reading

Read `docs/ai/README.md` before any non-trivial task.

Then read the relevant project knowledge file:

- architecture or boundaries: `docs/ai/architecture.md`
- build or deployment changes: `docs/ai/build.md`
- testing or regression work: `docs/ai/testing.md`
- task verification depth: `docs/ai/verification-matrix.md`

Always read active `.ai/` task files when they exist.

## Task Contract

Before editing, state:

1. proposed execution level
2. target outcome
3. expected file or module scope
4. planned verification
5. known uncertainties or blockers

## Execution Levels

- `Level 1`: local, bounded, easy rollback, quick targeted verification
- `Level 2`: one bounded workflow or subsystem, multi-step work, self-review
  required before completion
- `Level 3`: shared interfaces, cross-service contracts, risky migrations, or
  rollback that is not easy in the current session

Escalate when scope expands, rollback gets harder, or verification confidence
drops.

## Project-Specific Rules

- Prefer narrow edits over cross-cutting cleanup.
- Do not treat `docker/` changes as isolated if they affect build, runtime, or
  service wiring.
- Do not assume local Windows can compile backend binaries directly; the
  full-stack build path is `docker compose build`.
- Frontend-only verification can use `picture_bed` tests and build commands
  without rebuilding the entire stack when the change is clearly isolated.
- Durable project knowledge belongs in `docs/ai/*`.
- Current task runtime, plans, and state belong in `.ai/*`.

## Safety

- preserve unrelated user changes
- do not overwrite managed files blindly
- do not install dependencies from helper scripts
- do not mark work complete without verification
- do not invent backend behavior that is not grounded in the repo docs or code

## Verification

Use `docs/ai/verification-matrix.md` to pick the minimum acceptable checks for
the change you are making.

Before completion, state:

1. what was verified
2. how it was verified
3. what remains unverified and why
4. what follow-up is needed for each meaningful unverified item

## HydraStore V2 Principles

- The product name is `HydraStore｜高性能分布式对象存储系统`.
- MySQL is authoritative for upload sessions, manifests, chunk metadata,
  references, and lifecycle state. Redis is limited to tokens, cache, and
  ephemeral coordination.
- New objects use immutable content-addressed chunks plus an ordered manifest;
  legacy `file_info.file_id` objects remain readable through the legacy path.
- A physical chunk is published before metadata makes it visible. The
  database unique key `(sha256, size)`, owner lease, and idempotent state
  transitions are the concurrency boundary for deduplication.
- Commit is one metadata transaction. No file is visible before the session is
  committed, and repeating commit must return the same committed object without
  incrementing references twice.
- GC is grace-period based and must lock/check active upload references before
  moving `GC_PENDING` objects to `DELETING`. Physical deletion is retryable.
- FastDFS is a BlobStore backend, not the logical object model. New upload and
  download paths must stream with bounded memory and must not use
  `/tmp/chunks` or appender merge.

## HydraStore V2 Test Standard

For every claimed behavior, first write a test that would fail if the behavior
were only happy-path code. Required adversarial cases include concurrent same
chunk upload, duplicate part retry, duplicate commit, transaction failure,
stale lease takeover, shared-reference deletion, orphan GC, crash/resume, and
gateway failover. A passing happy-path request is not completion evidence.

## HydraStore V2 Prohibited Shortcuts

- Do not make Redis the source of truth, add a second custom placement system,
  or reintroduce whole-file FastDFS appender merge for new objects.
- Do not claim benchmark numbers, multi-storage distribution, failover, or
  crash recovery without running the corresponding reproducible test.
- Do not delete or rewrite legacy data as part of migration. Migrations must be
  repeatable and additive.
