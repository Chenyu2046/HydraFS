#ifndef HYDRASTORE_KNOWLEDGE_CHUNKER_H
#define HYDRASTORE_KNOWLEDGE_CHUNKER_H

#include "knowledge_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace hydrastore {

struct ChunkOptions {
    std::size_t target_codepoints = 1600;
    std::size_t hard_max_codepoints = 2200;
    std::size_t overlap_codepoints = 180;
    std::size_t max_chunks = 512;
};

bool ChunkDocument(const std::string &text, const ChunkOptions &options,
                   std::vector<EvidenceChunk> *chunks, bool *truncated,
                   std::string *error);

bool ChunkDocumentFile(const std::string &path, const ChunkOptions &options,
                       std::vector<EvidenceChunk> *chunks, bool *truncated,
                       std::string *error);

}  // namespace hydrastore

#endif
