#include "storage_query.h"

#include <cstring>

namespace hydrastore {

bool ParseQueryValue(const char *query, const char *key, std::string *value,
                     std::size_t max_length) {
    if (!query || !key || !*key || !value) return false;
    const std::size_t key_length = std::strlen(key);
    const char *cursor = query;
    while (*cursor) {
        const char *token_end = std::strchr(cursor, '&');
        if (!token_end) token_end = cursor + std::strlen(cursor);
        if (static_cast<std::size_t>(token_end - cursor) > key_length &&
            std::strncmp(cursor, key, key_length) == 0 && cursor[key_length] == '=') {
            const char *begin = cursor + key_length + 1;
            const std::size_t length = static_cast<std::size_t>(token_end - begin);
            if (length > max_length) return false;
            value->assign(begin, length);
            return true;
        }
        cursor = *token_end ? token_end + 1 : token_end;
    }
    return false;
}

}  // namespace hydrastore
