#ifndef HYDRASTORE_WIKI_COMPILER_H
#define HYDRASTORE_WIKI_COMPILER_H

#include "knowledge_store.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hydrastore {

bool ParseAndValidateWikiPatch(const std::string &json,
                               const std::vector<std::int64_t> &allowed_chunks,
                               WikiPatch *patch, std::string *error);

std::string NormalizeWikiPageKey(const std::string &title);

class WikiCompiler {
public:
    WikiCompiler(const std::string &model, const std::string &compiler_version,
                 int embedding_dimension,
                 const std::string &embedding_model = "text-embedding-v3");

    bool Compile(KnowledgeStore *store, const KnowledgeTaskClaim &task,
                 const std::string &api_key, std::string *error);

private:
    std::string model_;
    std::string compiler_version_;
    std::string embedding_model_;
    int embedding_dimension_;
};

}  // namespace hydrastore

#endif
