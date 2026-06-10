# Testing Guidance

## Automated Checks That Exist Today

- frontend Jest tests under `picture_bed/src/`
- compose topology validation through `docker compose config`
- repository whitespace / patch hygiene checks through `git diff --check`

## Current Practical Test Entry Points

- frontend test suite:
  `cd picture_bed && CI=true npm test -- --watchAll=false --runInBand`
- frontend production build:
  `cd picture_bed && npm run build`
- compose config validation:
  `cd docker && docker compose config > /dev/null`

## Important Gaps

- there is no standalone backend unit-test harness in the repository root
- many backend behaviors are validated through containerized integration rather
  than native unit tests
- scenario coverage for upload, share, and AI search currently lives mostly in
  `test-cases-slice1.md`, `chunked_upload.md`, and `ai_search.md`

## Rules

- Do not claim backend compile coverage unless a real backend build path ran.
- Do not claim end-to-end behavior from frontend Jest alone.
- If a manual check is used, record the exact document or command sequence.
- When tests are skipped because Docker, npm dependencies, or Linux-native
  libraries are unavailable, say so explicitly.
