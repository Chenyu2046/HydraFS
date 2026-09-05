#include "storage_resilience.h"

#include "storage_types.h"

#include <hiredis/hiredis.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

long EnvLong(const char *name, long fallback, long minimum, long maximum) {
    const char *value = std::getenv(name);
    if (!value || !*value) return fallback;
    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    return end != value ? std::max(minimum, std::min(maximum, parsed)) : fallback;
}

double EnvDouble(const char *name, double fallback, double minimum, double maximum) {
    const char *value = std::getenv(name);
    if (!value || !*value) return fallback;
    char *end = nullptr;
    const double parsed = std::strtod(value, &end);
    return end != value ? std::max(minimum, std::min(maximum, parsed)) : fallback;
}

std::string EnvString(const char *name, const char *fallback) {
    const char *value = std::getenv(name);
    return value && *value ? value : fallback;
}

}  // namespace

namespace hydrastore {

struct BlobStoreCircuitBreaker::Impl {
    struct Sample { bool success; bool timed_out; std::int64_t elapsed_ms; };
    std::mutex mutex;
    std::deque<Sample> samples;
    std::vector<std::int64_t> warmup;
    double srtt = 0;
    double baseline = 0;
    int consecutive_failures = 0;
    int half_open_successes = 0;
    bool half_open = false;
    bool probe_in_flight = false;
    std::uint64_t probe_generation = 0;
    std::uint64_t next_generation = 0;
    Clock::time_point local_open_until{};
    std::int64_t next_open_ms = 3000;
    const int min_samples = static_cast<int>(EnvLong("HYDRA_BREAKER_MIN_SAMPLES", 12, 1, 16));
    const double failure_rate_limit = EnvDouble("HYDRA_BREAKER_FAILURE_RATE", 0.25, 0.0, 1.0);
    const double timeout_rate_limit = EnvDouble("HYDRA_BREAKER_TIMEOUT_RATE", 0.125, 0.0, 1.0);
    const std::int64_t initial_open_ms = EnvLong("HYDRA_BREAKER_OPEN_MS", 3000, 100, 30000);
    const std::int64_t max_open_ms = EnvLong("HYDRA_BREAKER_MAX_OPEN_MS", 30000, 100, 300000);
    const std::int64_t probe_ms = EnvLong("HYDRA_BREAKER_PROBE_MS", 3000, 1000, 300000);
    const std::int64_t redis_command_timeout_ms = EnvLong("HYDRA_BREAKER_REDIS_TIMEOUT_MS", 50, 1, 5000);
    redisContext *redis = nullptr;
    bool redis_error_logged = false;
    const std::string key_prefix = EnvString("HYDRA_BREAKER_NAMESPACE", "hydrastore");
    const std::string open_key = key_prefix + ":breaker:blobstore:open";
    const std::string probe_key = key_prefix + ":breaker:blobstore:probe";
    const std::string marker_key = key_prefix + ":breaker:blobstore:degraded";
    const std::string redis_password = EnvString("HYDRA_REDIS_PASSWORD", "");
    std::string probe_token;

    ~Impl() {
        if (redis) redisFree(redis);
    }

    void LogRedisFailure() {
        if (!redis_error_logged) {
            std::fprintf(stderr, "HydraStore breaker: Redis state unavailable; fail-open metadata path\n");
            redis_error_logged = true;
        }
    }

    bool ConnectRedis() {
        if (redis && redis->err == 0) return true;
        if (redis) { redisFree(redis); redis = nullptr; }
        const char *host = std::getenv("HYDRA_REDIS_HOST");
        const int port = static_cast<int>(EnvLong("HYDRA_REDIS_PORT", 6379, 1, 65535));
        timeval timeout{0, 50000};
        redis = redisConnectWithTimeout(host && *host ? host : "redis", port, timeout);
        if (!redis || redis->err != 0) { LogRedisFailure(); return false; }
        timeval command_timeout{redis_command_timeout_ms / 1000,
                                (redis_command_timeout_ms % 1000) * 1000};
        if (redisSetTimeout(redis, command_timeout) != REDIS_OK) {
            LogRedisFailure();
            redisFree(redis);
            redis = nullptr;
            return false;
        }
        if (!redis_password.empty()) {
            redisReply *reply = static_cast<redisReply *>(redisCommand(redis, "AUTH %s", redis_password.c_str()));
            const bool authenticated = reply && reply->type == REDIS_REPLY_STATUS && reply->str &&
                                        std::string(reply->str) == "OK";
            if (reply) freeReplyObject(reply);
            if (!authenticated) {
                LogRedisFailure();
                redisFree(redis);
                redis = nullptr;
                return false;
            }
        }
        redis_error_logged = false;
        return true;
    }

    bool SharedOpen() {
        if (!ConnectRedis()) return false;
        redisReply *reply = static_cast<redisReply *>(redisCommand(redis, "GET %s", open_key.c_str()));
        if (!reply) { LogRedisFailure(); return false; }
        if (reply->type != REDIS_REPLY_STRING && reply->type != REDIS_REPLY_NIL) LogRedisFailure();
        const bool open = reply->type == REDIS_REPLY_STRING && reply->len > 0;
        freeReplyObject(reply);
        return open;
    }

