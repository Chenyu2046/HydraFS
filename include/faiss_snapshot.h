#ifndef HYDRASTORE_FAISS_SNAPSHOT_H
#define HYDRASTORE_FAISS_SNAPSHOT_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hydrastore {

class FaissSnapshot {
public:
    FaissSnapshot();
    ~FaissSnapshot();
    FaissSnapshot(const FaissSnapshot &) = delete;
    FaissSnapshot &operator=(const FaissSnapshot &) = delete;

    bool Load(const std::string &path, int dimension);
    bool Search(const std::vector<float> &query, int topk,
                std::vector<std::int64_t> *ids,
                std::vector<float> *scores) const;

    static bool Build(const std::string &output_path, int dimension,
                      const std::vector<std::int64_t> &ids,
                      const std::vector<std::vector<float>> &vectors);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace hydrastore

#endif
