#include "storage_resilience.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <unistd.h>

int main() {
    const std::string namespace_name = "hydrastore-test-" + std::to_string(static_cast<long long>(getpid()));
    setenv("HYDRA_BREAKER_NAMESPACE", namespace_name.c_str(), 1);
    setenv("HYDRA_BREAKER_OPEN_MS", "100", 1);
    setenv("HYDRA_BREAKER_MAX_OPEN_MS", "1000", 1);
    // The local breaker must remain usable when Redis coordination is unavailable.
    setenv("HYDRA_REDIS_HOST", "127.0.0.1", 1);
    setenv("HYDRA_REDIS_PORT", "1", 1);
    hydrastore::BlobStoreCircuitBreaker breaker;
    for (int i = 0; i < 8; ++i) breaker.Record(true, false, 10);
    for (int i = 0; i < 4; ++i) breaker.Record(false, false, 10);

    assert(!breaker.Allow().allowed);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    const auto first_probe = breaker.Allow();
    assert(first_probe.allowed);
    assert(!breaker.Allow().allowed);
    breaker.Record(true, false, 10, first_probe.probe_generation);
    const auto second_probe = breaker.Allow();
    assert(second_probe.allowed);
    breaker.Record(true, false, 10, second_probe.probe_generation);
    assert(breaker.Allow().allowed);

    // Three consecutive transient failures must not open the shared breaker
    // when the full sample window is otherwise healthy.
    setenv("HYDRA_BREAKER_NAMESPACE", (namespace_name + "-transient").c_str(), 1);
    setenv("HYDRA_BREAKER_MIN_SAMPLES", "12", 1);
    setenv("HYDRA_BREAKER_FAILURE_RATE", "0.25", 1);
    hydrastore::BlobStoreCircuitBreaker transient;
    for (int i = 0; i < 10; ++i) transient.Record(true, false, 10);
    for (int i = 0; i < 3; ++i) transient.Record(false, false, 10);
    assert(transient.Allow().allowed);
    return 0;
}