    bool SharedMarker() {
        if (!ConnectRedis()) return false;
        redisReply *reply = static_cast<redisReply *>(redisCommand(redis, "EXISTS %s", marker_key.c_str()));
        if (!reply || reply->type != REDIS_REPLY_INTEGER) {
            if (reply) freeReplyObject(reply);
            LogRedisFailure();
            return false;
        }
        const bool marked = reply->integer != 0;
        freeReplyObject(reply);
        return marked;
    }

    bool TryProbe() {
        if (!ConnectRedis()) return true;
        probe_token = std::to_string(static_cast<long long>(::getpid())) + "-" +
                      std::to_string(static_cast<long long>(Clock::now().time_since_epoch().count()));
        const char script[] =
            "if redis.call('exists',KEYS[1])~=0 then return 0 end "
            "return redis.call('set',KEYS[2],ARGV[1],'NX','PX',ARGV[2])";
        redisReply *reply = static_cast<redisReply *>(redisCommand(
            redis, "EVAL %b 2 %s %s %b %lld", script, sizeof(script) - 1,
            open_key.c_str(), probe_key.c_str(), probe_token.data(), probe_token.size(),
            static_cast<long long>(probe_ms)));
        if (!reply) { LogRedisFailure(); return true; }
        if (reply->type == REDIS_REPLY_ERROR) { LogRedisFailure(); freeReplyObject(reply); return true; }
        const bool acquired = reply->type == REDIS_REPLY_STATUS && reply->str &&
                              std::string(reply->str) == "OK";
        freeReplyObject(reply);
        return acquired;
    }

    void CompleteProbeRecovery() {
        if (!redis || redis->err != 0) return;
        static const char script[] =
            "if redis.call('get',KEYS[1])==ARGV[1] then "
            "redis.call('del',KEYS[1]); redis.call('del',KEYS[2]); "
            "return redis.call('exists',KEYS[3])==0 and 1 or 0 end return 0";
        redisReply *reply = static_cast<redisReply *>(redisCommand(
            redis, "EVAL %b 3 %s %s %s %b", script, sizeof(script) - 1,
            probe_key.c_str(), marker_key.c_str(), open_key.c_str(),
            probe_token.data(), probe_token.size()));
        if (reply) freeReplyObject(reply);
        probe_token.clear();
    }

    void PublishOpen(std::int64_t duration_ms) {
        if (!ConnectRedis()) return;
        const std::string marker_token =
            std::to_string(static_cast<long long>(::getpid())) + "-" +
            std::to_string(static_cast<long long>(Clock::now().time_since_epoch().count()));
        const std::int64_t marker_ms = std::min<std::int64_t>(max_open_ms + probe_ms, 600000);
        static const char script[] =
            "local ttl=redis.call('pttl',KEYS[1]); "
            "if ttl<ARGV[1] then redis.call('psetex',KEYS[1],ARGV[1],ARGV[2]) end; "
            "if redis.call('exists',KEYS[2])==0 then redis.call('psetex',KEYS[2],ARGV[3],ARGV[4]) end; "
            "return 1";
        redisReply *reply = static_cast<redisReply *>(redisCommand(
            redis, "EVAL %b 2 %s %s %lld %b %lld %b", script, sizeof(script) - 1,
            open_key.c_str(), marker_key.c_str(), static_cast<long long>(duration_ms),
            marker_token.data(), marker_token.size(), static_cast<long long>(marker_ms),
            marker_token.data(), marker_token.size()));
        if (!reply) LogRedisFailure(); else freeReplyObject(reply);
    }

    void OpenLocked() {
        const auto now = Clock::now();
        if (local_open_until > now) return;
        local_open_until = now + std::chrono::milliseconds(next_open_ms);
        PublishOpen(next_open_ms);
        next_open_ms = std::min<std::int64_t>(max_open_ms, next_open_ms * 2);
        half_open = false;
        probe_in_flight = false;
        half_open_successes = 0;
    }
};

BlobStoreCircuitBreaker::BlobStoreCircuitBreaker() : impl_(new Impl) {
    impl_->next_open_ms = impl_->initial_open_ms;
}
BlobStoreCircuitBreaker::~BlobStoreCircuitBreaker() = default;

BreakerDecision BlobStoreCircuitBreaker::Allow() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto now = Clock::now();
    if (impl_->local_open_until > now) return {false, "blobstore-circuit-open"};
    if (impl_->half_open) {
        if (impl_->probe_in_flight) return {false, "blobstore-circuit-open"};
        impl_->probe_in_flight = true;
        return {true, nullptr, impl_->probe_generation};
    }
    const bool local_was_open = impl_->local_open_until != Clock::time_point{};
    if (impl_->SharedOpen()) return {false, "blobstore-circuit-open"};
    if (local_was_open || impl_->SharedMarker()) {
        if (!impl_->TryProbe()) return {false, "blobstore-circuit-open"};
        impl_->half_open = true;
        impl_->probe_in_flight = true;
        impl_->probe_generation = ++impl_->next_generation;
        return {true, nullptr, impl_->probe_generation};
    }
    return {true, nullptr};
}

