# HydraStore V2 verification record

## Level 3 self-review

- Status: implementation and adversarial checks reached the planned bounded V2 scope.
- Scope change: added durable metadata/blob/gateway/GC paths, dual Gateway deployment, and frontend object-upload path; legacy APIs remain for compatibility.
- New risks found and addressed: FastCGI output, Compose DNS cycle, shared-part state propagation, failed-part retry, delete affected-row handling, ref-count update checks, and GC foreign-key cleanup.
- Decision: keep Level 3; finish with explicit residual-risk reporting.

## Verified

- `docker compose -f docker/docker-compose.yaml config --quiet` passed.
- Docker builds for `storage_gateway_1`, `storage_gateway_2`, `storage_gc_worker`, and `nginx_fastdfs` passed.
- Frontend targeted Jest: 7 tests passed.
- Node syntax checks for the V2 test and benchmark scripts passed.
- Live Compose stack reached healthy Nginx, MySQL, Redis, FastDFS storage, and both Gateway processes.
- Live HTTP adversarial runner passed concurrent dedup, duplicate commit, wrong-payload rejection, retry, and incomplete commit.
- Live delete changed the manifest relation and chunk to `GC_PENDING`; forced-expiry GC removed the physical FastDFS file and DB candidate.
- With `storage_gateway_1` stopped, Nginx served a valid V2 init through `storage_gateway_2`; the stopped Gateway was then restarted.

## Not verified

- The benchmark script was not run; no throughput, latency, CPU, memory, or dedup-savings numbers are claimed.
- `HYDRA_FAILPOINT=after_blob_put` and `HYDRA_FAILPOINT=after_db_commit_before_response` were not run through a dedicated injected container. The durable orphan journal and commit-response-loss recovery remain follow-up work.
- No long-running lease-expiry test or physical multi-storage distribution/failure test was run.
