// SPDX-License-Identifier: MIT
// Public API implementation for gstextconv.

#include "gstextconv/gstextconv.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "astc_bridge.hpp"
#include "codec.hpp"
#include "color_format.hpp"
#include "container.hpp"
#include "flip.hpp"
#include "image_io.hpp"
#include "mipgen.hpp"

namespace gstextconv {

namespace {

[[noreturn]] void invalid(const std::string& msg) {
    throw Error(Error::Code::InvalidFile, msg);
}
[[noreturn]] void unsupported(const std::string& msg) {
    throw Error(Error::Code::UnsupportedFormat, msg);
}
[[noreturn]] void failed(const std::string& msg) {
    throw Error(Error::Code::ConversionFailed, msg);
}

bool looks_like_png(const std::uint8_t* d, std::size_t n) {
    return n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G';
}

bool looks_like_jpg(const std::uint8_t* d, std::size_t n) {
    return n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF;
}

bool looks_like_astc(const std::uint8_t* d, std::size_t n) {
    return n >= 16 && d[0] == 0x13 && d[1] == 0xAB && d[2] == 0xA1 && d[3] == 0x5C;
}

int resolve_num_mipmaps_request(int requested, int w, int h) {
    if (requested <= 0) {
        int levels = 1;
        int cw = w, ch = h;
        while ((cw > 1 || ch > 1) && levels < 16) {
            cw = std::max(1, cw / 2);
            ch = std::max(1, ch / 2);
            ++levels;
        }
        return levels;
    }
    return std::min(requested, 16);
}

std::uint32_t format_code_for_block(int bx, int by) {
    // Only ASTC 6x6 has a known empirical code in the FS23 table.
    if (bx == 6 && by == 6) return 34u;
    return 34u;  // default: treat unknown block sizes as 6x6 pending empirical data
}

}  // namespace

std::string_view library_version() noexcept { return "1.0.0"; }

std::string_view astcenc_version_string() noexcept { return "astcenc 5.3.0 (embedded)"; }

std::string_view build_release() noexcept {
#ifdef GSTEXTCONV_BUILD_RELEASE
    return GSTEXTCONV_BUILD_RELEASE;
#else
    return "release";
#endif
}
std::string_view build_date() noexcept {
#ifdef GSTEXTCONV_BUILD_DATE
    return GSTEXTCONV_BUILD_DATE;
#else
    return __DATE__ " " __TIME__;
#endif
}

Image decode(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 4) invalid("empty input");

    // Native formats first
    if (looks_like_png(data, size) || looks_like_jpg(data, size)) {
        auto src = imageio::decode_png_jpg(data, size);
        Image img;
        img.width = src.width;
        img.height = src.height;
        img.color_format = ColorFormat::RGBA32;
        img.color_space = ColorSpace::Srgb;
        img.compression = {0, 0};
        img.version = ContainerVersion::V6;
        img.origin = Origin::TopLeft;
        img.layers = {{MipLevel{src.width, src.height, std::move(src.rgba)}}};
        img.num_layers = 1;
        img.num_mipmaps = 1;
        return img;
    }

    if (looks_like_astc(data, size)) {
        const int bx = data[4];
        const int by = data[5];
        // Decode 24-bit little-endian xsize/ysize
        auto rd24 = [&](std::size_t off) {
            return static_cast<int>(data[off] |
                                    (data[off + 1] << 8) |
                                    (data[off + 2] << 16));
        };
        int w = rd24(7);
        int h = rd24(10);
        auto rgba = astc::decompress_to_rgba8(
            data + 16, size - 16, w, h, bx, by, ColorSpace::Srgb);
        Image img;
        img.width = w; img.height = h;
        img.color_format = ColorFormat::RGBA32;
        img.compression = {bx, by};
        img.version = ContainerVersion::V6;
        img.layers = {{MipLevel{w, h, std::move(rgba)}}};
        return img;
    }

    // GS2D container
    if (!container::is_gs2d(data, size)) {
        // JPG-renamed-to-ast fallback (FS20 quirk)
        if (looks_like_jpg(data, size)) {
            return decode(data, size);
        }
        invalid("unsupported container (no GS2D/PNG/JPG magic)");
    }

