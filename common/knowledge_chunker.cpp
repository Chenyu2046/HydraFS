#include "knowledge_chunker.h"

#include "hash_util.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <vector>

namespace hydrastore {
namespace {

struct Codepoint {
    std::size_t byte = 0;
    std::size_t next = 0;
};

std::vector<Codepoint> Decode(const std::string &text) {
    std::vector<Codepoint> result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t begin = i;
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t width = 1;
        if (c >= 0xF0 && c <= 0xF4 && i + 3 < text.size() &&
            (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80 &&
            (static_cast<unsigned char>(text[i + 2]) & 0xC0) == 0x80 &&
            (static_cast<unsigned char>(text[i + 3]) & 0xC0) == 0x80) {
            width = 4;
        } else if (c >= 0xE0 && c <= 0xEF && i + 2 < text.size() &&
                   (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(text[i + 2]) & 0xC0) == 0x80) {
            width = 3;
        } else if (c >= 0xC2 && c <= 0xDF && i + 1 < text.size() &&
                   (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80) {
            width = 2;
        }
        i = std::min(text.size(), i + width);
        result.push_back({begin, i});
    }
    return result;
}

bool IsPreferredBoundary(const std::string &text, const std::vector<Codepoint> &cp,
                         std::size_t cut) {
    if (cut == 0 || cut >= cp.size()) return true;
    const std::size_t byte = cp[cut - 1].next;
    const char previous = text[byte - 1];
    if (previous == '\n' || previous == '\r') return true;
    if (previous == '.' || previous == '!' || previous == '?') return true;
    return false;
}

std::size_t ChooseCut(const std::string &text, const std::vector<Codepoint> &cp,
                      std::size_t start, std::size_t target, std::size_t hard) {
    const std::size_t max_cut = std::min(cp.size(), start + hard);
    const std::size_t preferred_limit = std::min(cp.size(), start + target);
    std::size_t preferred = 0;
    for (std::size_t cut = start + 1; cut <= preferred_limit; ++cut) {
        if (IsPreferredBoundary(text, cp, cut)) preferred = cut;
    }
    if (preferred > start) return preferred;
    return std::max(start + 1, max_cut);
}

std::string HeadingBefore(const std::string &text, const std::vector<Codepoint> &cp,
                          std::size_t begin) {
    if (begin == 0) return {};
    std::size_t line_start = cp[begin - 1].byte;
    while (line_start > 0 && text[line_start - 1] != '\n') --line_start;
    std::string line = text.substr(line_start, cp[begin - 1].next - line_start);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
    if (!line.empty() && line[0] == '#') return line;
    return {};
}

}  // namespace

bool ChunkDocument(const std::string &text, const ChunkOptions &options,
                   std::vector<EvidenceChunk> *chunks, bool *truncated,
                   std::string *error) {
    if (error) error->clear();
    if (!chunks || !truncated || options.target_codepoints == 0 ||
        options.hard_max_codepoints < options.target_codepoints ||
        options.overlap_codepoints >= options.hard_max_codepoints ||
        options.max_chunks == 0) {
        if (error) *error = "invalid chunk options";
        return false;
    }
    chunks->clear();
    *truncated = false;
    const std::vector<Codepoint> cp = Decode(text);
    std::size_t start = 0;
    while (start < cp.size()) {
        if (chunks->size() >= options.max_chunks) {
            *truncated = true;
            break;
        }
        const std::size_t cut = ChooseCut(text, cp, start,
                                          options.target_codepoints,
                                          options.hard_max_codepoints);
        EvidenceChunk chunk;
        chunk.chunk_no = static_cast<int>(chunks->size());
        chunk.start_offset = static_cast<std::int64_t>(cp[start].byte);
        chunk.end_offset = static_cast<std::int64_t>(cp[cut - 1].next);
        chunk.content = text.substr(static_cast<std::size_t>(chunk.start_offset),
                                    static_cast<std::size_t>(chunk.end_offset - chunk.start_offset));
        chunk.heading = HeadingBefore(text, cp, start);
        Sha256 sha;
        sha.Update(chunk.content.data(), chunk.content.size());
        chunk.content_sha256 = sha.FinalHex();
        chunks->push_back(std::move(chunk));
        if (cut == cp.size()) break;
        start = cut > options.overlap_codepoints ? cut - options.overlap_codepoints : cut;
    }
    return true;
}

bool ChunkDocumentFile(const std::string &path, const ChunkOptions &options,
                       std::vector<EvidenceChunk> *chunks, bool *truncated,
                       std::string *error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { if (error) *error = "cannot open extracted document"; return false; }
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return ChunkDocument(text, options, chunks, truncated, error);
}

}  // namespace hydrastore
