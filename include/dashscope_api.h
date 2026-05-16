#ifndef _DASHSCOPE_API_H_
#define _DASHSCOPE_API_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  调用 Qwen-VL 多模态模型，传入图片 URL，返回文本描述
 *
 * @param api_key     DashScope API Key
 * @param image_url   图片的公网访问 URL
 * @param out_desc    (out) 返回的文本描述
 * @param max_len     out_desc 缓冲区最大长度
 *
 * @returns  0 成功, -1 失败
 */
int dashscope_describe_image(const char *api_key, const char *image_url,
                              char *out_desc, int max_len);

/**
 * @brief  调用 text-embedding-v3 模型，传入文本，返回 float 向量
 *
 * @param api_key     DashScope API Key
 * @param model       模型名称，如 "text-embedding-v3"
 * @param text        输入文本
 * @param out_vector  (out) 返回的 float 向量
 * @param dimension   向量维度（如 1024）
 *
 * @returns  0 成功, -1 失败
 */
int dashscope_get_embedding(const char *api_key, const char *model,
                             const char *text,
                             float *out_vector, int dimension);

/**
 * @brief  调用通用文本模型（如 qwen-turbo）生成摘要 + 关键词标签
 *
 * @param api_key       DashScope API Key
 * @param model         模型名称，如 "qwen-turbo" / "qwen-plus"
 * @param text          输入文本（建议 < 6000 字符）
 * @param out_summary   (out) 中文摘要（100~300 字）
 * @param sum_max_len   summary 缓冲区长度
 * @param out_tags_json (out) 标签 JSON 数组字符串，如 ["FastDFS","Faiss","C++"]
 * @param tags_max_len  tags 缓冲区长度
 *
 * @returns  0 成功, -1 失败（失败时可调用 dashscope_last_error_* 查看原因）
 */
int dashscope_summarize_text(const char *api_key, const char *model,
                              const char *text,
                              char *out_summary, int sum_max_len,
                              char *out_tags_json, int tags_max_len);

/* ====== 错误信息查询（进程级 last-error） ====== */

/* 错误码字符串：如 "InvalidApiKey" / "Throttling" / "" */
const char *dashscope_last_error_code(void);
/* 人类可读消息 */
const char *dashscope_last_error_msg(void);
/* HTTP 状态码（如 401 / 429 / 0） */
long dashscope_last_http_code(void);
/* 是否属于 API Key 鉴权 / 欠费类错误 */
int dashscope_last_error_is_api_key(void);
/* 在新一次调用前清空（库内部自动调用，外部一般不用） */
void dashscope_clear_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
