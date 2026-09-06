#include "faiss_snapshot.h"

#include <faiss/IndexFlat.h>
#include <faiss/IndexIDMap.h>
#include <faiss/index_io.h>

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

namespace hydrastore {

struct FaissSnapshot::Impl {
    std::unique_ptr<faiss::Index> index;
    int dimension = 0;
};

FaissSnapshot::FaissSnapshot() : impl_(new Impl()) {}
FaissSnapshot::~FaissSnapshot() = default;

bool FaissSnapshot::Load(const std::string &path, int dimension) {
    if (dimension <= 0) return false;
    std::unique_ptr<faiss::Index> loaded;
    try {
        loaded.reset(faiss::read_index(path.c_str()));
    } catch (...) {
        return false;
    }
    if (!loaded || loaded->d != dimension ||
        dynamic_cast<faiss::IndexIDMap2 *>(loaded.get()) == nullptr) return false;
    impl_->index = std::move(loaded);
    impl_->dimension = dimension;
    return true;
}

bool FaissSnapshot::Search(const std::vector<float> &query, int top_k,
                           std::vector<std::int64_t> *ids,
                           std::vector<float> *scores) const {
    if (!ids || !scores || !impl_->index || impl_->dimension <= 0 ||
        query.size() != static_cast<std::size_t>(impl_->dimension) || top_k <= 0) return false;
    const int count = std::min<int>(top_k, static_cast<int>(impl_->index->ntotal));
    ids->assign(static_cast<std::size_t>(count), 0);
    scores->assign(static_cast<std::size_t>(count), 0.0f);
    if (count == 0) return true;
    std::vector<faiss::Index::idx_t> labels(static_cast<std::size_t>(count), -1);
    impl_->index->search(1, query.data(), count, scores->data(), labels.data());
    for (int i = 0; i < count; ++i) (*ids)[static_cast<std::size_t>(i)] = static_cast<std::int64_t>(labels[static_cast<std::size_t>(i)]);
    return true;
}

bool FaissSnapshot::Build(const std::string &path, int dimension,
                          const std::vector<std::int64_t> &ids,
                          const std::vector<std::vector<float>> &vectors) {
    if (dimension <= 0 || ids.size() != vectors.size()) return false;
    std::vector<float> flat;
    flat.reserve(ids.size() * static_cast<std::size_t>(dimension));
    for (const auto &vector : vectors) {
        if (vector.size() != static_cast<std::size_t>(dimension)) return false;
        flat.insert(flat.end(), vector.begin(), vector.end());
    }
    const std::filesystem::path target(path);
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) return false;
    const std::string temporary = path + ".tmp." + std::to_string(static_cast<long long>(::getpid()));
    std::unique_ptr<faiss::Index> base(new faiss::IndexFlatIP(dimension));
    std::unique_ptr<faiss::IndexIDMap2> index(new faiss::IndexIDMap2(base.release()));
    if (!ids.empty()) {
        std::vector<faiss::Index::idx_t> faiss_ids;
        faiss_ids.reserve(ids.size());
        for (std::int64_t id : ids) faiss_ids.push_back(static_cast<faiss::Index::idx_t>(id));
        index->add_with_ids(static_cast<faiss::Index::idx_t>(ids.size()), flat.data(), faiss_ids.data());
    }
    try {
        faiss::write_index(index.get(), temporary.c_str());
    } catch (...) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    const int fd = open(temporary.c_str(), O_RDONLY);
    if (fd < 0 || fsync(fd) != 0) {
        if (fd >= 0) close(fd);
        std::filesystem::remove(temporary, ec);
        return false;
    }
    close(fd);
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

}  // namespace hydrastore
