/**
 * @file dashscope_api.cpp
 * @brief  封装 libcurl 调用阿里百炼 DashScope API
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

extern "C" {
#include "cJSON.h"
#include "make_log.h"
}
#include "dashscope_api.h"

#define DS_LOG_MODULE "cgi"
#define DS_LOG_PROC   "dashscope"

#define DASHSCOPE_VL_URL   "https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation"
#define DASHSCOPE_EMB_URL  "https://dashscope.aliyuncs.com/api/v1/services/embeddings/text-embedding/text-embedding"

// libcurl 写回调：将响应体追加到动态缓冲区
struct CurlBuffer {
    char *data;
    size_t size;
    size_t capacity;
};

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    struct CurlBuffer *buf = (struct CurlBuffer *)userdata;

    // 扩容
    while (buf->size + total + 1 > buf->capacity) {
        buf->capacity = buf->capacity * 2 + total;
        buf->data = (char *)realloc(buf->data, buf->capacity);
        if (!buf->data) return 0;
    }

    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

static void curl_buffer_init(struct CurlBuffer *buf)
{
    buf->capacity = 4096;
    buf->data = (char *)malloc(buf->capacity);
    buf->data[0] = '\0';
    buf->size = 0;
}

static void curl_buffer_free(struct CurlBuffer *buf)
{
    if (buf->data) {
        free(buf->data);
        buf->data = NULL;
    }
    buf->size = 0;
    buf->capacity = 0;
}

/**
 * 调用 Qwen-VL 多模态模型描述图片
 */
int dashscope_describe_image(const char *api_key, const char *image_url,
                              char *out_desc, int max_len)
{
    int ret = -1;
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    struct CurlBuffer response;
    char auth_header[512] = {0};
    CURLcode res;
    curl_buffer_init(&response);

    // 构造请求 JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "qwen-vl-plus");

    cJSON *input = cJSON_CreateObject();
    cJSON *messages = cJSON_CreateArray();
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");

    cJSON *content = cJSON_CreateArray();
    cJSON *img_item = cJSON_CreateObject();
    cJSON_AddStringToObject(img_item, "image", image_url);
    cJSON_AddItemToArray(content, img_item);

    cJSON *text_item = cJSON_CreateObject();
    cJSON_AddStringToObject(text_item, "text", "请用中文详细描述这张图片的内容，包括主要物体、场景、颜色、文字等信息。");
    cJSON_AddItemToArray(content, text_item);

    cJSON_AddItemToObject(msg, "content", content);
    cJSON_AddItemToArray(messages, msg);
    cJSON_AddItemToObject(input, "messages", messages);
    cJSON_AddItemToObject(root, "input", input);

    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        LOG(DS_LOG_MODULE, DS_LOG_PROC, "cJSON_Print failed\n");
        cJSON_Delete(root);
        goto END;
    }

    LOG(DS_LOG_MODULE, DS_LOG_PROC, "describe_image request: %s\n", json_str);

    // libcurl 发送请求
    curl = curl_easy_init();
    if (!curl) {
        LOG(DS_LOG_MODULE, DS_LOG_PROC, "curl_easy_init failed\n");
        free(json_str);
        cJSON_Delete(root);
        goto END;
    }

    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, DASHSCOPE_VL_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    res = curl_easy_perform(curl);
    free(json_str);
    cJSON_Delete(root);

    if (res != CURLE_OK) {
        LOG(DS_LOG_MODULE, DS_LOG_PROC, "curl_easy_perform failed: %s\n", curl_easy_strerror(res));
        goto END;
    }

    LOG(DS_LOG_MODULE, DS_LOG_PROC, "describe_image response: %.500s\n", response.data);

    // 解析响应
    {
        cJSON *resp = cJSON_Parse(response.data);
        if (!resp) {
            LOG(DS_LOG_MODULE, DS_LOG_PROC, "parse response failed\n");
            goto END;
        }

        // 检查是否有错误
        cJSON *code_item = cJSON_GetObjectItem(resp, "code");
        if (code_item && code_item->valuestring) {
            LOG(DS_LOG_MODULE, DS_LOG_PROC, "API error code: %s\n", code_item->valuestring);
            cJSON *msg_item = cJSON_GetObjectItem(resp, "message");
            if (msg_item && msg_item->valuestring) {
                LOG(DS_LOG_MODULE, DS_LOG_PROC, "API error message: %s\n", msg_item->valuestring);
            }
            cJSON_Delete(resp);
            goto END;
        }

        // 提取描述文本: output.choices[0].message.content[0].text
        cJSON *output = cJSON_GetObjectItem(resp, "output");
        if (!output) { cJSON_Delete(resp); goto END; }

        cJSON *choices = cJSON_GetObjectItem(output, "choices");
        if (!choices || cJSON_GetArraySize(choices) == 0) { cJSON_Delete(resp); goto END; }

        cJSON *choice0 = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(choice0, "message");
        if (!message) { cJSON_Delete(resp); goto END; }

        cJSON *cont = cJSON_GetObjectItem(message, "content");
        if (!cont) { cJSON_Delete(resp); goto END; }

        // content 可能是数组或字符串
        const char *desc_text = NULL;
        if (cont->type == cJSON_Array && cJSON_GetArraySize(cont) > 0) {
            cJSON *first = cJSON_GetArrayItem(cont, 0);
            cJSON *text_obj = cJSON_GetObjectItem(first, "text");
            if (text_obj && text_obj->valuestring) {
                desc_text = text_obj->valuestring;
            }
        } else if (cont->type == cJSON_String) {
            desc_text = cont->valuestring;
        }

        if (desc_text) {
            strncpy(out_desc, desc_text, max_len - 1);
            out_desc[max_len - 1] = '\0';
            ret = 0;
        }

        cJSON_Delete(resp);
    }

