/**
 * @file faiss_wrapper.cpp
 * @brief  封装 FAISS 向量索引操作
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#include <faiss/IndexFlat.h>
#include <faiss/index_io.h>

extern "C" {
#include "make_log.h"
}
#include "faiss_wrapper.h"

#define FW_LOG_MODULE "cgi"
#define FW_LOG_PROC   "faiss"

// 全局索引实例（进程内单例）
static faiss::IndexFlatIP *g_index = NULL;
static int g_dimension = 0;
static char g_index_path[512] = {0};
static int g_auto_save = 1;

static int file_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0);
}

/**
 * 初始化 FAISS 索引
 */
int faiss_init(const char *index_path, int dimension)
{
    if (g_index != NULL && strcmp(g_index_path, index_path) == 0 && g_dimension == dimension) {
        return 0;
    }

    if (g_index != NULL) {
        delete g_index;
        g_index = NULL;
        g_dimension = 0;
        memset(g_index_path, 0, sizeof(g_index_path));
    }

    g_dimension = dimension;
    strncpy(g_index_path, index_path, sizeof(g_index_path) - 1);

    if (file_exists(index_path)) {
        // 加载已有索引
        try {
            faiss::Index *loaded = faiss::read_index(index_path);
            g_index = dynamic_cast<faiss::IndexFlatIP *>(loaded);
            if (!g_index) {
                LOG(FW_LOG_MODULE, FW_LOG_PROC, "loaded index is not IndexFlatIP, creating new\n");
                delete loaded;
                g_index = new faiss::IndexFlatIP(dimension);
            } else {
                LOG(FW_LOG_MODULE, FW_LOG_PROC, "loaded index from %s, ntotal=%ld\n",
                    index_path, g_index->ntotal);
            }
        } catch (const std::exception &e) {
            LOG(FW_LOG_MODULE, FW_LOG_PROC, "read_index failed: %s, creating new\n", e.what());
            g_index = new faiss::IndexFlatIP(dimension);
        }
    } else {
        LOG(FW_LOG_MODULE, FW_LOG_PROC, "no index file at %s, creating new (dim=%d)\n",
            index_path, dimension);
        g_index = new faiss::IndexFlatIP(dimension);
    }

    return 0;
}

/**
 * L2 归一化向量（原地修改）
 * 归一化后，IndexFlatIP 的内积 = 余弦相似度，范围 [-1, 1]
 */
void vector_l2_normalize(float *vector, int dimension)
{
    float norm = 0.0f;
    for (int i = 0; i < dimension; i++) {
        norm += vector[i] * vector[i];
    }
    norm = sqrtf(norm);
    if (norm > 1e-10f) {
        for (int i = 0; i < dimension; i++) {
            vector[i] /= norm;
        }
    }
}

/**
 * 添加向量，返回 faiss_id
 * 注意：会对输入向量做 L2 归一化（原地修改）
 */
int faiss_add(float *vector, int dimension)
{
    if (!g_index) {
        LOG(FW_LOG_MODULE, FW_LOG_PROC, "faiss_add: index not initialized\n");
        return -1;
    }

    if (dimension != g_dimension) {
        LOG(FW_LOG_MODULE, FW_LOG_PROC, "faiss_add: dimension mismatch %d vs %d\n",
            dimension, g_dimension);
        return -1;
    }

    // L2 归一化，使内积 = 余弦相似度
    vector_l2_normalize(vector, dimension);

    // faiss_id = 添加前的 ntotal
    int faiss_id = (int)g_index->ntotal;

    try {
        g_index->add(1, vector);
    } catch (const std::exception &e) {
        LOG(FW_LOG_MODULE, FW_LOG_PROC, "faiss_add failed: %s\n", e.what());
        return -1;
    }

    LOG(FW_LOG_MODULE, FW_LOG_PROC, "faiss_add: id=%d, ntotal=%ld\n", faiss_id, g_index->ntotal);

    // 每次添加后自动持久化
    if (g_auto_save) {
        faiss_save(g_index_path);
    }

    return faiss_id;
}

/**
 * 向量搜索
 */
int faiss_search(float *query, int dimension, int topk,
                 long *out_ids, float *out_scores)
{
    if (!g_index) {
        LOG(FW_LOG_MODULE, FW_LOG_PROC, "faiss_search: index not initialized\n");
        return -1;
    }

    if (g_index->ntotal == 0) {
        LOG(FW_LOG_MODULE, FW_LOG_PROC, "faiss_search: index is empty\n");
        return 0;
    }

    // 搜索数不能超过索引中的向量数
    long actual_k = topk;
    if (actual_k > g_index->ntotal) {
        actual_k = g_index->ntotal;
    }

    // FAISS search 使用 faiss::Index::idx_t (int64_t)
    faiss::Index::idx_t *ids = new faiss::Index::idx_t[actual_k];
    float *scores = new float[actual_k];

    try {
        g_index->search(1, query, (int)actual_k, scores, ids);
    } catch (const std::exception &e) {
        LOG(FW_LOG_MODULE, FW_LOG_PROC, "faiss_search failed: %s\n", e.what());
        delete[] ids;
        delete[] scores;
        return -1;
    }

    int count = 0;
    for (int i = 0; i < actual_k; i++) {
        if (ids[i] >= 0) {
            out_ids[count] = (long)ids[i];
            out_scores[count] = scores[i];
            count++;
        }
    }

    delete[] ids;
    delete[] scores;

    LOG(FW_LOG_MODULE, FW_LOG_PROC, "faiss_search: topk=%d, found=%d\n", topk, count);
    return count;
}

/**
 * 持久化索引到文件
 */
int faiss_save(const char *index_path)
{
    if (!g_index) {
        return -1;
    }

    try {
        faiss::write_index(g_index, index_path);
        LOG(FW_LOG_MODULE, FW_LOG_PROC, "faiss_save: saved to %s, ntotal=%ld\n",
            index_path, g_index->ntotal);
    } catch (const std::exception &e) {
        LOG(FW_LOG_MODULE, FW_LOG_PROC, "faiss_save failed: %s\n", e.what());
        return -1;
    }

    return 0;
}

void faiss_set_auto_save(int enabled)
{
    g_auto_save = enabled ? 1 : 0;
}

/**
 * 获取索引中向量总数
 */
long faiss_get_ntotal(void)
{
    if (!g_index) return 0;
    return (long)g_index->ntotal;
}

/**
 * 释放并重置全局索引（用于 rebuild 场景）
 */
void faiss_reset(void)
{
    if (g_index) {
        delete g_index;
        g_index = NULL;
    }
    g_dimension = 0;
    g_auto_save = 1;
    memset(g_index_path, 0, sizeof(g_index_path));
}
