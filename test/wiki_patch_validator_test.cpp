#include "wiki_compiler.h"

#include <cassert>
#include <string>
#include <vector>

int main() {
    const std::vector<std::int64_t> allowed = {11, 12};
    hydrastore::WikiPatch patch;
    std::string error;
    const std::string valid =
        R"({"operations":[{"op":"upsert_page","title":"Hydra Store","base_revision_id":0,"summary":"summary","body_markdown":"# body","claims":[{"text":"fact","confidence":0.9,"citations":[11]}]}]})";
    assert(hydrastore::ParseAndValidateWikiPatch(valid, allowed, &patch, &error));
    assert(patch.pages.size() == 1 && patch.pages[0].page_key == hydrastore::NormalizeWikiPageKey("Hydra Store"));
    const std::string bad_citation =
        R"({"operations":[{"op":"upsert_page","title":"Hydra Store","summary":"summary","body_markdown":"body","claims":[{"text":"fact","citations":[99]}]}]})";
    assert(!hydrastore::ParseAndValidateWikiPatch(bad_citation, allowed, &patch, &error));
    const std::string unsafe =
        R"({"operations":[{"op":"upsert_page","title":"Hydra Store","summary":"summary","body_markdown":"<script>alert(1)</script>"}]})";
    assert(!hydrastore::ParseAndValidateWikiPatch(unsafe, allowed, &patch, &error));
    const std::string keyed =
        R"({"operations":[{"op":"upsert_page","page_key":"alpha-page","title":"Alpha","summary":"summary","body_markdown":"body"}],"links":[{"src_page_key":"alpha-page","dst_page_key":"beta-page","relation":"related"}]})";
    assert(hydrastore::ParseAndValidateWikiPatch(keyed, allowed, &patch, &error,
                                                {"alpha-page", "beta-page"}));
    assert(patch.pages.size() == 1 && patch.pages[0].page_key == "alpha-page" && patch.links.size() == 1);
    const std::string multi_source =
        R"({"operations":[{"op":"upsert_page","title":"Cross source","summary":"summary","body_markdown":"body","claims":[{"text":"fact","confidence":1,"citations":[11,12]}]}]})";
    assert(hydrastore::ParseAndValidateWikiPatch(multi_source, allowed, &patch, &error));
    assert(patch.pages.size() == 1 && patch.pages[0].claims[0].citations.size() == 2);
    const std::string unknown_existing_key =
        R"({"operations":[{"op":"upsert_page","page_key":"not-a-candidate","title":"New","summary":"summary","body_markdown":"body"}]})";
    assert(!hydrastore::ParseAndValidateWikiPatch(unknown_existing_key, allowed, &patch, &error,
                                                  {"candidate-page"}));
    const std::string repair_key =
        R"({"operations":[{"op":"upsert_page","page_key":"candidate-page","title":"Candidate","base_revision_id":9,"summary":"summary","body_markdown":"body","claims":[{"text":"fact","citations":[12]}]}]})";
    assert(hydrastore::ParseAndValidateWikiPatch(repair_key, allowed, &patch, &error,
                                                {"candidate-page"}, true));
    const std::string invalid_key =
        R"({"operations":[{"op":"upsert_page","page_key":"bad key","title":"Alpha","summary":"summary","body_markdown":"body"}]})";
    assert(!hydrastore::ParseAndValidateWikiPatch(invalid_key, allowed, &patch, &error));
    return 0;
}
