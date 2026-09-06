# HydraStore AI V2 Round 0 Review

## Scope

The attached technical plan is treated as the requested implementation
specification. The implementation is limited to the AI / Knowledge Layer;
storage data-plane, AIMD upload tuning, Gateway worker changes, and FastDFS
capacity work are out of scope.

## Findings

- `knowledge_worker.cpp` uses a non-atomic select/update claim and resets all
  running tasks on startup.
- `ai_cgi.cpp` performs index and database writes during search and hydrates
  each FAISS result with a separate SQL query.
- `faiss_wrapper.cpp` exposes a process-global positional index and cannot
  provide stable IDs or versioned read-only snapshots.
- Existing `wiki_page` / `wiki_link` are file-level compatibility tables.
- API key and request-body logging must not cross the new path.

## Frozen decisions

- New AI tables live in `docker/mysql/hydra_ai_v2.sql`; legacy tables remain
  readable and are not rewritten or deleted by migration.
- `KnowledgeStore` owns task lease/fencing, evidence generation publish,
  revision CAS, source invalidation, index state, and bounded hydration.
- Evidence chunks and wiki revisions use stable `knowledge_vector.id` labels;
  search reads only a published snapshot and never rebuilds it.
- Wiki patches are JSON-only, deterministic-validated, citation-bounded, and
  published in one transaction.
- Public APIs retain the existing `cmd` names; new fields are additive.

## Review status

Round 0: complete. Contract freeze: complete. No runtime tests or builds were
run during the review/coding stages, as required by the attached plan.
