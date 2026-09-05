#include "dashscope_api.h"

#include <cassert>
#include <cstring>

int main() {
    assert(std::strcmp(image_mime_type("png"), "image/png") == 0);
    assert(std::strcmp(image_mime_type("JPG"), "image/jpeg") == 0);
    assert(std::strcmp(image_mime_type("jpeg"), "image/jpeg") == 0);
    assert(std::strcmp(image_mime_type("gif"), "image/gif") == 0);
    assert(std::strcmp(image_mime_type("WebP"), "image/webp") == 0);
    assert(std::strcmp(image_mime_type("bmp"), "image/bmp") == 0);
    assert(std::strcmp(image_mime_type("tiff"), "application/octet-stream") == 0);
    assert(std::strcmp(image_mime_type(nullptr), "application/octet-stream") == 0);
    return 0;
}
