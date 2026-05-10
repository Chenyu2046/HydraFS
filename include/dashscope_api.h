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

#ifdef __cplusplus
}
#endif

#endif
