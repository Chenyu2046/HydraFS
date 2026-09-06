#ifndef HYDRASTORE_KNOWLEDGE_TYPES_H
#define HYDRASTORE_KNOWLEDGE_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace hydrastore {

struct KnowledgeTaskClaim {
    std::int64_t id = 0;
    std::string user;
    std::string md5;
    std::string task_type;
    int retry_count = 0;
    std::string worker_id;
    std::int64_t lease_epoch = 0;
};

struct SourceObject {
    std::string user;
    std::string md5;
    std::string filename;
    std::string type;
    std::string storage_mode;
    std::string object_id;
    std::int64_t manifest_id = 0;
    std::int64_t size = 0;
    std::string legacy_url;
    std::string local_path;
    bool truncated = false;
};

struct ExtractedDocument {
    std::string text_path;
    std::string type;
    std::int64_t extracted_bytes = 0;
    bool truncated = false;
};

struct EvidenceChunk {
    std::int64_t id = 0;
    int chunk_no = 0;
    std::string heading;
    std::string content;
    std::string content_sha256;
    std::int64_t start_offset = 0;
    std::int64_t end_offset = 0;
};

struct KnowledgeVectorRecord {
    std::int64_t id = 0;
    std::string source_type;
    std::int64_t source_id = 0;
    std::string model;
    int dimension = 0;
    std::vector<float> embedding;
};

struct SearchHydration {
    std::int64_t vector_id = 0;
    std::string source_type;
    std::int64_t source_id = 0;
    std::string md5;
    std::string filename;
    std::string type;
    std::int64_t size = 0;
    std::string url;
    int chunk_no = 0;
    std::string snippet;
    std::string page_key;
    std::string title;
    std::int64_t revision_id = 0;
    std::string summary;
    std::string body_markdown;
};

struct WikiClaimPatch {
    std::string text;
    float confidence = 0.0f;
    std::vector<std::int64_t> citations;
};

struct WikiPagePatch {
    std::string page_key;
    std::string title;
    std::int64_t base_revision_id = 0;
    bool has_base_revision = false;
    std::string summary;
    std::string body_markdown;
    std::vector<WikiClaimPatch> claims;
};

struct WikiLinkPatch {
    std::string src_page_key;
    std::string dst_page_key;
    std::string relation;
};

struct WikiPatch {
    std::vector<WikiPagePatch> pages;
    std::vector<WikiLinkPatch> links;
};

struct WikiCandidate {
    std::int64_t page_id = 0;
    std::string page_key;
    std::string title;
    std::int64_t revision_id = 0;
    std::string summary;
    std::string body_markdown;
    std::vector<std::string> active_claims;
};

struct WikiClaimView {
    std::int64_t id = 0;
    std::int64_t revision_id = 0;
    std::string text;
    float confidence = 0.0f;
    std::vector<std::pair<std::string, std::int64_t>> citations;
};

struct WikiPageView {
    std::int64_t page_id = 0;
    std::string page_key;
    std::string title;
    std::int64_t revision_id = 0;
    std::string summary;
    std::string body_markdown;
    std::vector<WikiClaimView> claims;
    std::vector<std::string> source_md5s;
};

struct BacklinkView {
    std::string md5;
    std::string concept;
    std::string page_key;
    std::string title;
    std::int64_t page_id = 0;
};

struct FileKnowledgeCard {
    std::string md5;
    std::string filename;
    std::string type;
    std::int64_t size = 0;
    std::string url;
    std::string parse_status;
    std::string task_status;
    std::int64_t task_id = 0;
    std::string summary;
    std::string description;
    std::string error;
    bool wiki_ready = false;
    bool evidence_ready = false;
    bool partial_source = false;
};

}  // namespace hydrastore

#endif
