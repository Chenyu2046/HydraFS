#ifndef HYDRASTORE_METADATA_STORE_H
#define HYDRASTORE_METADATA_STORE_H

#include "storage_types.h"

#include <mysql/mysql.h>

#include <cstdint>
#include <string>
#include <vector>

namespace hydrastore {

struct GcCandidate {
    std::int64_t id = 0;
    std::string backend_file_id;
};

class MetadataStore {
public:
    MetadataStore(const std::string &host, unsigned int port,
                  const std::string &user, const std::string &password,
                  const std::string &database);
    ~MetadataStore();

    bool InitOrResume(const std::string &upload_id, const std::string &user,
                      const std::string &filename, std::int64_t object_size,
                      const std::string &content_digest,
                      const std::vector<PartSpec> &parts,
                      UploadSession *session, std::vector<PartStatus> *statuses);
    bool GetSession(const std::string &upload_id, const std::string &user,
                    UploadSession *session);
    bool MarkPartReady(const std::string &upload_id, int part_index,
                       const std::string &owner_upload_id,
                       std::int64_t lease_epoch, const std::string &backend_file_id);
    bool ClaimPartUpload(const std::string &upload_id, int part_index,
                         PartClaim *claim);
    bool MarkPartFailed(const std::string &upload_id, int part_index,
                        const std::string &owner_upload_id, std::int64_t lease_epoch);
    bool Commit(const std::string &upload_id, const std::string &user,
                UploadSession *session);
    bool Abort(const std::string &upload_id, const std::string &user);
    bool DeleteObjectForUser(const std::string &object_id, const std::string &user);
    bool GetManifest(const std::string &object_id, const std::string &user,
                     UploadSession *session,
                     std::vector<PartStatus> *parts);
    bool GetManifestByDigest(const std::string &content_digest, const std::string &user,
                             UploadSession *session, std::vector<PartStatus> *parts);
    bool GetSharedManifest(const std::string &share_token, UploadSession *session,
                           std::vector<PartStatus> *parts);
    bool EnqueueParseTask(const std::string &user, const std::string &content_digest,
                          const std::string &type, const std::string &source);
    bool ReconcileExpiredUploads();
    bool ClaimGc(std::vector<GcCandidate> *candidates, int limit);
    bool FinishGc(const GcCandidate &candidate, bool deleted, const std::string &error);

private:
    bool Exec(const std::string &sql, const std::vector<std::string> &params = {});
    bool Query(const std::string &sql, const std::vector<std::string> &params,
               std::vector<std::vector<std::string>> *rows);
    bool Txn(const char *sql);
    bool ReadSession(const std::vector<std::string> &row, UploadSession *session) const;
    bool ReadStatuses(const std::string &upload_id, const std::string &current_upload_id,
                      std::vector<PartStatus> *statuses);
    bool ReadObjectInfo(UploadSession *session);
    bool ManifestMatchesUpload(std::int64_t manifest_id, const std::string &upload_id);
    long long LastInsertId() const;
    unsigned long long AffectedRows() const;
    void Close();

    std::string host_;
    unsigned int port_;
    std::string user_;
    std::string password_;
    std::string database_;
    MYSQL *connection_;
    unsigned long long last_affected_rows_ = 0;
    long long last_insert_id_ = 0;
};

}  // namespace hydrastore

#endif
