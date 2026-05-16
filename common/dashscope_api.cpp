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
#define DASHSCOPE_GEN_URL  "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation"

/* ====== 进程级 last-error 记录 ====== */
static char g_ds_err_code[64]  = {0};
static char g_ds_err_msg[512]  = {0};
static long g_ds_http_code     = 0;

extern "C" const char *dashscope_last_error_code(void) { return g_ds_err_code; }
extern "C" const char *dashscope_last_error_msg(void)  { return g_ds_err_msg;  }
extern "C" long dashscope_last_http_code(void)         { return g_ds_http_code; }

extern "C" void dashscope_clear_last_error(void)
{
    g_ds_err_code[0] = '\0';
    g_ds_err_msg[0]  = '\0';
    g_ds_http_code   = 0;
}

extern "C" int dashscope_last_error_is_api_key(void)
{
    if (g_ds_http_code == 401 || g_ds_http_code == 403) return 1;
    if (g_ds_err_code[0] == '\0') return 0;
    /* DashScope 常见 Key/欠费/权限类错误码 */
    static const char *KEY_ERRS[] = {
        "InvalidApiKey", "Unauthorized", "AccessDenied",
        "InvalidParameter.ApiKey", "AuthenticationError",
        "Arrearage", "InsufficientQuota", "InsufficientBalance",
        "AccountAbnormal", "ModelAccessDenied",
        NULL
    };
    for (int i = 0; KEY_ERRS[i]; i++) {
        if (strcasecmp(g_ds_err_code, KEY_ERRS[i]) == 0) return 1;
    }
    /* 部分错误码以 "InvalidApiKey." 为前缀 */
    if (strncasecmp(g_ds_err_code, "InvalidApiKey", 13) == 0) return 1;
    return 0;
}

