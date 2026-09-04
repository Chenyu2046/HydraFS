#include "storage_types.h"

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

#include "fdfs_client.h"
#include "sockopt.h"

namespace hydrastore {
namespace {

struct UploadContext {
    const BlobSource *source;
};

int UploadCallback(void *arg, const int64_t file_size, int sock) {
    auto *context = static_cast<UploadContext *>(arg);
    std::int64_t remaining = file_size;
    char buffer[64 * 1024];
    while (remaining > 0) {
        const std::size_t want = static_cast<std::size_t>(
            std::min<std::int64_t>(remaining, sizeof(buffer)));
        const std::size_t got = context->source->read(
            context->source->context, buffer, want);
        if (got == 0 || got > want) {
            return EIO;
        }
        const int result = tcpsenddata(sock, buffer, static_cast<int>(got),
                                       g_fdfs_network_timeout);
        if (result != 0) {
            return result;
        }
        remaining -= static_cast<std::int64_t>(got);
    }
    return 0;
}

struct DownloadContext {
    const BlobSink *sink;
};

int DownloadCallback(void *arg, const int64_t, const char *data, const int current_size) {
    auto *context = static_cast<DownloadContext *>(arg);
    return context->sink->write(context->sink->context, data,
                                static_cast<std::size_t>(current_size)) ? 0 : EIO;
}

}  // namespace

class FastDFSBlobStore final : public BlobStore {
public:
    explicit FastDFSBlobStore(const char *client_config) : initialized_(false) {
        initialized_ = fdfs_client_init(client_config) == 0;
    }

    ~FastDFSBlobStore() override {
        if (initialized_) {
            fdfs_client_destroy();
        }
    }

    bool Put(const BlobSource &source, std::int64_t size,
             const std::string &extension, std::string *backend_file_id) override {
        if (!initialized_ || !source.read || !backend_file_id || size < 0) {
            return false;
        }
        ConnectionInfo *tracker = tracker_get_connection();
        if (!tracker) {
            return false;
        }
        ConnectionInfo storage;
        char group[FDFS_GROUP_NAME_MAX_LEN + 1] = {0};
        int store_path = 0;
        int result = tracker_query_storage_store(tracker, &storage, group, &store_path);
        if (result == 0) {
            char file_id[128] = {0};
            UploadContext context{&source};
            result = storage_upload_by_callback1(
                tracker, &storage, store_path, UploadCallback, &context, size,
                extension.empty() ? nullptr : extension.c_str(), nullptr, 0,
                group, file_id);
            if (result == 0) {
                *backend_file_id = file_id;
            }
        }
        tracker_close_connection_ex(tracker, result != 0);
        return result == 0;
    }

    bool Get(const std::string &backend_file_id, const BlobSink &sink,
             std::int64_t offset, std::int64_t bytes,
             std::int64_t *total_size) override {
        if (!initialized_ || backend_file_id.empty() || !sink.write || offset < 0 || bytes < 0) {
            return false;
        }
        ConnectionInfo *tracker = tracker_get_connection();
        if (!tracker) {
            return false;
        }
        ConnectionInfo storage;
        int result = tracker_query_storage_fetch1(tracker, &storage, backend_file_id.c_str());
        if (result == 0) {
            DownloadContext context{&sink};
            result = storage_download_file_ex1(
                tracker, &storage, backend_file_id.c_str(), offset, bytes,
                DownloadCallback, &context, total_size);
        }
        tracker_close_connection_ex(tracker, result != 0);
        return result == 0;
    }

    bool Delete(const std::string &backend_file_id) override {
        if (!initialized_ || backend_file_id.empty()) {
            return false;
        }
        ConnectionInfo *tracker = tracker_get_connection();
        if (!tracker) {
            return false;
        }
        ConnectionInfo storage;
        int result = tracker_query_storage_fetch1(tracker, &storage, backend_file_id.c_str());
        if (result == 0) {
            result = storage_delete_file1(tracker, &storage, backend_file_id.c_str());
        }
        tracker_close_connection_ex(tracker, result != 0 && result != ENOENT);
        return result == 0 || result == ENOENT;
    }

private:
    bool initialized_;
};

}  // namespace hydrastore

std::unique_ptr<hydrastore::BlobStore> hydrastore::MakeFastDFSBlobStore(
    const char *client_config) {
    return std::unique_ptr<hydrastore::BlobStore>(
        new hydrastore::FastDFSBlobStore(client_config));
}
