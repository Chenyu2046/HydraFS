#ifndef HYDRASTORE_KNOWLEDGE_STORE_H
#define HYDRASTORE_KNOWLEDGE_STORE_H

#include "knowledge_types.h"

#include <mysql/mysql.h>

#include <cstdint>
#include <string>
#include <vector>

namespace hydrastore {

class KnowledgeStore {
public:
    KnowledgeStore(const std::string &host, unsigned int port,
                   const std::string &user, const std::string &password,
                   const std::string &database);
    ~KnowledgeStore();

    bool Connect();
    MYSQL *Connection() const { return connection_; }

    bool ClaimTask(const std::string &worker_id, KnowledgeTaskClaim *claim);
    bool RenewTask(const KnowledgeTaskClaim &claim);
    bool FinishTask(const KnowledgeTaskClaim &claim);
    bool ContinueTask(const KnowledgeTaskClaim &claim);
    bool SkipTask(const KnowledgeTaskClaim &claim, const std::string &reason);
    bool FailTask(const KnowledgeTaskClaim &claim, const std::string &error,
                  bool retryable);
    bool MarkWikiFailed(const KnowledgeTaskClaim &claim, const std::string &error);
    bool RecoverExpiredTasks();
    bool EnqueueTask(const std::string &user, const std::string &md5,
                     const std::string &task_type, const std::string &source,
                     bool force = false);

    bool LoadSourceObject(const std::string &user, const std::string &md5,
                          SourceObject *source);
    bool SourceRelationExists(const std::string &user, const std::string &md5,
                              bool *exists);
    bool LoadApiKey(const std::string &user, std::string *api_key);

    bool BeginEvidenceGeneration(const SourceObject &source,
                                 std::int64_t *generation,
                                 const KnowledgeTaskClaim *claim = nullptr);
    bool PutStagingChunk(const SourceObject &source, std::int64_t generation,
                         const EvidenceChunk &chunk, std::int64_t *chunk_id);
    bool PutVector(const std::string &user, const std::string &source_type,
                   std::int64_t source_id, const std::string &model,
                   int dimension, const std::vector<float> &embedding,
                   std::int64_t *vector_id);
    bool PublishEvidenceGeneration(const SourceObject &source,
                                   std::int64_t generation, int chunk_count,
                                   bool truncated, std::int64_t source_bytes,
                                   const KnowledgeTaskClaim *claim = nullptr);
    bool AbortEvidenceGeneration(const SourceObject &source,
                                 std::int64_t generation,
                                 const std::string &error,
                                 const KnowledgeTaskClaim *claim = nullptr);
    bool LoadPublishedEvidence(const SourceObject &source,
                               std::int64_t generation,
                               std::vector<EvidenceChunk> *chunks);
    bool UpdateLegacyAiRecord(const SourceObject &source,
                              const std::string &description,
                              const std::string &summary,
                              const std::string &model);

    bool MarkIndexDirty(const std::string &user);
    bool LoadIndexState(const std::string &user, std::int64_t *published,
                        std::int64_t *dirty);
    bool ClaimDirtyIndex(const std::string &worker_id, std::string *user,
                        std::int64_t *generation);
    bool PublishIndexGeneration(const std::string &user,
                                const std::string &worker_id,
                                std::int64_t generation);
    bool FailIndexGeneration(const std::string &user,
                             const std::string &worker_id,
                             std::int64_t generation,
                             const std::string &error);
    bool LoadActiveVectors(const std::string &user,
                           std::vector<KnowledgeVectorRecord> *vectors);
    bool LoadVectorsForSources(const std::string &user,
                               const std::string &source_type,
                               const std::vector<std::int64_t> &source_ids,
                               std::vector<KnowledgeVectorRecord> *vectors);

    bool LoadSearchHydration(const std::string &user,
                             const std::vector<std::int64_t> &vector_ids,
                             std::vector<SearchHydration> *rows);
    bool LoadWikiClaims(const std::string &user,
                        const std::vector<std::int64_t> &revision_ids,
                        std::vector<WikiClaimView> *claims);

    bool LoadFileCard(const std::string &user, const std::string &md5,
                      FileKnowledgeCard *card);
    bool LoadWikiForSource(const std::string &user, const std::string &md5,
                           std::vector<WikiPageView> *pages);
    bool LoadStaleWikiForSource(const std::string &user, const std::string &md5,
                                std::vector<WikiPageView> *pages, int limit = 3);
    bool HasStaleWikiForSource(const std::string &user, const std::string &md5,
                               bool *has_more);
    bool LoadBacklinks(const std::string &user, const std::string &md5,
                       std::vector<BacklinkView> *links);
    bool LoadRelated(const std::string &user, const std::string &md5,
                     std::vector<BacklinkView> *links);
    bool LoadWikiCandidates(const std::string &user, int limit,
                            std::vector<WikiCandidate> *candidates);
    bool LoadWikiCandidatesByRevisionIds(
        const std::string &user, const std::vector<std::int64_t> &revision_ids,
        std::vector<WikiCandidate> *candidates);

    bool PublishWikiPatch(const WikiPublishContext &context,
                          const WikiPatch &patch,
                          const std::vector<WikiPageEmbedding> &embeddings,
                          const std::string &model,
                          const std::string &compiler_version,
                          int embedding_dimension, std::string *error,
                          bool *continued = nullptr);
    bool DeleteSourceKnowledge(const std::string &user, const std::string &md5,
                               std::string *error);

private:
    bool Exec(const std::string &sql);
    bool Query(const std::string &sql,
               std::vector<std::vector<std::string>> *rows);
    std::string Escape(const std::string &value) const;
    std::string InList(const std::vector<std::int64_t> &ids) const;
    bool BeginTransaction();
    bool Commit();
    void Rollback();
    bool AffectedOne() const;
    bool ReadSingle(const std::string &sql, std::vector<std::string> *row);
    bool LoadWikiCandidateEvidence(const std::string &user,
                                   std::vector<WikiCandidate> *candidates,
                                   bool require_valid_revision = true);
    bool LoadWikiClaimsInternal(const std::string &user,
                                const std::vector<std::int64_t> &revision_ids,
                                std::vector<WikiClaimView> *claims,
                                bool require_valid_revision);
    std::string BuildValidWikiRevisionPredicate(const std::string &revision_alias,
                                                const std::string &page_alias) const;
    bool VerifyTaskLeaseInTransaction(const KnowledgeTaskClaim &claim);
    bool EnsureConnection();
    void Close();

    std::string host_;
    unsigned int port_;
    std::string user_;
    std::string password_;
    std::string database_;
    MYSQL *connection_ = nullptr;
    unsigned long long affected_rows_ = 0;
    long long last_insert_id_ = 0;
};

}  // namespace hydrastore

#endif
