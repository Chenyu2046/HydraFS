#ifndef HYDRASTORE_OBJECT_READER_H
#define HYDRASTORE_OBJECT_READER_H

#include "storage_types.h"

#include <mysql/mysql.h>

#include <string>

namespace hydrastore {

// Internal reader for manifest-backed objects. It deliberately bypasses the
// user-facing HTTP authorization layer because the caller is the trusted AI worker.
class ObjectReader {
public:
    ObjectReader(MYSQL *connection, BlobStore *blobs);

    void SetConnection(MYSQL *connection);
    bool DownloadToFile(const std::string &content_digest,
                        const std::string &user,
                        const std::string &output_path);

private:
    MYSQL *connection_;
    BlobStore *blobs_;
};

}  // namespace hydrastore

#endif
