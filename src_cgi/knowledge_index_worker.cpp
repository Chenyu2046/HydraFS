#include "faiss_snapshot.h"
#include "hash_util.h"
#include "knowledge_store.h"

extern "C" {
#include "cfg.h"
#include "make_log.h"
}

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
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
        std::int64_t lease_epoch = 0;
        if (!store.ClaimDirtyIndex(worker, &user, &generation, &lease_epoch)) { sleep(2); continue; }
        std::vector<hydrastore::KnowledgeVectorRecord> records;
        if (!store.LoadActiveVectors(user, &records)) {
            if (!store.FailIndexGeneration(user, worker, generation, lease_epoch, "active vector load failed")) {
                LOG("cgi", "knowledge_index_worker", "user_hash=%s generation=%lld epoch=%lld stage=fail_fenced\n", UserDirectory(root, user).c_str(), static_cast<long long>(generation), static_cast<long long>(lease_epoch));
            }
            continue;
        }
        std::vector<std::int64_t> ids;
        std::vector<std::vector<float>> vectors;
        for (const auto &record : records) {
            bool invalid = record.id <= 0 || record.dimension != dimension ||
                           record.embedding.size() != static_cast<std::size_t>(dimension) ||
                           record.embedding_bytes != record.embedding.size() * sizeof(float);
            double norm_squared = 0.0;
            for (const float value : record.embedding) {
                if (!std::isfinite(value)) invalid = true;
                norm_squared += static_cast<double>(value) * value;
            }
            if (!std::isfinite(norm_squared) || norm_squared <= 0.0) invalid = true;
            if (invalid) {
                std::ostringstream error;
                error << "invalid_active_vector invalid_vector_id=" << record.id
                      << " expected_dimension=" << dimension
                      << " actual_dimension=" << record.dimension
                      << " actual_size=" << record.embedding.size()
                      << " actual_bytes=" << record.embedding_bytes;
                if (!store.FailIndexGeneration(user, worker, generation, lease_epoch, error.str())) {
                    LOG("cgi", "knowledge_index_worker", "user_hash=%s generation=%lld epoch=%lld stage=fail_fenced\n", UserDirectory(root, user).c_str(), static_cast<long long>(generation), static_cast<long long>(lease_epoch));
                }
                ids.clear();
                vectors.clear();
                invalid = true;
                break;
            }
            ids.push_back(record.id); vectors.push_back(record.embedding);
        }
        if (ids.size() != records.size()) continue;
        const std::string path = UserDirectory(root, user) + "/vectors." + std::to_string(generation) + "." + std::to_string(lease_epoch) + ".faiss";
        if (!hydrastore::FaissSnapshot::Build(path, dimension, ids, vectors)) {
            if (!store.FailIndexGeneration(user, worker, generation, lease_epoch, "FAISS snapshot build failed")) {
                LOG("cgi", "knowledge_index_worker", "user_hash=%s generation=%lld epoch=%lld stage=fail_fenced\n", UserDirectory(root, user).c_str(), static_cast<long long>(generation), static_cast<long long>(lease_epoch));
            }
            continue;
        }
        if (!store.PublishIndexGeneration(user, worker, generation, lease_epoch)) {
            if (!store.FailIndexGeneration(user, worker, generation, lease_epoch, "index generation superseded or publish failed")) {
                LOG("cgi", "knowledge_index_worker", "user_hash=%s generation=%lld epoch=%lld stage=publish_fenced\n", UserDirectory(root, user).c_str(), static_cast<long long>(generation), static_cast<long long>(lease_epoch));
            }
        }
    }
    return 0;
}
