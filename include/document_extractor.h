#ifndef HYDRASTORE_DOCUMENT_EXTRACTOR_H
#define HYDRASTORE_DOCUMENT_EXTRACTOR_H

#include "knowledge_types.h"

#include <string>

namespace hydrastore {

constexpr std::int64_t AI_MAX_EXTRACTED_BYTES = 8 * 1024 * 1024;

bool ExtractDocument(const SourceObject &source, ExtractedDocument *out,
                     std::string *error);

}  // namespace hydrastore

#endif