    auto dec = codec::decode_container(data, size);
    Image img;
    img.width = dec.header.dim_x;
    img.height = dec.header.dim_y;
    img.color_format = ColorFormat::RGBA32;
    img.color_space = ColorSpace::Srgb;
    img.version = (dec.header.version >= 6) ? ContainerVersion::V6
                                             : ContainerVersion::V3;
    img.origin = dec.header.flip_mode ? Origin::BottomLeft : Origin::TopLeft;
    img.num_layers = std::max<int>(1, dec.header.field_count);
    img.num_mipmaps = static_cast<int>(dec.mips.size());
    img.compression = {dec.block_x, dec.block_y};

    img.layers.resize(1);
    img.layers[0].reserve(dec.mips.size());
    ColorFormat src_fmt = ColorFormat::RGBA32;
    if (!dec.is_astc) {
        switch (dec.bytes_per_pixel) {
            case 1:  src_fmt = ColorFormat::R8; break;
            case 2:  src_fmt = ColorFormat::RG16; break;
            case 3:  src_fmt = ColorFormat::RGB24; break;
            case 4:  src_fmt = ColorFormat::RGBA32; break;
            case 8:  src_fmt = ColorFormat::RGBA64F; break;
            case 16: src_fmt = ColorFormat::RGBA128F; break;
            default: src_fmt = ColorFormat::RGBA32; break;
        }
        img.color_format = src_fmt;
    }
    for (const auto& m : dec.mips) {
        if (m.data.empty()) continue;
        MipLevel lvl;
        lvl.width = m.width;
        lvl.height = m.height;
        if (dec.is_astc) {
            lvl.data = astc::decompress_to_rgba8(
                m.data.data(), m.data.size(),
                m.width, m.height, dec.block_x, dec.block_y,
                ColorSpace::Srgb);
        } else {
            const std::size_t need =
                static_cast<std::size_t>(m.width) * m.height *
                cfmt::bytes_per_pixel(src_fmt);
            if (m.data.size() >= need) {
                lvl.data = cfmt::to_rgba8(
                    m.data.data(),
                    static_cast<std::size_t>(m.width),
                    static_cast<std::size_t>(m.height),
                    src_fmt);
            } else {
                lvl.data.assign(
                    static_cast<std::size_t>(m.width) * m.height * 4, 0);
            }
        }
        img.layers[0].push_back(std::move(lvl));
    }
    // The layer data is RGBA8 after the loop.
    img.color_format = ColorFormat::RGBA32;
    img.num_mipmaps = static_cast<int>(img.layers[0].size());

    return img;
}

Image load(const std::uint8_t* data, std::size_t size) {
    if (!container::is_gs2d(data, size)) {
        invalid("loader expects a GS2D container");
    }
    auto dec = codec::decode_container(data, size);
    Image img;
    img.width = dec.header.dim_x;
    img.height = dec.header.dim_y;
    img.color_format = ColorFormat::RGBA32;
    img.color_space = ColorSpace::Srgb;
    img.version = (dec.header.version >= 6) ? ContainerVersion::V6 : ContainerVersion::V3;
    img.origin = dec.header.flip_mode ? Origin::BottomLeft : Origin::TopLeft;
    img.num_layers = std::max<int>(1, dec.header.field_count);
    img.num_mipmaps = static_cast<int>(dec.mips.size());
    img.compression = {dec.block_x, dec.block_y};
    img.layers.resize(1);
    for (const auto& m : dec.mips) {
        img.layers[0].push_back(MipLevel{m.width, m.height, m.data});
    }
    return img;
}

