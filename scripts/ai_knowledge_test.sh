#!/usr/bin/env sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"

cd "$REPO_ROOT/docker"
docker compose run --rm --no-deps fastcgi_app /app/bin_cgi/knowledge_chunker_test
docker compose run --rm --no-deps fastcgi_app /app/bin_cgi/wiki_patch_validator_test
docker compose run --rm --no-deps fastcgi_app /app/bin_cgi/knowledge_multi_source_wiki_test
docker compose run --rm --no-deps fastcgi_app /app/bin_cgi/document_extractor_timeout_test
docker compose run --rm --no-deps fastcgi_app /app/bin_cgi/faiss_snapshot_test

if [ "${HYDRA_RUN_DB_TEST:-0}" = "1" ]; then
    docker compose run --rm --no-deps fastcgi_app /app/bin_cgi/knowledge_task_claim_test
    docker compose run --rm --no-deps fastcgi_app /app/bin_cgi/knowledge_repair_batch_test
fi
