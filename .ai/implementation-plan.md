# Implementation Plan

1. Audit the current repository state against the requested AIMD feature.
   Verify: confirm window bounds, tuning signals, retry behavior, and backend
   concurrency hardening from real files.

2. Promote the task into large-mode artifacts.
   Verify: `.ai/` contains spec, scope, plan, affected-files, run-trace,
   verification, evaluation, and review records.

3. Add durable performance guidance without overclaiming benchmark evidence.
   Verify: `docs/ai/performance.md` and related docs distinguish target KPIs
   from locally verified evidence.

4. Re-run targeted verification and record residual risk.
   Verify: targeted AIMD Jest suite, frontend build, `git diff --check`, and
   explicit not-run reasons for Docker-backed checks.