std::vector<std::uint8_t> encode(const Image& src, const EncodeOptions& opts) {
    if (src.layers.empty() || src.layers[0].empty()) {
        invalid("empty image");
    }
    // Convert base mip (mip 0) to RGBA8 if needed.
    const auto& base = src.layers[0][0];
    std::vector<std::uint8_t> rgba = base.data;
    int w = base.width;
    int h = base.height;

    // Apply resize (nearest) if requested.
    if (opts.resize) {
        const int nw = opts.resize->first;
        const int nh = opts.resize->second;
        if (nw > 0 && nh > 0 && (nw != w || nh != h)) {
            std::vector<std::uint8_t> dst(static_cast<std::size_t>(nw) * nh * 4);
            for (int y = 0; y < nh; ++y) {
                const int sy = std::min(h - 1, (y * h) / nh);
                for (int x = 0; x < nw; ++x) {
                    const int sx = std::min(w - 1, (x * w) / nw);
                    std::memcpy(&dst[(y * nw + x) * 4], &rgba[(sy * w + sx) * 4], 4);
                }
            }
            rgba = std::move(dst);
            w = nw;
            h = nh;
        }
    }

    // Flip vertically if the target requires bottomLeft but source is topLeft.
    std::uint32_t flip_mode = 0;
    if (opts.ideal_origin == Origin::BottomLeft) {
        flipops::flip_vertical(rgba.data(), w, h, 4);
        flip_mode = 1;
    }

    // Build mip chain.
    int num_mips = resolve_num_mipmaps_request(opts.mipmaps, w, h);
    std::vector<std::pair<int, int>> mip_sizes;
    auto mip_rgba = mip::build_chain_rgba8(rgba.data(), w, h, num_mips, mip_sizes);

    // ASTC compress every mip level.
    std::vector<std::vector<std::uint8_t>> mip_blocks;
    mip_blocks.reserve(mip_rgba.size());
    for (std::size_t i = 0; i < mip_rgba.size(); ++i) {
        auto [mw, mh] = mip_sizes[i];
        auto blocks = astc::compress_rgba8(
            mip_rgba[i].data(), mw, mh,
            opts.block_size.x, opts.block_size.y,
            opts.quality, opts.color_space);
        mip_blocks.push_back(std::move(blocks));
    }

    const int mip_count_total = static_cast<int>(mip_blocks.size());
    const std::uint32_t fmt_code = format_code_for_block(opts.block_size.x, opts.block_size.y);

    if (opts.target_game == TargetGame::FS20) {
        return codec::encode_container_v4(mip_blocks, w, h);
    }
    return codec::encode_container_v6(
        mip_blocks, w, h, /*num_layers=*/1,
        opts.block_size.x, opts.block_size.y,
        fmt_code, flip_mode, mip_count_total);
}

std::vector<std::uint8_t> encode_many(
    const std::vector<Image>& inputs,
    const EncodeOptions& opts,
    std::vector<std::vector<std::uint8_t>>* extra_outputs) {
    if (inputs.empty()) invalid("no input images");
    if (opts.texture_type == TextureType::TwoD) {
        auto first = encode(inputs.front(), opts);
        if (extra_outputs) {
            for (std::size_t i = 1; i < inputs.size(); ++i) {
                extra_outputs->push_back(encode(inputs[i], opts));
            }
        }
        return first;
    }

    // Texture2DArray: stack layers (all must share dims).
    const int w = inputs.front().width;
    const int h = inputs.front().height;
    std::vector<std::vector<std::uint8_t>> combined_mips;  // mip-major, concatenated across layers
    int num_mips = resolve_num_mipmaps_request(opts.mipmaps, w, h);

    for (int mip = 0; mip < num_mips; ++mip) {
        combined_mips.emplace_back();
    }

    for (const auto& img : inputs) {
        if (img.width != w || img.height != h) {
            unsupported("2darray: all layers must share dimensions");
        }
        const auto& base = img.layers.at(0).at(0);
        std::vector<std::pair<int, int>> sizes;
        auto chain = mip::build_chain_rgba8(base.data.data(), w, h, num_mips, sizes);
        for (int mip = 0; mip < num_mips; ++mip) {
            auto blocks = astc::compress_rgba8(
                chain[mip].data(), sizes[mip].first, sizes[mip].second,
                opts.block_size.x, opts.block_size.y,
                opts.quality, opts.color_space);
            combined_mips[mip].insert(combined_mips[mip].end(),
                                      blocks.begin(), blocks.end());
        }
    }

    const std::uint32_t fmt_code = format_code_for_block(opts.block_size.x, opts.block_size.y);
    if (opts.target_game == TargetGame::FS20) {
        return codec::encode_container_v4(combined_mips, w, h);
    }
    return codec::encode_container_v6(
        combined_mips, w, h, static_cast<int>(inputs.size()),
        opts.block_size.x, opts.block_size.y,
        fmt_code, /*flip_mode=*/0, num_mips);
}

