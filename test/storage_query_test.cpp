#include "storage_query.h"

#include <cassert>
#include <string>

int main() {
    std::string value;
    assert(hydrastore::ParseQueryValue("objectId=abc&index=1", "objectId", &value));
    assert(value == "abc");
    const std::string oversized = "objectId=" + std::string(1024, 'x');
    assert(!hydrastore::ParseQueryValue(oversized.c_str(), "objectId", &value));
    assert(!hydrastore::ParseQueryValue("notobjectId=bad", "objectId", &value));
    return 0;
}
