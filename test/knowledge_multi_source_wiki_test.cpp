#include "wiki_compiler.h"

#include <cassert>
#include <string>

int main() {
    hydrastore::WikiPatch patch;
    std::string error;
    const std::string json =
        R"({"operations":[{"op":"upsert_page","title":"Shared concept","summary":"summary","body_markdown":"body","claims":[{"text":"fact supported by two files","confidence":0.8,"citations":[101,202]}]}]})";
    assert(hydrastore::ParseAndValidateWikiPatch(json, {101, 202}, &patch, &error));
    assert(patch.pages.size() == 1);
    assert(patch.pages[0].claims.size() == 1);
    assert(patch.pages[0].claims[0].citations.size() == 2);

    const std::string arbitrary_key =
        R"({"operations":[{"op":"upsert_page","page_key":"forged-key","title":"Shared concept","summary":"summary","body_markdown":"body","claims":[{"text":"fact","citations":[101]}]}]})";
    assert(!hydrastore::ParseAndValidateWikiPatch(arbitrary_key, {101}, &patch, &error,
                                                  {"known-candidate"}));
    return 0;
}
