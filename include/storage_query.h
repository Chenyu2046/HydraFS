#ifndef HYDRASTORE_STORAGE_QUERY_H
#define HYDRASTORE_STORAGE_QUERY_H

#include <string>

namespace hydrastore {

// Parse one bounded query parameter without writing through the legacy C parser.
bool ParseQueryValue(const char *query, const char *key, std::string *value,
                     std::size_t max_length = 1023);

}  // namespace hydrastore

#endif
