#ifndef HYDRASTORE_STORAGE_TYPES_H
#define HYDRASTORE_STORAGE_TYPES_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hydrastore {

enum class ChunkState {
    kUploading,
    kReady,
    kGcPending,
    kDeleting,
    kFailed
};

enum class PartAvailability {
    kReady,
    kUploadable,
    kWaiting,
    kMissing
};

struct PartSpec {
    int index = 0;
    std::int64_t size = 0;
    std::string sha256;
};

struct PartStatus {
    PartSpec spec;
    std::string state;
    std::string backend_file_id;
    std::int64_t chunk_id = 0;
    std::string owner_upload_id;
    std::int64_t lease_until = 0;
    std::int64_t lease_epoch = 0;
    PartAvailability availability = PartAvailability::kMissing;
};

struct PartClaim {
    bool granted = false;
    std::int64_t chunk_id = 0;
    std::int64_t lease_epoch = 0;
    std::string state;
    std::string backend_file_id;
};

struct UploadSession {
    std::string id;
    std::string user;
    std::string filename;
    std::string content_digest;
    std::int64_t size = 0;
    int chunk_count = 0;
    std::string state;
    std::string object_id;
    std::string storage_mode;
    std::string legacy_url;
    std::int64_t manifest_id = 0;
};

struct BlobSource {
    using Read = std::function<std::size_t(void *context, void *buffer, std::size_t capacity)>;
    void *context = nullptr;
    Read read;
};

struct BlobSink {
    using Write = std::function<bool(void *context, const void *data, std::size_t size)>;
    void *context = nullptr;
    Write write;
};

class BlobStore {
public:
    virtual ~BlobStore() = default;
    virtual bool Put(const BlobSource &source, std::int64_t size,
                     const std::string &extension, std::string *backend_file_id) = 0;
    virtual bool Get(const std::string &backend_file_id, const BlobSink &sink,
                     std::int64_t offset = 0, std::int64_t bytes = 0,
                     std::int64_t *total_size = nullptr) = 0;
    virtual bool Delete(const std::string &backend_file_id) = 0;
};

std::unique_ptr<BlobStore> MakeFastDFSBlobStore(const char *client_config);

}  // namespace hydrastore

#endif