void BlobStoreCircuitBreaker::Record(bool success, bool timed_out, std::int64_t elapsed_ms,
                                     std::uint64_t probe_generation) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto sample = Impl::Sample{success, timed_out, std::max<std::int64_t>(0, elapsed_ms)};
    if (success) {
        impl_->srtt = impl_->srtt == 0 ? sample.elapsed_ms :
            0.125 * sample.elapsed_ms + 0.875 * impl_->srtt;
        if (impl_->warmup.size() < 8) {
            impl_->warmup.push_back(sample.elapsed_ms);
            if (impl_->warmup.size() == 8) {
                std::sort(impl_->warmup.begin(), impl_->warmup.end());
                impl_->baseline = (impl_->warmup[3] + impl_->warmup[4]) / 2.0;
            }
        }
    }
    impl_->samples.push_back(sample);
    if (impl_->samples.size() > 16) impl_->samples.pop_front();

    if (impl_->half_open) {
        if (probe_generation != impl_->probe_generation) return;
        impl_->probe_in_flight = false;
        if (!success) {
            impl_->OpenLocked();
        } else if (++impl_->half_open_successes >= 2) {
            impl_->local_open_until = Clock::time_point{};
            impl_->half_open = false;
            impl_->half_open_successes = 0;
            impl_->consecutive_failures = 0;
            impl_->next_open_ms = impl_->initial_open_ms;
            impl_->CompleteProbeRecovery();
        }
        return;
    }

    impl_->consecutive_failures = success ? 0 : impl_->consecutive_failures + 1;
    if (impl_->samples.size() < static_cast<std::size_t>(impl_->min_samples)) return;
    int failures = 0;
    int timeouts = 0;
    for (const auto &item : impl_->samples) {
        failures += !item.success;
        timeouts += item.timed_out;
    }
    const bool rtt_bad = impl_->baseline > 0 &&
        impl_->srtt > std::max(4.0 * impl_->baseline, 2000.0);
    // A few adjacent failures can be a transient FastDFS/tracker blip.  The
    // shared breaker must use the configured window rate before rejecting all
    // workers and amplifying a retry storm.
    if (static_cast<double>(failures) / impl_->samples.size() >= impl_->failure_rate_limit ||
        static_cast<double>(timeouts) / impl_->samples.size() >= impl_->timeout_rate_limit || rtt_bad) {
        impl_->OpenLocked();
    }
}

class FaultInjectingBlobStore final : public BlobStore {
public:
    explicit FaultInjectingBlobStore(std::unique_ptr<BlobStore> inner)
        : inner_(std::move(inner)), delay_ms_(EnvLong("HYDRA_TEST_BLOB_DELAY_MS", 0, 0, 600000)),
          fail_every_(EnvLong("HYDRA_TEST_BLOB_FAIL_EVERY_N", 0, 0, 1000000)),
          timeout_every_(EnvLong("HYDRA_TEST_BLOB_TIMEOUT_EVERY_N", 0, 0, 1000000)) {}

    bool Put(const BlobSource &source, std::int64_t size, const std::string &extension,
             std::string *backend_file_id) override {
        const long call = ++calls_;
        const bool timeout = timeout_every_ > 0 && call % timeout_every_ == 0;
        const bool failure = fail_every_ > 0 && call % fail_every_ == 0;
        if (delay_ms_ > 0 || timeout) {
            const long wait_ms = timeout ? std::max(delay_ms_, 2500L) : delay_ms_;
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
        }
        if (failure || timeout) return false;
        return inner_->Put(source, size, extension, backend_file_id);
    }
    bool Get(const std::string &id, const BlobSink &sink, std::int64_t offset, std::int64_t bytes, std::int64_t *total) override {
        return inner_->Get(id, sink, offset, bytes, total);
    }
    bool Delete(const std::string &id) override { return inner_->Delete(id); }

private:
    std::unique_ptr<BlobStore> inner_;
    const long delay_ms_;
    const long fail_every_;
    const long timeout_every_;
    std::atomic<long> calls_{0};
};

std::unique_ptr<BlobStore> MakeFaultInjectingBlobStore(std::unique_ptr<BlobStore> inner) {
    const char *enabled = std::getenv("HYDRA_ENABLE_TEST_FAULTS");
    if (!enabled || std::string(enabled) != "1") return inner;
    const char *delay = std::getenv("HYDRA_TEST_BLOB_DELAY_MS");
    const char *failure = std::getenv("HYDRA_TEST_BLOB_FAIL_EVERY_N");
    const char *timeout = std::getenv("HYDRA_TEST_BLOB_TIMEOUT_EVERY_N");
    if ((!delay || !*delay) && (!failure || !*failure) && (!timeout || !*timeout)) return inner;
    return std::unique_ptr<BlobStore>(new FaultInjectingBlobStore(std::move(inner)));
}

}  // namespace hydrastore
