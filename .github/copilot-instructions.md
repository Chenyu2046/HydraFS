# Copilot Instructions

Read `AGENTS.md` before making changes.

For non-trivial tasks, read `docs/ai/README.md` and the relevant file in
`docs/ai/` before editing.

Task classification rules:

- `Level 1`: local change with quick verification
- `Level 2`: bounded multi-step work with a self-review checkpoint
- `Level 3`: shared contracts, risky refactors, cross-service behavior, or
  expensive rollback

Do not store durable project facts in this file.

- durable project knowledge: `docs/ai/*`
- current task runtime: `.ai/*`
- repository workflow entrypoint: `AGENTS.md`
