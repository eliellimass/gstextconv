// SPDX-License-Identifier: MIT
// astcenc bridge implementation.

#include "astc_bridge.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <thread>

#include "astcenc.h"

namespace gstextconv::astc {

namespace {

astcenc_profile pick_profile(ColorSpace cs) {
    switch (cs) {
        case ColorSpace::Srgb:   return ASTCENC_PRF_LDR_SRGB;
        case ColorSpace::Linear: return ASTCENC_PRF_LDR;
        case ColorSpace::Alpha:  return ASTCENC_PRF_LDR;
    }
    return ASTCENC_PRF_LDR_SRGB;
}

float pick_quality(Quality q) {
    switch (q) {
        case Quality::Fast:     return ASTCENC_PRE_FAST;
        case Quality::Medium:   return ASTCENC_PRE_MEDIUM;
        case Quality::Thorough: return ASTCENC_PRE_THOROUGH;
    }
    return ASTCENC_PRE_FAST;
}

[[noreturn]] void throw_astc(const char* op, astcenc_error err) {
    throw std::runtime_error(std::string(op) + ": " + astcenc_get_error_string(err));
}

struct Context {
    astcenc_context* ctx = nullptr;
    ~Context() { if (ctx) astcenc_context_free(ctx); }
};

}  // namespace

std::vector<std::uint8_t> compress_rgba8(
    const std::uint8_t* rgba, int width, int height,
    int block_x, int block_y, Quality quality, ColorSpace color_space) {

    astcenc_config config{};
    astcenc_error err = astcenc_config_init(
        pick_profile(color_space),
        static_cast<unsigned>(block_x),
        static_cast<unsigned>(block_y),
        1u,
        pick_quality(quality),
        ASTCENC_FLG_SELF_DECOMPRESS_ONLY |
            (color_space == ColorSpace::Alpha ? ASTCENC_FLG_USE_ALPHA_WEIGHT : 0u),
        &config);
    if (err != ASTCENC_SUCCESS) throw_astc("astcenc_config_init", err);

    Context ctx;
    const unsigned threads = 1u;
    err = astcenc_context_alloc(&config, threads, &ctx.ctx);
    if (err != ASTCENC_SUCCESS) throw_astc("astcenc_context_alloc", err);

    astcenc_image image{};
    image.dim_x = static_cast<unsigned>(width);
    image.dim_y = static_cast<unsigned>(height);
    image.dim_z = 1;
    image.data_type = ASTCENC_TYPE_U8;
    void* slice = const_cast<std::uint8_t*>(rgba);
    void* slices[] = {slice};
    image.data = slices;

    astcenc_swizzle swz{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};

    const std::size_t blocks_x = (static_cast<std::size_t>(width) + block_x - 1) / block_x;
    const std::size_t blocks_y = (static_cast<std::size_t>(height) + block_y - 1) / block_y;
    const std::size_t out_size = blocks_x * blocks_y * 16;
    std::vector<std::uint8_t> out(out_size);

    err = astcenc_compress_image(ctx.ctx, &image, &swz, out.data(), out.size(), 0);
    if (err != ASTCENC_SUCCESS) throw_astc("astcenc_compress_image", err);
    return out;
}

std::vector<std::uint8_t> decompress_to_rgba8(
    const std::uint8_t* astc_blocks, std::size_t blocks_size,
    int width, int height, int block_x, int block_y, ColorSpace color_space) {

    astcenc_config config{};
    astcenc_error err = astcenc_config_init(
        pick_profile(color_space),
        static_cast<unsigned>(block_x),
        static_cast<unsigned>(block_y),
        1u,
        ASTCENC_PRE_FASTEST,
        ASTCENC_FLG_DECOMPRESS_ONLY,
        &config);
    if (err != ASTCENC_SUCCESS) throw_astc("astcenc_config_init(decode)", err);

    Context ctx;
    err = astcenc_context_alloc(&config, 1u, &ctx.ctx);
    if (err != ASTCENC_SUCCESS) throw_astc("astcenc_context_alloc(decode)", err);

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
    astcenc_image image{};
    image.dim_x = static_cast<unsigned>(width);
    image.dim_y = static_cast<unsigned>(height);
    image.dim_z = 1;
    image.data_type = ASTCENC_TYPE_U8;
    void* slice = rgba.data();
    void* slices[] = {slice};
    image.data = slices;

    astcenc_swizzle swz{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};

    err = astcenc_decompress_image(ctx.ctx, astc_blocks, blocks_size, &image, &swz, 0);
    if (err != ASTCENC_SUCCESS) throw_astc("astcenc_decompress_image", err);
    return rgba;
}

std::vector<std::uint8_t> build_astc_file_header(
    int width, int height, int depth, int block_x, int block_y) {
    std::vector<std::uint8_t> hdr(16, 0);
    hdr[0] = 0x13; hdr[1] = 0xAB; hdr[2] = 0xA1; hdr[3] = 0x5C;
    hdr[4] = static_cast<std::uint8_t>(block_x);
    hdr[5] = static_cast<std::uint8_t>(block_y);
    hdr[6] = static_cast<std::uint8_t>(depth > 0 ? 1 : 1);
    auto put24 = [&](int off, int v) {
        hdr[off + 0] = static_cast<std::uint8_t>(v & 0xFF);
        hdr[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        hdr[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    };
    put24(7, width);
    put24(10, height);
    put24(13, depth > 0 ? depth : 1);
    return hdr;
}

}  // namespace gstextconv::astc
