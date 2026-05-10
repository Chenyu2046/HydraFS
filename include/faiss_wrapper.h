#ifndef _FAISS_WRAPPER_H_
#define _FAISS_WRAPPER_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 FAISS 索引（加载已有文件或创建新索引）
 *         如果当前进程已加载其他路径的索引，则自动切换到新路径。
 *
 * @param index_path  索引文件路径
 * @param dimension   向量维度（如 1024）
 *
 * @returns  0 成功, -1 失败
 */
int faiss_init(const char *index_path, int dimension);

/**
 * @brief  添加向量到索引
 *
 * @param vector     float 向量数组
 * @param dimension  向量维度
 *
 * @returns  添加后的 faiss_id (>=0), -1 失败
 */
int faiss_add(float *vector, int dimension);

/**
 * @brief  向量相似度搜索
 *
 * @param query      查询向量
 * @param dimension  向量维度
 * @param topk       返回前 k 个结果
 * @param out_ids    (out) 结果 faiss_id 数组
 * @param out_scores (out) 结果相似度分数数组
 *
 * @returns  实际返回的结果数 (>=0), -1 失败
 */
int faiss_search(float *query, int dimension, int topk,
                 long *out_ids, float *out_scores);

/**
 * @brief  持久化索引到磁盘
 *
 * @param index_path  保存路径
 *
 * @returns  0 成功, -1 失败
 */
int faiss_save(const char *index_path);

/**
 * @brief  设置是否在 faiss_add 后自动保存索引
 *
 * @param enabled 1=自动保存，0=关闭自动保存
 */
void faiss_set_auto_save(int enabled);

/**
 * @brief  获取当前索引中的向量总数
 *
 * @returns  向量数量
 */
long faiss_get_ntotal(void);

/**
 * @brief  对向量进行 L2 归一化（原地修改）
 *         归一化后内积 = 余弦相似度，范围 [-1, 1]
 *
 * @param vector     float 向量数组
 * @param dimension  向量维度
 */
void vector_l2_normalize(float *vector, int dimension);

/**
 * @brief  释放并重置全局索引（用于 rebuild 场景）
 */
void faiss_reset(void);

#ifdef __cplusplus
}
#endif

#endif
