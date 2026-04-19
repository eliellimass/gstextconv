// SPDX-License-Identifier: MIT
#include "image_io.hpp"

#include <cstring>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_STDIO
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

namespace gstextconv::imageio {

LoadedImage decode_png_jpg(const std::uint8_t* data, std::size_t size) {
    int w = 0, h = 0, n = 0;
    stbi_uc* pixels = stbi_load_from_memory(
        data, static_cast<int>(size), &w, &h, &n, 4);
    if (!pixels) {
        const char* err = stbi_failure_reason();
        throw std::runtime_error(std::string("stb_image failed: ") + (err ? err : "unknown"));
    }
    LoadedImage out;
    out.width = w;
    out.height = h;
    out.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return out;
}

namespace {
void stb_write_cb(void* ctx, void* data, int size) {
    auto* buf = static_cast<std::vector<std::uint8_t>*>(ctx);
    const auto* p = static_cast<const std::uint8_t*>(data);
    buf->insert(buf->end(), p, p + size);
}
}  // namespace

std::vector<std::uint8_t> encode_png(
    const std::uint8_t* pixels, int width, int height, int channels) {
    std::vector<std::uint8_t> buf;
    if (!stbi_write_png_to_func(stb_write_cb, &buf, width, height, channels,
                                pixels, width * channels)) {
        throw std::runtime_error("stbi_write_png_to_func failed");
    }
    return buf;
}

std::vector<std::uint8_t> encode_jpg(
    const std::uint8_t* pixels, int width, int height, int channels, int quality) {
    std::vector<std::uint8_t> buf;
    if (!stbi_write_jpg_to_func(stb_write_cb, &buf, width, height, channels,
                                pixels, quality)) {
        throw std::runtime_error("stbi_write_jpg_to_func failed");
    }
    return buf;
}

}  // namespace gstextconv::imageio
