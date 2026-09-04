#include "hash_util.h"

#include <openssl/sha.h>

#include <iomanip>
#include <sstream>

namespace hydrastore {

struct Sha256::Impl {
    SHA256_CTX context;
    bool finalized = false;
    std::string digest;
};

Sha256::Sha256() : impl_(new Impl()) {
    SHA256_Init(&impl_->context);
}

Sha256::~Sha256() {
    delete impl_;
}

void Sha256::Update(const void *data, std::size_t size) {
    if (!impl_->finalized && size != 0) {
        SHA256_Update(&impl_->context, data, size);
    }
}

std::string Sha256::FinalHex() {
    if (impl_->finalized) {
        return impl_->digest;
    }
    unsigned char digest[SHA256_DIGEST_LENGTH] = {0};
    SHA256_Final(digest, &impl_->context);
    impl_->finalized = true;

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char byte : digest) {
        out << std::setw(2) << static_cast<unsigned int>(byte);
    }
    impl_->digest = out.str();
    return impl_->digest;
}

}  // namespace hydrastore
