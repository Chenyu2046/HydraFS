#include "knowledge_chunker.h"

#include <cassert>
#include <string>
#include <vector>

int main() {
    std::string cjk;
    for (int i = 0; i < 250; ++i) cjk += "中文";
    const std::string input = "标题\n" + std::string(500, 'a') + "。\n" + cjk;
    hydrastore::ChunkOptions options;
    options.target_codepoints = 80;
    options.hard_max_codepoints = 100;
    options.overlap_codepoints = 10;
    options.max_chunks = 20;
    std::vector<hydrastore::EvidenceChunk> first, second;
    bool first_truncated = false, second_truncated = false;
    std::string error;
    assert(hydrastore::ChunkDocument(input, options, &first, &first_truncated, &error));
    assert(hydrastore::ChunkDocument(input, options, &second, &second_truncated, &error));
    assert(!first.empty() && first.size() == second.size());
    assert(first_truncated == second_truncated);
    for (std::size_t i = 0; i < first.size(); ++i) {
        assert(first[i].content == second[i].content);
        assert(first[i].content_sha256 == second[i].content_sha256);
        assert(first[i].end_offset > first[i].start_offset);
        assert(first[i].end_offset - first[i].start_offset <= 400);
    }
    return 0;
}
