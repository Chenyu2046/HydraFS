# Verification Matrix

This file maps common change areas in `AI_YunCunChu` to minimum verification.

## Standard Entry Points

Prefer these repository wrappers first:

- `scripts/ai_build.sh`
- `scripts/ai_test.sh`
- `scripts/ai_check.sh`

If the wrappers are not sufficient for the exact change, run stronger checks
directly and report them explicitly.

## Risk Matrix

| Change Area | Minimum Verification | Stronger Verification |
| --- | --- | --- |
| `picture_bed/src/` UI-only changes | `scripts/ai_test.sh` | Also run `cd picture_bed && npm run build` |
| `picture_bed/src/services/` API or upload logic | `scripts/ai_test.sh` | Also run `cd picture_bed && npm run build` and review affected backend routes |
| `src_cgi/`, `common/`, `include/` backend logic | `scripts/ai_check.sh` | Also run `scripts/ai_build.sh` or an equivalent backend-capable build path |
| `docker/*.yaml`, Dockerfiles, startup scripts, Nginx config | `scripts/ai_check.sh` | Also run `scripts/ai_build.sh` |
| upload, share, AI search, or other cross-stack workflows | `scripts/ai_check.sh` plus targeted docs review | Also run `scripts/ai_build.sh` and a manual flow from `test-cases-slice1.md`, `chunked_upload.md`, or `ai_search.md` |
| repo workflow files such as `AGENTS.md`, `docs/ai/*`, `.ai/*`, `scripts/ai_*` | `scripts/ai_check.sh` | Also inspect the generated guidance for consistency with current repo facts |

## Completion Standard

Before claiming completion, record:

- which commands were run
- what passed
- what could not be run
- whether the remaining risk needs follow-up