END:
    if (curl) curl_easy_cleanup(curl);
    if (headers) curl_slist_free_all(headers);
    curl_buffer_free(&response);
    return ret;
}

/**
 * 调用 text-embedding-v3 获取文本向量
 */
int dashscope_get_embedding(const char *api_key, const char *model,
                             const char *text,
                             float *out_vector, int dimension)
{
    int ret = -1;
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    struct CurlBuffer response;
    char auth_header[512] = {0};
    CURLcode res;
    curl_buffer_init(&response);

    // 构造请求 JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model);

    cJSON *input = cJSON_CreateObject();
    cJSON *texts = cJSON_CreateArray();
    cJSON_AddItemToArray(texts, cJSON_CreateString(text));
    cJSON_AddItemToObject(input, "texts", texts);
    cJSON_AddItemToObject(root, "input", input);

    cJSON *parameters = cJSON_CreateObject();
    cJSON_AddNumberToObject(parameters, "dimension", dimension);
    cJSON_AddItemToObject(root, "parameters", parameters);

    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        LOG(DS_LOG_MODULE, DS_LOG_PROC, "cJSON_Print failed\n");
        cJSON_Delete(root);
        goto END;
    }

    LOG(DS_LOG_MODULE, DS_LOG_PROC, "embedding request: %.200s\n", json_str);

    curl = curl_easy_init();
    if (!curl) {
        LOG(DS_LOG_MODULE, DS_LOG_PROC, "curl_easy_init failed\n");
        free(json_str);
        cJSON_Delete(root);
        goto END;
    }

    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, DASHSCOPE_EMB_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    res = curl_easy_perform(curl);
    free(json_str);
    cJSON_Delete(root);

    if (res != CURLE_OK) {
        LOG(DS_LOG_MODULE, DS_LOG_PROC, "curl_easy_perform failed: %s\n", curl_easy_strerror(res));
        goto END;
    }

    LOG(DS_LOG_MODULE, DS_LOG_PROC, "embedding response: %.500s\n", response.data);

    // 解析响应: output.embeddings[0].embedding[]
    {
        cJSON *resp = cJSON_Parse(response.data);
        if (!resp) {
            LOG(DS_LOG_MODULE, DS_LOG_PROC, "parse embedding response failed\n");
            goto END;
        }

        cJSON *code_item = cJSON_GetObjectItem(resp, "code");
        if (code_item && code_item->valuestring) {
            LOG(DS_LOG_MODULE, DS_LOG_PROC, "Embedding API error: %s\n", code_item->valuestring);
            cJSON_Delete(resp);
            goto END;
        }

        cJSON *output = cJSON_GetObjectItem(resp, "output");
        if (!output) { cJSON_Delete(resp); goto END; }

        cJSON *embeddings = cJSON_GetObjectItem(output, "embeddings");
        if (!embeddings || cJSON_GetArraySize(embeddings) == 0) { cJSON_Delete(resp); goto END; }

        cJSON *emb0 = cJSON_GetArrayItem(embeddings, 0);
        cJSON *embedding = cJSON_GetObjectItem(emb0, "embedding");
        if (!embedding) { cJSON_Delete(resp); goto END; }

        int arr_size = cJSON_GetArraySize(embedding);
        if (arr_size < dimension) {
            LOG(DS_LOG_MODULE, DS_LOG_PROC, "embedding dim mismatch: got %d, expected %d\n", arr_size, dimension);
            cJSON_Delete(resp);
            goto END;
        }

        for (int i = 0; i < dimension; i++) {
            cJSON *val = cJSON_GetArrayItem(embedding, i);
            out_vector[i] = (float)val->valuedouble;
        }

        ret = 0;
        cJSON_Delete(resp);
    }

END:
    if (curl) curl_easy_cleanup(curl);
    if (headers) curl_slist_free_all(headers);
    curl_buffer_free(&response);
    return ret;
}
