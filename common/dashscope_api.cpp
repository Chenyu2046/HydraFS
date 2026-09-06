#include "dashscope_api.h"

#include <curl/curl.h>

extern "C" {
#include "cJSON.h"
#include "make_log.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define DS_LOG_MODULE "cgi"
#define DS_LOG_PROC   "dashscope"
#define DASHSCOPE_VL_URL "https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation"
#define DASHSCOPE_TEXT_URL "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation"
#define DASHSCOPE_EMB_URL "https://dashscope.aliyuncs.com/api/v1/services/embeddings/text-embedding/text-embedding"

namespace {

struct CurlBuffer { std::string data; std::size_t limit = 8 * 1024 * 1024; };

size_t WriteResponse(void *ptr, size_t size, size_t count, void *userdata) {
    CurlBuffer *buffer = static_cast<CurlBuffer *>(userdata);
    const std::size_t bytes = size * count;
    if (buffer->data.size() + bytes > buffer->limit) return 0;
    buffer->data.append(static_cast<const char *>(ptr), bytes);
    return bytes;
}

bool Perform(const char *url, const char *api_key, const std::string &body,
             CurlBuffer *response) {
    if (!url || !api_key || !*api_key || !response) return false;
    CURL *curl = curl_easy_init();
    if (!curl) return false;
    char auth[768] = {0};
    std::snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteResponse);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    long status = 0;
    CURLcode result = CURLE_FAILED_INIT;
    for (int attempt = 0; attempt < 2; ++attempt) {
        response->data.clear();
        status = 0;
        result = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (result == CURLE_OK && status >= 200 && status < 300) break;
        const bool transient_curl = result == CURLE_COULDNT_CONNECT ||
                                    result == CURLE_OPERATION_TIMEDOUT ||
                                    result == CURLE_RECV_ERROR ||
                                    result == CURLE_SEND_ERROR;
        const bool transient_http = status == 429 || status >= 500;
        if (!transient_curl && !transient_http) break;
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK || status < 200 || status >= 300) {
        LOG(DS_LOG_MODULE, DS_LOG_PROC, "DashScope request failed: curl=%d http=%ld\n",
            static_cast<int>(result), status);
        return false;
    }
    return true;
}

const char *TextFromOutput(cJSON *response) {
    cJSON *output = cJSON_GetObjectItem(response, "output");
    if (!output) return nullptr;
    cJSON *choices = cJSON_GetObjectItem(output, "choices");
    if (choices && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = choice ? cJSON_GetObjectItem(choice, "message") : nullptr;
        cJSON *content = message ? cJSON_GetObjectItem(message, "content") : nullptr;
        if (content && content->type == cJSON_String) return content->valuestring;
        if (content && cJSON_GetArraySize(content) > 0) {
            cJSON *item = cJSON_GetArrayItem(content, 0);
            cJSON *text = item ? cJSON_GetObjectItem(item, "text") : nullptr;
            if (text && text->type == cJSON_String) return text->valuestring;
        }
    }
    cJSON *text = cJSON_GetObjectItem(output, "text");
    return text && text->type == cJSON_String ? text->valuestring : nullptr;
}

int CopyText(const char *text, char *out, int max_len) {
    if (!text || !out || max_len <= 1) return -1;
    std::strncpy(out, text, static_cast<std::size_t>(max_len - 1));
    out[max_len - 1] = '\0';
    return 0;
}

int Chat(const char *api_key, const char *model, const char *system_prompt,
         const char *user_prompt, char *out, int max_len) {
    if (!model || !*model || !user_prompt) return -1;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model);
    cJSON *input = cJSON_CreateObject();
    cJSON *messages = cJSON_CreateArray();
    if (system_prompt && *system_prompt) {
        cJSON *system = cJSON_CreateObject();
        cJSON_AddStringToObject(system, "role", "system");
        cJSON_AddStringToObject(system, "content", system_prompt);
        cJSON_AddItemToArray(messages, system);
    }
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", "user");
    cJSON_AddStringToObject(message, "content", user_prompt);
    cJSON_AddItemToArray(messages, message);
    cJSON_AddItemToObject(input, "messages", messages);
    cJSON_AddItemToObject(root, "input", input);
    cJSON *parameters = cJSON_CreateObject();
    cJSON_AddStringToObject(parameters, "result_format", "message");
    cJSON_AddItemToObject(root, "parameters", parameters);
    char *serialized = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!serialized) return -1;
    CurlBuffer response;
    const bool success = Perform(DASHSCOPE_TEXT_URL, api_key, serialized, &response);
    free(serialized);
    if (!success) return -1;
    cJSON *parsed = cJSON_Parse(response.data.c_str());
    if (!parsed) return -1;
    const int result = CopyText(TextFromOutput(parsed), out, max_len);
    cJSON_Delete(parsed);
    return result;
}

std::string Base64(const unsigned char *data, std::size_t size) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((size + 2) / 3) * 4);
    for (std::size_t i = 0; i < size;) {
        const std::size_t remaining = size - i;
        const unsigned int a = data[i++];
        const unsigned int b = remaining > 1 ? data[i++] : 0;
        const unsigned int c = remaining > 2 ? data[i++] : 0;
        encoded.push_back(table[a >> 2]);
        encoded.push_back(table[((a & 3) << 4) | (b >> 4)]);
        encoded.push_back(remaining > 1 ? table[((b & 15) << 2) | (c >> 6)] : '=');
        encoded.push_back(remaining > 2 ? table[c & 63] : '=');
    }
    return encoded;
}

}  // namespace

