# Verification

## Run

- `git diff --check`
- `CI=true npm --prefix picture_bed test -- --watchAll=false --runInBand src/services/images.test.js`
- `npm --prefix picture_bed run build`

## Not Run

- `docker compose` based build or pressure tests

Reason:

- the current workstation session does not expose a working `docker` command
- the current workstation session does not expose `sh`, so shell-wrapper syntax
  checks were also not runnable here

## Results

- `git diff --check`: passed, CRLF conversion warnings only
- targeted AIMD Jest suite: passed, `6/6` tests
- frontend production build: passed, with pre-existing warnings in
  `FileGrid.js`, `FileList.js`, `Graph.js`, and `SharedHub.js`

## Interpretation Rule

Frontend scheduler behavior is verified by targeted tests.
Docker-backed backend build and true pressure-test KPI validation remain
follow-up work.
