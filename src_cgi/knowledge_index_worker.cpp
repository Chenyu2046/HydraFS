#include "faiss_snapshot.h"
#include "hash_util.h"
#include "knowledge_store.h"

extern "C" {
#include "cfg.h"
#include "make_log.h"
}

#include <algorithm>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>
#include <signal.h>

namespace {
volatile sig_atomic_t running = 1;
void Stop(int) { running = 0; }

std::string Config(const char *section, const char *key, const char *fallback = "") {
    char value[1024] = {0};
    if (get_cfg_value(CFG_PATH, const_cast<char *>(section), const_cast<char *>(key), value) != 0) return fallback;
    return value;
}

std::string UserDirectory(const std::string &root, const std::string &user) {
    hydrastore::Sha256 sha;
    sha.Update(user.data(), user.size());
    return root + "/" + sha.FinalHex();
}
}

int main() {
    signal(SIGTERM, Stop); signal(SIGINT, Stop);
    const std::string host = Config("mysql", "ip", "db");
    const unsigned int port = static_cast<unsigned int>(std::strtoul(Config("mysql", "port", "3306").c_str(), nullptr, 10));
    hydrastore::KnowledgeStore store(host, port, Config("mysql", "user"), Config("mysql", "password"), Config("mysql", "database", "yuncuchu"));
    if (!store.Connect()) return 1;
    const std::string root = Config("faiss", "user_index_dir", "/data/faiss/users");
    const int dimension = std::max(1, std::atoi(Config("dashscope", "embedding_dimension", "1024").c_str()));
    const std::string worker = "index:" + std::to_string(static_cast<long long>(getpid()));
    while (running) {
        std::string user;
        std::int64_t generation = 0;
        if (!store.ClaimDirtyIndex(worker, &user, &generation)) { sleep(2); continue; }
        std::vector<hydrastore::KnowledgeVectorRecord> records;
        if (!store.LoadActiveVectors(user, &records)) {
            store.FailIndexGeneration(user, worker, generation, "active vector load failed");
            continue;
        }
        std::vector<std::int64_t> ids;
        std::vector<std::vector<float>> vectors;
        for (const auto &record : records) {
            if (record.dimension != dimension || record.embedding.size() != static_cast<std::size_t>(dimension)) continue;
            ids.push_back(record.id); vectors.push_back(record.embedding);
        }
        const std::string path = UserDirectory(root, user) + "/vectors." + std::to_string(generation) + ".faiss";
        if (!hydrastore::FaissSnapshot::Build(path, dimension, ids, vectors)) {
            store.FailIndexGeneration(user, worker, generation, "FAISS snapshot build failed");
            continue;
        }
        if (!store.PublishIndexGeneration(user, worker, generation)) {
            store.FailIndexGeneration(user, worker, generation, "index generation superseded or publish failed");
        }
    }
    return 0;
}
