#include "faiss_snapshot.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

int main() {
    const std::string path = "/tmp/hydra_ai_snapshot_test.faiss";
    const std::vector<std::int64_t> ids = {101, 205};
    const std::vector<std::vector<float>> vectors = {{1.0f, 0.0f}, {0.0f, 1.0f}};
    assert(hydrastore::FaissSnapshot::Build(path, 2, ids, vectors));
    hydrastore::FaissSnapshot snapshot;
    assert(snapshot.Load(path, 2));
    std::vector<std::int64_t> result_ids;
    std::vector<float> scores;
    assert(snapshot.Search({0.9f, 0.1f}, 2, &result_ids, &scores));
    assert(!result_ids.empty() && result_ids[0] == 101);
    std::remove(path.c_str());
    return 0;
}