Image load_source_image(const std::uint8_t* data, std::size_t size,
                        int raw_width, int raw_height, ColorFormat raw_format) {
    if (data == nullptr || size == 0) invalid("empty source");

    if (looks_like_png(data, size) || looks_like_jpg(data, size)) {
        auto loaded = imageio::decode_png_jpg(data, size);
        Image img;
        img.width = loaded.width;
        img.height = loaded.height;
        img.color_format = ColorFormat::RGBA32;
        img.color_space = ColorSpace::Srgb;
        img.origin = Origin::TopLeft;
        img.num_layers = 1;
        img.num_mipmaps = 1;
        img.compression = {0, 0};
        img.layers = {{MipLevel{loaded.width, loaded.height, std::move(loaded.rgba)}}};
        return img;
    }

    if (raw_width <= 0 || raw_height <= 0) {
        unsupported("raw image requires raw_width/raw_height");
    }
    const std::size_t expected = static_cast<std::size_t>(raw_width) * raw_height *
                                  cfmt::bytes_per_pixel(raw_format);
    if (size < expected) {
        invalid("raw image: input size smaller than width*height*bpp");
    }
    auto rgba = cfmt::to_rgba8(data, raw_width, raw_height, raw_format);
    Image img;
    img.width = raw_width;
    img.height = raw_height;
    img.color_format = ColorFormat::RGBA32;
    img.color_space = ColorSpace::Srgb;
    img.layers = {{MipLevel{raw_width, raw_height, std::move(rgba)}}};
    img.num_layers = 1;
    img.num_mipmaps = 1;
    img.compression = {0, 0};
    return img;
}

std::vector<std::uint8_t> write_image(const Image& img, const DecodeWriteOptions& opts) {
    if (img.layers.empty()) invalid("empty image");
    const int layer_idx = std::min<int>(opts.layer_index, static_cast<int>(img.layers.size()) - 1);
    const auto& layer = img.layers[layer_idx];
    if (layer.empty()) invalid("layer without mip data");
    const int mip_idx = std::min<int>(opts.mip_index, static_cast<int>(layer.size()) - 1);
    const auto& src = layer[mip_idx];

    std::vector<std::uint8_t> rgba = src.data;
    int w = src.width;
    int h = src.height;

    if (opts.undo_flip && img.origin == Origin::BottomLeft) {
        flipops::flip_vertical(rgba.data(), w, h, 4);
    }

    // Apply channel swizzle. When channels == 4 we keep RGBA.
    std::size_t channel_count = 0;
    for (char c : opts.channels) {
        if (c == 'r' || c == 'g' || c == 'b' || c == 'a') ++channel_count;
    }
    if (channel_count == 0) channel_count = 4;
    auto swizzled = cfmt::swizzle_rgba8(rgba.data(), w, h, opts.channels.data(), channel_count);

    switch (opts.format) {
        case OutputFormat::PNG:
            return imageio::encode_png(swizzled.data(), w, h, static_cast<int>(channel_count));
        case OutputFormat::JPG:
            return imageio::encode_jpg(swizzled.data(), w, h,
                                       std::min<int>(3, static_cast<int>(channel_count)),
                                       opts.jpg_quality);
        case OutputFormat::RawRGBA:
            return rgba;
        case OutputFormat::ASTC: {
            const auto blocks = astc::compress_rgba8(
                rgba.data(), w, h,
                img.compression.x > 0 ? img.compression.x : 6,
                img.compression.y > 0 ? img.compression.y : 6,
                Quality::Fast, img.color_space);
            auto hdr = astc::build_astc_file_header(
                w, h, 1,
                img.compression.x > 0 ? img.compression.x : 6,
                img.compression.y > 0 ? img.compression.y : 6);
            hdr.insert(hdr.end(), blocks.begin(), blocks.end());
            return hdr;
        }
    }
    failed("unsupported output format");
}

}  // namespace gstextconv
