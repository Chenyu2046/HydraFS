#ifndef HYDRASTORE_STORAGE_RESILIENCE_H
#define HYDRASTORE_STORAGE_RESILIENCE_H

#include <cstdint>
#include <memory>
#include <string>

namespace hydrastore {

class BlobStore;

struct BreakerDecision {
    bool allowed = true;
    const char *reason = nullptr;
    std::uint64_t probe_generation = 0;
};

class BlobStoreCircuitBreaker {
public:
    BlobStoreCircuitBreaker();
    ~BlobStoreCircuitBreaker();
    BlobStoreCircuitBreaker(const BlobStoreCircuitBreaker &) = delete;
    BlobStoreCircuitBreaker &operator=(const BlobStoreCircuitBreaker &) = delete;

    BreakerDecision Allow();
    void Record(bool success, bool timed_out, std::int64_t elapsed_ms,
                std::uint64_t probe_generation = 0);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<BlobStore> MakeFaultInjectingBlobStore(std::unique_ptr<BlobStore> inner);

}  // namespace hydrastore

#endif
