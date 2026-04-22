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
#include "dds_io.hpp"
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
    // Semantics (aligned with cli.md and the mipmaps field docs):
    //   requested <  0  -> "max": build the full mip chain down to 1x1.
    //   requested == 0  -> only the base mip level (no additional mipmaps).
    //   requested >  0  -> `requested` additional mipmaps on top of the base,
    //                      i.e. `requested + 1` total mip levels.
    // The returned value is the TOTAL number of mip levels including the base.
    if (requested < 0) {
        int levels = 1;
        int cw = w, ch = h;
        while ((cw > 1 || ch > 1) && levels < 16) {
            cw = std::max(1, cw / 2);
            ch = std::max(1, ch / 2);
            ++levels;
        }
        return levels;
    }
    return std::min(requested + 1, 16);
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
    const int num_layers = std::max<int>(1, dec.num_layers);
    img.num_layers = num_layers;
    img.num_mipmaps = static_cast<int>(dec.mips.size());
    img.type = num_layers > 1 ? TextureType::TwoDArray : TextureType::TwoD;
    img.compression = {dec.block_x, dec.block_y};

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

    img.layers.assign(static_cast<std::size_t>(num_layers), {});
    for (int L = 0; L < num_layers; ++L) {
        img.layers[L].reserve(dec.mips.size());
        for (std::size_t m = 0; m < dec.mips.size(); ++m) {
            const auto& shape = dec.mips[m];
            // Prefer the per-layer buffer the codec filled in; fall back to
            // the legacy shared buffer so single-layer textures still decode.
            const std::vector<std::uint8_t>* raw = nullptr;
            if (L < static_cast<int>(dec.layer_data.size()) &&
                m < dec.layer_data[L].size() &&
                !dec.layer_data[L][m].empty()) {
                raw = &dec.layer_data[L][m];
            } else if (!shape.data.empty()) {
                raw = &shape.data;
            } else {
                continue;
            }
            MipLevel lvl;
            lvl.width  = shape.width;
            lvl.height = shape.height;
            if (dec.is_astc) {
                lvl.data = astc::decompress_to_rgba8(
                    raw->data(), raw->size(),
                    shape.width, shape.height, dec.block_x, dec.block_y,
                    ColorSpace::Srgb);
            } else {
                const std::size_t need =
                    static_cast<std::size_t>(shape.width) * shape.height *
                    cfmt::bytes_per_pixel(src_fmt);
                if (raw->size() >= need) {
                    lvl.data = cfmt::to_rgba8(
                        raw->data(),
                        static_cast<std::size_t>(shape.width),
                        static_cast<std::size_t>(shape.height),
                        src_fmt);
                } else {
                    lvl.data.assign(
                        static_cast<std::size_t>(shape.width) * shape.height * 4, 0);
                }
            }
            img.layers[L].push_back(std::move(lvl));
        }
    }
    // Layer data is RGBA8 after the decode loop.
    img.color_format = ColorFormat::RGBA32;
    img.num_mipmaps = img.layers.empty() ? 0 :
                      static_cast<int>(img.layers[0].size());

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
    const int num_layers = std::max<int>(1, dec.num_layers);
    img.num_layers = num_layers;
    img.num_mipmaps = static_cast<int>(dec.mips.size());
    img.type = num_layers > 1 ? TextureType::TwoDArray : TextureType::TwoD;
    img.compression = {dec.block_x, dec.block_y};
    img.layers.assign(static_cast<std::size_t>(num_layers), {});
    for (int L = 0; L < num_layers; ++L) {
        img.layers[L].reserve(dec.mips.size());
        for (std::size_t m = 0; m < dec.mips.size(); ++m) {
            const auto& shape = dec.mips[m];
            const std::vector<std::uint8_t>* raw = nullptr;
            if (L < static_cast<int>(dec.layer_data.size()) &&
                m < dec.layer_data[L].size() &&
                !dec.layer_data[L][m].empty()) {
                raw = &dec.layer_data[L][m];
            } else {
                raw = &shape.data;
            }
            img.layers[L].push_back(MipLevel{shape.width, shape.height, *raw});
        }
    }
    return img;
}

