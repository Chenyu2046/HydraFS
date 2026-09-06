"""Static regression checks for the HydraStore AI V2 CR hardening boundary.

This fixture intentionally performs no network, database, build, or runtime
verification. It is kept as a test-environment check for the implementation
freeze described by the technical plan.
"""

from pathlib import Path
import json
import re
import sys


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    configs = [root / "conf" / "cfg.json", root / "docker" / "fastcgi_app" / "cfg.json"]
    for path in configs:
        data = json.loads(path.read_text(encoding="utf-8"))
        assert data["dashscope"]["api_key"] == ""

    tracked = "\n".join(
        path.read_text(encoding="utf-8", errors="ignore")
        for path in root.rglob("*")
        if path.is_file() and ".git" not in path.parts
    )
    secret_pattern = re.compile(r'"api_key"\s*:\s*"sk' + r'-[A-Za-z0-9]{20,}"')
    assert not secret_pattern.search(tracked)

    ai_cgi = (root / "src_cgi" / "ai_cgi.cpp").read_text(encoding="utf-8")
    worker = (root / "src_cgi" / "knowledge_worker.cpp").read_text(encoding="utf-8")
    store = (root / "common" / "knowledge_store.cpp").read_text(encoding="utf-8")
    compiler = (root / "common" / "wiki_compiler.cpp").read_text(encoding="utf-8")
    index_worker = (root / "src_cgi" / "knowledge_index_worker.cpp").read_text(encoding="utf-8")

    assert 'getenv("DASHSCOPE_API_KEY")' in ai_cgi
    assert 'getenv("DASHSCOPE_API_KEY")' in worker
    assert "BuildValidWikiRevisionPredicate" in store
    assert ".current_revision_id=" in store and "invalid_citation" in store
    assert "ContinueTask" in store
    assert "lease_epoch=" in store and "lease_until>NOW()" in store
    assert "LoadActiveVectors(task.user" not in compiler
    assert "LoadStaleWikiForSource(task.user, task.md5, &stale_pages, 3)" in compiler
    assert "ContinueTask(context.task)" in store
    assert "dirty_generation > published_generation" in compiler
    assert "LoadWikiCandidates(task.user, static_cast<int>(kMaxWikiCandidates)" in compiler
    assert "invalid_active_vector" in index_worker
    assert "FailIndexGeneration(user, worker, generation, error.str())" in index_worker
    assert "record.dimension != dimension" in index_worker
    assert "record.dimension != dimension || record.embedding.size()" not in index_worker
    assert "LoadIndexState" in store and "ReadSingle" not in store[store.index("bool KnowledgeStore::LoadIndexState"):store.index("bool KnowledgeStore::ClaimDirtyIndex")]
    return 0


if __name__ == "__main__":
    sys.exit(main())
