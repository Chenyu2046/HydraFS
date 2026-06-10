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