extern "C" int dashscope_generate_json(const char *api_key, const char *model,
                                         const char *system_prompt, const char *user_prompt,
                                         char *out_json, size_t output_size) {
    return Chat(api_key, model, system_prompt, user_prompt, out_json,
                static_cast<int>(output_size));
}

static int DescribeImageModel(const char *api_key, const char *model, const char *image_url,
                              char *out_desc, int max_len) {
    if (!image_url) return -1;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model && *model ? model : "qwen-vl-plus");
    cJSON *input = cJSON_CreateObject();
    cJSON *messages = cJSON_CreateArray();
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", "user");
    cJSON *content = cJSON_CreateArray();
    cJSON *image = cJSON_CreateObject();
    cJSON_AddStringToObject(image, "image", image_url);
    cJSON_AddItemToArray(content, image);
    cJSON *text = cJSON_CreateObject();
    cJSON_AddStringToObject(text, "text", "请用中文描述图片中的主要物体、场景、颜色和可见文字。");
    cJSON_AddItemToArray(content, text);
    cJSON_AddItemToObject(message, "content", content);
    cJSON_AddItemToArray(messages, message);
    cJSON_AddItemToObject(input, "messages", messages);
    cJSON_AddItemToObject(root, "input", input);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return -1;
    CurlBuffer response;
    const bool success = Perform(DASHSCOPE_VL_URL, api_key, body, &response);
    free(body);
    if (!success) return -1;
    cJSON *parsed = cJSON_Parse(response.data.c_str());
    if (!parsed) return -1;
    const int result = CopyText(TextFromOutput(parsed), out_desc, max_len);
    cJSON_Delete(parsed);
    return result;
}

extern "C" int dashscope_describe_image(const char *api_key, const char *image_url,
                                          char *out_desc, int max_len) {
    return DescribeImageModel(api_key, "qwen-vl-plus", image_url, out_desc, max_len);
}

extern "C" int dashscope_describe_image_model(const char *api_key, const char *model,
                                               const char *image_url, char *out_desc, int max_len) {
    return DescribeImageModel(api_key, model, image_url, out_desc, max_len);
}

static int DescribeImageFileModel(const char *api_key, const char *model, const char *image_path,
                                  const char *file_type, char *out_desc, int max_len) {
    if (!image_path) return -1;
    FILE *file = std::fopen(image_path, "rb");
    if (!file) return -1;
    if (std::fseek(file, 0, SEEK_END) != 0) { std::fclose(file); return -1; }
    const long file_size = std::ftell(file);
    if (file_size <= 0 || file_size > 16L * 1024L * 1024L || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file); return -1;
    }
    std::vector<unsigned char> data(static_cast<std::size_t>(file_size));
    if (std::fread(data.data(), 1, data.size(), file) != data.size()) { std::fclose(file); return -1; }
    std::fclose(file);
    const std::string url = std::string("data:") + image_mime_type(file_type) + ";base64," + Base64(data.data(), data.size());
    return DescribeImageModel(api_key, model, url.c_str(), out_desc, max_len);
}

extern "C" int dashscope_describe_image_file(const char *api_key, const char *image_path,
                                               const char *file_type, char *out_desc, int max_len) {
    return DescribeImageFileModel(api_key, "qwen-vl-plus", image_path, file_type, out_desc, max_len);
}

extern "C" int dashscope_describe_image_file_model(const char *api_key, const char *model,
                                                    const char *image_path, const char *file_type,
                                                    char *out_desc, int max_len) {
    return DescribeImageFileModel(api_key, model, image_path, file_type, out_desc, max_len);
}

extern "C" int dashscope_get_embedding(const char *api_key, const char *model,
                                         const char *text, float *out_vector, int dimension) {
    if (!model || !text || !out_vector || dimension <= 0) return -1;
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
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return -1;
    CurlBuffer response;
    const bool success = Perform(DASHSCOPE_EMB_URL, api_key, body, &response);
    free(body);
    if (!success) return -1;
    cJSON *parsed = cJSON_Parse(response.data.c_str());
    if (!parsed) return -1;
    cJSON *output = cJSON_GetObjectItem(parsed, "output");
    cJSON *embeddings = output ? cJSON_GetObjectItem(output, "embeddings") : nullptr;
    cJSON *first = embeddings && cJSON_GetArraySize(embeddings) > 0 ? cJSON_GetArrayItem(embeddings, 0) : nullptr;
    cJSON *embedding = first ? cJSON_GetObjectItem(first, "embedding") : nullptr;
    bool valid = embedding && cJSON_GetArraySize(embedding) >= dimension;
    if (valid) {
        for (int i = 0; i < dimension; ++i) {
            cJSON *value = cJSON_GetArrayItem(embedding, i);
            if (!value || value->type != cJSON_Number) {
                valid = false;
                break;
            }
            out_vector[i] = static_cast<float>(value->valuedouble);
        }
    }
    cJSON_Delete(parsed);
    return valid ? 0 : -1;
}
