#ifndef HYDRASTORE_DOCUMENT_EXTRACTOR_H
#define HYDRASTORE_DOCUMENT_EXTRACTOR_H

#include "knowledge_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace hydrastore {

constexpr std::int64_t AI_MAX_EXTRACTED_BYTES = 8 * 1024 * 1024;

struct FilterResult {
    bool ok = false;
    bool truncated = false;
    bool timed_out = false;
    int exit_code = -1;
};

struct ExtractorOptions {
    std::size_t max_output_bytes = AI_MAX_EXTRACTED_BYTES;
    int timeout_ms = 30000;
};

FilterResult RunFilter(const std::string &program,
                       const std::vector<std::string> &args,
                       std::size_t max_output_bytes, int timeout_ms,
                       std::string *output);

bool ExtractDocument(const SourceObject &source, ExtractedDocument *out,
                     std::string *error,
                     const ExtractorOptions &options = ExtractorOptions());

}  // namespace hydrastore

#endif