namespace {

// Build the RGBA8 mip chain for one layer, either by inheriting the chain that
// already lives inside `src_layer` (DDS case) or by downsampling from the base.
// Returns per-mip (width, height) pairs via `out_sizes` and per-mip RGBA bytes
// via the return value.
std::vector<std::vector<std::uint8_t>> build_layer_chain(
    const std::vector<MipLevel>& src_layer,
    int base_w, int base_h,
    int num_mips, bool inherit_mipmaps,
    bool flip, Origin src_origin, Origin dst_origin,
    std::vector<std::pair<int, int>>& out_sizes) {

    out_sizes.clear();
    std::vector<std::vector<std::uint8_t>> mips;

    auto maybe_flip = [&](std::vector<std::uint8_t>& px, int w, int h) {
        if (flip && src_origin != dst_origin) {
            flipops::flip_vertical(px.data(), w, h, 4);
        }
    };

    const bool inherit =
        inherit_mipmaps && src_layer.size() > 1 &&
        static_cast<int>(src_layer.size()) >= num_mips;

    if (inherit) {
        mips.reserve(static_cast<std::size_t>(num_mips));
        out_sizes.reserve(static_cast<std::size_t>(num_mips));
        for (int i = 0; i < num_mips; ++i) {
            const auto& m = src_layer[static_cast<std::size_t>(i)];
            std::vector<std::uint8_t> px = m.data;
            maybe_flip(px, m.width, m.height);
            mips.push_back(std::move(px));
            out_sizes.emplace_back(m.width, m.height);
        }
    } else {
        std::vector<std::uint8_t> base = src_layer.at(0).data;
        maybe_flip(base, base_w, base_h);
        mips = mip::build_chain_rgba8(base.data(), base_w, base_h, num_mips, out_sizes);
    }
    return mips;
}

// Nearest-neighbour resize of an RGBA8 layer's base mip.
void resize_base_inplace(std::vector<std::uint8_t>& rgba, int& w, int& h,
                         int nw, int nh) {
    if (nw <= 0 || nh <= 0 || (nw == w && nh == h)) return;
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

}  // namespace

std::vector<std::uint8_t> encode(const Image& src, const EncodeOptions& opts) {
    if (src.layers.empty() || src.layers[0].empty()) {
        invalid("empty image");
    }

    // 2DArray fast path: when the caller asks us to inherit layers and the
    // source image already packs every slice in `src.layers`, encode them all
    // here without forcing the caller to split and re-pass them.
    const bool treat_as_array =
        opts.inherit_layers && src.layers.size() > 1 &&
        opts.texture_type == TextureType::TwoDArray;

    const auto& base = src.layers[0][0];
    int w = base.width;
    int h = base.height;

    // Resize is only meaningful for the non-inherit path.
    const bool want_resize = opts.resize.has_value();
    if (want_resize) {
        w = opts.resize->first;
        h = opts.resize->second;
    }

    // Decide final mip count.
    int num_mips;
    const bool inherit_mipmaps =
        opts.inherit_mipmaps && !want_resize &&
        src.layers[0].size() > 1;
    if (inherit_mipmaps) {
        // Honour the source chain verbatim; callers expect "herdar mipmaps".
        num_mips = static_cast<int>(src.layers[0].size());
    } else {
        num_mips = resolve_num_mipmaps_request(opts.mipmaps, w, h);
    }

    const bool flip = opts.ideal_origin != src.origin;
    const std::uint32_t flip_mode = (opts.ideal_origin == Origin::BottomLeft) ? 1u : 0u;

    const int num_layers = treat_as_array ? static_cast<int>(src.layers.size()) : 1;

    std::vector<std::vector<std::uint8_t>> combined_mips(
        static_cast<std::size_t>(num_mips));

    for (int L = 0; L < num_layers; ++L) {
        const auto& src_layer = src.layers[static_cast<std::size_t>(L)];

        // Optional resize of the base when caller requests it.
        std::vector<MipLevel> resized_layer;
        const std::vector<MipLevel>* layer_ptr = &src_layer;
        if (want_resize && !inherit_mipmaps) {
            resized_layer.push_back(src_layer.at(0));
            resize_base_inplace(resized_layer[0].data,
                                resized_layer[0].width, resized_layer[0].height,
                                opts.resize->first, opts.resize->second);
            layer_ptr = &resized_layer;
        }

        std::vector<std::pair<int, int>> sizes;
        auto chain = build_layer_chain(
            *layer_ptr, w, h, num_mips, inherit_mipmaps,
            flip, src.origin, opts.ideal_origin, sizes);

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
        combined_mips, w, h, num_layers,
        opts.block_size.x, opts.block_size.y,
        fmt_code, flip_mode, num_mips);
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

    if (ddsio::looks_like_dds(data, size)) {
        auto dds = ddsio::decode_dds(data, size);
        Image img;
        img.width  = dds.width;
        img.height = dds.height;
        img.color_format = ColorFormat::RGBA32;
        img.color_space  = ColorSpace::Srgb;
        img.origin       = Origin::TopLeft;           // DDS uses top-left by convention
        img.num_layers   = std::max(1, dds.num_layers);
        img.num_mipmaps  = std::max(1, dds.num_mipmaps);
        img.type = img.num_layers > 1 ? TextureType::TwoDArray : TextureType::TwoD;
        img.compression = {0, 0};
        img.layers.resize(static_cast<std::size_t>(img.num_layers));
        for (int L = 0; L < img.num_layers; ++L) {
            const auto& src_layer = dds.layers[L];
            auto& dst_layer = img.layers[L];
            dst_layer.reserve(src_layer.mips.size());
            for (const auto& m : src_layer.mips) {
                dst_layer.push_back(MipLevel{m.width, m.height, m.rgba});
            }
        }
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
