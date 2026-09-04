#ifndef HYDRASTORE_HASH_UTIL_H
#define HYDRASTORE_HASH_UTIL_H

#include <cstddef>
#include <string>

namespace hydrastore {

class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256 &) = delete;
    Sha256 &operator=(const Sha256 &) = delete;

    void Update(const void *data, std::size_t size);
    std::string FinalHex();

private:
    struct Impl;
    Impl *impl_;
};

}  // namespace hydrastore

#endif
