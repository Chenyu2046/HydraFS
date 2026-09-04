extern "C" {
#include "cfg.h"
}
#include "metadata_store.h"
#include "storage_types.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
struct Config {
    std::string mysql_host = "127.0.0.1";
    unsigned int mysql_port = 3306;
    std::string user;
    std::string password;
    std::string database;
    std::string client;
};

Config ReadConfig() {
    Config config;
    char value[512] = {0};
    get_cfg_value(CFG_PATH, (char *)"mysql", (char *)"ip", value); config.mysql_host = value;
    get_cfg_value(CFG_PATH, (char *)"mysql", (char *)"port", value); config.mysql_port = strtoul(value, nullptr, 10);
    memset(value, 0, sizeof(value)); get_cfg_value(CFG_PATH, (char *)"mysql", (char *)"user", value); config.user = value;
    memset(value, 0, sizeof(value)); get_cfg_value(CFG_PATH, (char *)"mysql", (char *)"password", value); config.password = value;
    memset(value, 0, sizeof(value)); get_cfg_value(CFG_PATH, (char *)"mysql", (char *)"database", value); config.database = value;
    memset(value, 0, sizeof(value)); get_cfg_value(CFG_PATH, (char *)"dfs_path", (char *)"client", value); config.client = value;
    return config;
}
}

int main() {
    const Config config = ReadConfig();
    hydrastore::MetadataStore metadata(config.mysql_host, config.mysql_port, config.user,
                                       config.password, config.database);
    const std::unique_ptr<hydrastore::BlobStore> blobs = hydrastore::MakeFastDFSBlobStore(config.client.c_str());
    for (;;) {
        if (!metadata.ReconcileExpiredUploads()) {
            fprintf(stderr, "HydraStore GC reconcile failed\n");
        }
        std::vector<hydrastore::GcCandidate> candidates;
        if (metadata.ClaimGc(&candidates, 32)) {
            for (const auto &candidate : candidates) {
                const bool deleted = candidate.backend_file_id.empty() || blobs->Delete(candidate.backend_file_id);
                if (!metadata.FinishGc(candidate, deleted, deleted ? "" : "FastDFS delete failed")) {
                    fprintf(stderr, "HydraStore GC finalize failed id=%lld\n",
                            static_cast<long long>(candidate.id));
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}
