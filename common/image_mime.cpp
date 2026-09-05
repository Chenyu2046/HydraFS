#include "dashscope_api.h"

#include <strings.h>

const char *image_mime_type(const char *file_type) {
    if (file_type) {
        if (strcasecmp(file_type, "png") == 0) return "image/png";
        if (strcasecmp(file_type, "jpg") == 0 || strcasecmp(file_type, "jpeg") == 0) {
            return "image/jpeg";
        }
        if (strcasecmp(file_type, "gif") == 0) return "image/gif";
        if (strcasecmp(file_type, "webp") == 0) return "image/webp";
        if (strcasecmp(file_type, "bmp") == 0) return "image/bmp";
    }
    return "application/octet-stream";
}
