#include "document_extractor.h"

#include <cassert>
#include <string>

int main() {
    std::string output;
    const hydrastore::FilterResult bounded = hydrastore::RunFilter(
        "yes", {"utf8"}, 64, 1000, &output);
    assert(bounded.ok);
    assert(bounded.truncated);
    assert(output.size() <= 64);

    const hydrastore::FilterResult timed_out = hydrastore::RunFilter(
        "sh", {"-c", "sleep 5"}, 1024, 100, &output);
    assert(!timed_out.ok);
    assert(timed_out.timed_out);
    assert(timed_out.exit_code >= 128 || timed_out.exit_code == -1);
    return 0;
}
