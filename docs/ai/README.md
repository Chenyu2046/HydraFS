# docs/ai

This directory stores long-lived AI-facing project knowledge for
`AI_YunCunChu`.

Read this directory before non-trivial work. Do not use it for one-off task
notes.

## Start Here

- `architecture.md`: container topology, code ownership, and key boundaries
- `build.md`: real build and deploy entrypoints for this repository
- `testing.md`: automated and manual verification paths that actually exist
- `verification-matrix.md`: map change risk to minimum checks

## Source Documents

These files are current repository facts and should be treated as primary
references when updating `docs/ai/*`:

- `README.md`
- `chunked_upload.md`
- `ai_search.md`
- `test-cases-slice1.md`

## Reading Guide By Change Type

- Docker, compose, Nginx, container wiring: `architecture.md`, `build.md`,
  `verification-matrix.md`
- FastCGI C/C++ logic: `architecture.md`, `testing.md`,
  `verification-matrix.md`
- React UI or API glue: `build.md`, `testing.md`, `verification-matrix.md`
- Cross-stack features such as upload, share, or AI search: read all files in
  this directory and the relevant root design documents first

Task runtime state does not belong here.
Task runtime belongs in `.ai/`.
