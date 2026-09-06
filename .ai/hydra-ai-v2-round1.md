# HydraStore AI V2 Round 1 Self-Review

## Status versus plan

- AI/Knowledge Layer implementation is present across the new additive store,
  worker, index worker, DashScope wrapper, migration, FastCGI compatibility
  endpoints, and minimal React consumers.
- The storage data plane, AIMD upload path, Gateway worker model, and FastDFS
  placement behavior remain outside this change.
- Coding is frozen; the final verification stage has now been attempted.

## Scope and risk review

- Scope remains Level 3 because this changes a shared MySQL schema, worker
  fencing contract, search API, and container startup topology.
- The old AI/Wiki tables are retained as compatibility surfaces; new state is
  additive in `hydra_ai_v2.sql`.
- Evidence failure retains the last published generation by separating
  `current_generation` from `published_generation`.
- Search uses only the published per-user snapshot and user-filtered
  hydration; it does not rebuild or append synchronously.
- Wiki writes use a page lock, base-revision CAS, staging revision, citation
  validation, and one transaction.

## Review fixes applied

- Removed a false UTF-8 boundary heuristic from the chunker.
- Fixed nullable vector-id output, per-revision claim hydration, partial-source
  reporting, and concurrent index-generation state calculation.
- Added bounded centroid-ranked Wiki candidate/evidence selection and the
  required DashScope system/user prompt separation.
- Made deleted-source repair publish a safe revision without reviving the
  deleted document state.
- Preserved shared-reference Wiki pages and vectors during source deletion;
  only pages with zero remaining published evidence are staled.
- Separated DashScope text-generation and multimodal-generation endpoints,
  accepted no-op Wiki patches, and marked unsupported or empty sources as
  `skipped` rather than retrying them.
- Made migration password handling work for the readiness probe and made the
  AI migration checksum safe when run without an init variable.
- Fenced evidence aborts as well as evidence/Wiki publish: an expired worker
  may clean only its own staging generation and cannot overwrite the current
  document state. Lease refresh and finish/fail transitions now reject an
  already expired lease.
- Scoped source deletion invalidation to claims, revisions, and pages that
  actually cite the deleted source; unrelated citation-free records are not
  swept. Unsupported types are queued so their terminal `SKIPPED` state is
  observable, and search file matches now expose chunk IDs alongside snippets.

## Round 2 result

- No new known P0 or correctness/security P1 issue remains in the statically
  reviewed path.
- Docker backend build and C++ integration tests remain environment-blocked;
  see the final verification record for exact commands and follow-up.
