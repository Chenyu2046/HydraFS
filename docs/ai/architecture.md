# Architecture Notes

## System Shape

This repository is a three-container application orchestrated by
`docker/docker-compose.yaml`.

- `tc_fcgi_mysql`: MySQL 8.0, exposed as `3307 -> 3306`
- `tc_fcgi_nginx_fastdfs`: Nginx + FastDFS, exposed as `80` and `443`
- `tc_fcgi_app`: Redis + FastCGI application processes, exposed as
  `10000-10012`

The default runtime path is browser -> Nginx -> FastCGI -> MySQL / Redis /
FastDFS / DashScope / FAISS.

## Code Ownership Boundaries

- `src_cgi/`: endpoint-specific FastCGI entrypoints such as login, upload,
  chunked upload, share, and AI search
- `common/`: shared utilities for config, MySQL, Redis, logging, hashing,
  DashScope, FAISS, and knowledge-task helpers
- `include/`: public headers consumed by CGI and shared modules
- `picture_bed/src/`: React pages, components, services, and auth context
- `docker/`: image definitions, runtime scripts, compose topology, and service
  health checks

## Important Operational Facts

- Backend binaries are built inside the Docker image via `/app/Makefile`.
- The frontend production bundle is built during the
  `docker/nginx_fastdfs/dockerfile` image build.
- `docker/fastcgi_app/start.sh` is the runtime process map for FastCGI ports.
- Chunk upload concurrency behavior and AI search flow already have durable
  design docs in `chunked_upload.md` and `ai_search.md`.

## Change Boundaries

- A change in `docker/` can affect build, runtime, networking, and service
  readiness across the whole stack.
- A change in `common/` often affects multiple CGI endpoints, even if only one
  handler is being edited.
- A change in `picture_bed/src/services/` can change contract assumptions
  between the frontend and FastCGI routes.

## HydraStore AI V2 Knowledge Layer

The V2 AI path is separate from the storage data plane. `ai_parse_task` is a
MySQL-backed queue claimed with `FOR UPDATE SKIP LOCKED`; leases are fenced by
`worker_id` and `lease_epoch`. `knowledge_worker` extracts bounded UTF-8
evidence, creates deterministic overlapping chunks, stores staging vectors,
and publishes one evidence generation atomically. `knowledge_index_worker`
builds immutable per-user `IndexIDMap2(IndexFlatIP)` snapshots using
`knowledge_vector.id` as the stable label, then advances
`knowledge_index_state.published_generation`.

`ai_cgi` only reads the published generation during search and hydrates FAISS
IDs with user-filtered SQL. Wiki compilation is an asynchronous second task:
the model returns a bounded JSON patch, citations are checked against the
published source chunks, and revisions are published with a page-level CAS
transaction. The old `user_file_ai_desc`, `wiki_page`, and `wiki_link` tables
remain compatibility surfaces; new V2 state lives in the additive
`docker/mysql/hydra_ai_v2.sql` migration.
