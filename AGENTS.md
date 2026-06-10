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