/* 内部：从响应体 JSON 抽取 code/message 到 last-error */
static void capture_api_error(cJSON *resp, long http_code)
{
    g_ds_http_code = http_code;
    if (!resp) {
        if (g_ds_err_code[0] == '\0') {
            snprintf(g_ds_err_code, sizeof(g_ds_err_code), "ParseError");
            snprintf(g_ds_err_msg, sizeof(g_ds_err_msg),
                     "failed to parse response (http=%ld)", http_code);
        }
        return;
    }
    cJSON *code_item = cJSON_GetObjectItem(resp, "code");
    cJSON *msg_item  = cJSON_GetObjectItem(resp, "message");
    if (code_item && code_item->valuestring) {
        strncpy(g_ds_err_code, code_item->valuestring, sizeof(g_ds_err_code) - 1);
        g_ds_err_code[sizeof(g_ds_err_code) - 1] = '\0';
    }
    if (msg_item && msg_item->valuestring) {
        strncpy(g_ds_err_msg, msg_item->valuestring, sizeof(g_ds_err_msg) - 1);
        g_ds_err_msg[sizeof(g_ds_err_msg) - 1] = '\0';
    }
    /* HTTP 401/403 但响应里没 code，给一个默认 */
    if (g_ds_err_code[0] == '\0' && (http_code == 401 || http_code == 403)) {
        snprintf(g_ds_err_code, sizeof(g_ds_err_code), "Unauthorized");
        if (g_ds_err_msg[0] == '\0') {
            snprintf(g_ds_err_msg, sizeof(g_ds_err_msg),
                     "HTTP %ld from DashScope", http_code);
        }
    }
}

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
    long http_code = 0;
    dashscope_clear_last_error();
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
        snprintf(g_ds_err_code, sizeof(g_ds_err_code), "NetworkError");
        snprintf(g_ds_err_msg, sizeof(g_ds_err_msg), "%s", curl_easy_strerror(res));
        goto END;
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    LOG(DS_LOG_MODULE, DS_LOG_PROC, "describe_image response: %.500s\n", response.data);

    // 解析响应
    {
        cJSON *resp = cJSON_Parse(response.data);
        if (!resp) {
            LOG(DS_LOG_MODULE, DS_LOG_PROC, "parse response failed\n");
            capture_api_error(NULL, http_code);
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
            capture_api_error(resp, http_code);
            cJSON_Delete(resp);
            goto END;
        }
        /* HTTP 4xx 但响应不是标准 code/message 结构 */
        if (http_code >= 400) {
            capture_api_error(resp, http_code);
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
    long http_code = 0;
    dashscope_clear_last_error();
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
        snprintf(g_ds_err_code, sizeof(g_ds_err_code), "NetworkError");
        snprintf(g_ds_err_msg, sizeof(g_ds_err_msg), "%s", curl_easy_strerror(res));
        goto END;
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    LOG(DS_LOG_MODULE, DS_LOG_PROC, "embedding response: %.500s\n", response.data);

    // 解析响应: output.embeddings[0].embedding[]
    {
        cJSON *resp = cJSON_Parse(response.data);
        if (!resp) {
            LOG(DS_LOG_MODULE, DS_LOG_PROC, "parse embedding response failed\n");
            capture_api_error(NULL, http_code);
            goto END;
        }

        cJSON *code_item = cJSON_GetObjectItem(resp, "code");
        if (code_item && code_item->valuestring) {
            LOG(DS_LOG_MODULE, DS_LOG_PROC, "Embedding API error: %s\n", code_item->valuestring);
            capture_api_error(resp, http_code);
            cJSON_Delete(resp);
            goto END;
        }
        if (http_code >= 400) {
            capture_api_error(resp, http_code);
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

/**
 * 调用 qwen-turbo / qwen-plus 等通用文本模型生成中文摘要 + tag
 * 输出格式约定：模型必须返回 JSON { "summary": "...", "tags": ["a","b","c"] }
 */
int dashscope_summarize_text(const char *api_key, const char *model,
                              const char *text,
                              char *out_summary, int sum_max_len,
                              char *out_tags_json, int tags_max_len)
{
    int ret = -1;
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    struct CurlBuffer response;
    char auth_header[512] = {0};
    CURLcode res;
    long http_code = 0;
    dashscope_clear_last_error();
    curl_buffer_init(&response);

    if (!api_key || !text || !out_summary || sum_max_len <= 0) {
        snprintf(g_ds_err_code, sizeof(g_ds_err_code), "InvalidParameter");
        snprintf(g_ds_err_msg, sizeof(g_ds_err_msg), "missing argument");
        goto END;
    }
    if (out_tags_json && tags_max_len > 0) {
        snprintf(out_tags_json, tags_max_len, "[]");
    }
    out_summary[0] = '\0';

    /* 限长，避免上下文过大 */
    int text_len = (int)strlen(text);
    if (text_len > 6000) text_len = 6000;

    /* 构造请求 */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", (model && *model) ? model : "qwen-turbo");

    cJSON *input = cJSON_CreateObject();
    cJSON *messages = cJSON_CreateArray();

    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content",
        "你是一个文档知识抽取助手。读取用户给的文档片段，"
        "用中文给出 100~200 字的核心摘要，并提炼 3~5 个关键概念作为标签。"
        "必须严格只输出 JSON，不要任何额外说明，格式：\n"
        "{\"summary\":\"...\",\"tags\":[\"概念1\",\"概念2\"]}");
    cJSON_AddItemToArray(messages, sys_msg);

    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    /* 把文本片段塞进去（截断到 text_len） */
    {
        char *truncated = (char *)malloc(text_len + 1);
        if (!truncated) { cJSON_Delete(root); goto END; }
        memcpy(truncated, text, text_len);
        truncated[text_len] = '\0';
        cJSON_AddStringToObject(user_msg, "content", truncated);
        free(truncated);
    }
    cJSON_AddItemToArray(messages, user_msg);

    cJSON_AddItemToObject(input, "messages", messages);
    cJSON_AddItemToObject(root, "input", input);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "result_format", "message");
    cJSON_AddNumberToObject(params, "max_tokens", 600);
    cJSON_AddNumberToObject(params, "temperature", 0.3);
    cJSON_AddItemToObject(root, "parameters", params);

    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) { cJSON_Delete(root); goto END; }

    curl = curl_easy_init();
    if (!curl) { free(json_str); cJSON_Delete(root); goto END; }

    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, DASHSCOPE_GEN_URL);
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
        snprintf(g_ds_err_code, sizeof(g_ds_err_code), "NetworkError");
        snprintf(g_ds_err_msg, sizeof(g_ds_err_msg), "%s", curl_easy_strerror(res));
        goto END;
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    LOG(DS_LOG_MODULE, DS_LOG_PROC, "summarize response: %.500s\n", response.data);

    {
        cJSON *resp = cJSON_Parse(response.data);
        if (!resp) { capture_api_error(NULL, http_code); goto END; }

        cJSON *code_item = cJSON_GetObjectItem(resp, "code");
        if (code_item && code_item->valuestring) {
            capture_api_error(resp, http_code);
            cJSON_Delete(resp); goto END;
        }
        if (http_code >= 400) {
            capture_api_error(resp, http_code);
            cJSON_Delete(resp); goto END;
        }

        /* output.choices[0].message.content（result_format=message） */
        cJSON *output = cJSON_GetObjectItem(resp, "output");
        const char *content_text = NULL;
        if (output) {
            cJSON *choices = cJSON_GetObjectItem(output, "choices");
            if (choices && cJSON_GetArraySize(choices) > 0) {
                cJSON *choice0 = cJSON_GetArrayItem(choices, 0);
                cJSON *msg = cJSON_GetObjectItem(choice0, "message");
                if (msg) {
                    cJSON *cont = cJSON_GetObjectItem(msg, "content");
                    if (cont && cont->valuestring) content_text = cont->valuestring;
                }
            }
            /* 兼容 result_format=text 的旧返回 */
            if (!content_text) {
                cJSON *text_item = cJSON_GetObjectItem(output, "text");
                if (text_item && text_item->valuestring) content_text = text_item->valuestring;
            }
        }
        if (!content_text || !*content_text) {
            snprintf(g_ds_err_code, sizeof(g_ds_err_code), "EmptyResponse");
            snprintf(g_ds_err_msg, sizeof(g_ds_err_msg), "model returned empty content");
            cJSON_Delete(resp); goto END;
        }

        /* content_text 应为 JSON。容错：找第一个 { 到最后一个 } */
        const char *lbrace = strchr(content_text, '{');
        const char *rbrace = strrchr(content_text, '}');
        if (!lbrace || !rbrace || rbrace <= lbrace) {
            /* 退化：当成纯摘要文本 */
            strncpy(out_summary, content_text, sum_max_len - 1);
            out_summary[sum_max_len - 1] = '\0';
            ret = 0;
            cJSON_Delete(resp);
            goto END;
        }
        int json_len = (int)(rbrace - lbrace + 1);
        char *inner = (char *)malloc(json_len + 1);
        if (!inner) { cJSON_Delete(resp); goto END; }
        memcpy(inner, lbrace, json_len);
        inner[json_len] = '\0';

        cJSON *parsed = cJSON_Parse(inner);
        free(inner);
        if (!parsed) {
            /* 退化：当成纯摘要文本 */
            strncpy(out_summary, content_text, sum_max_len - 1);
            out_summary[sum_max_len - 1] = '\0';
            ret = 0;
            cJSON_Delete(resp);
            goto END;
        }

        cJSON *sum_item = cJSON_GetObjectItem(parsed, "summary");
        if (sum_item && sum_item->valuestring) {
            strncpy(out_summary, sum_item->valuestring, sum_max_len - 1);
            out_summary[sum_max_len - 1] = '\0';
        }

        if (out_tags_json && tags_max_len > 0) {
            cJSON *tags_item = cJSON_GetObjectItem(parsed, "tags");
            if (tags_item && cJSON_IsArray(tags_item)) {
                char *tags_str = cJSON_PrintUnformatted(tags_item);
                if (tags_str) {
                    strncpy(out_tags_json, tags_str, tags_max_len - 1);
                    out_tags_json[tags_max_len - 1] = '\0';
                    free(tags_str);
                }
            }
        }

        ret = (out_summary[0] != '\0') ? 0 : -1;
        if (ret != 0) {
            snprintf(g_ds_err_code, sizeof(g_ds_err_code), "EmptyResponse");
            snprintf(g_ds_err_msg, sizeof(g_ds_err_msg), "summary field missing");
        }
        cJSON_Delete(parsed);
        cJSON_Delete(resp);
    }

END:
    if (curl) curl_easy_cleanup(curl);
    if (headers) curl_slist_free_all(headers);
    curl_buffer_free(&response);
    return ret;
}
