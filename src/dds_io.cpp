// SPDX-License-Identifier: MIT
#include "dds_io.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

#include "gstextconv/gstextconv.hpp"

namespace gstextconv::ddsio {

namespace {

// ---------------------------------------------------------------------------
// DDS header layout (Microsoft docs: `DDS_HEADER`, `DDS_HEADER_DXT10`).
// ---------------------------------------------------------------------------

constexpr std::uint32_t kMagic = 0x20534444u;  // 'DDS '

// dwFlags bits
constexpr std::uint32_t DDSD_CAPS         = 0x00000001;
constexpr std::uint32_t DDSD_MIPMAPCOUNT  = 0x00020000;

// ddpfPixelFormat.dwFlags bits
constexpr std::uint32_t DDPF_ALPHAPIXELS  = 0x01;
constexpr std::uint32_t DDPF_ALPHA        = 0x02;
constexpr std::uint32_t DDPF_FOURCC       = 0x04;
constexpr std::uint32_t DDPF_RGB          = 0x40;
constexpr std::uint32_t DDPF_LUMINANCE    = 0x00020000;

// dwCaps2 bits
constexpr std::uint32_t DDSCAPS2_CUBEMAP  = 0x00000200;

// DX10 resource dimension
constexpr std::uint32_t DIMENSION_TEXTURE2D = 3;

// Misc flag 4 in the DXT10 header marks the surface as a cubemap.
constexpr std::uint32_t DDS_RESOURCE_MISC_TEXTURECUBE = 0x4;

std::uint32_t rd32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) <<  8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

struct PixelFormat {
    std::uint32_t flags = 0;
    std::uint32_t fourcc = 0;
    std::uint32_t rgb_bits = 0;
    std::uint32_t r_mask = 0;
    std::uint32_t g_mask = 0;
    std::uint32_t b_mask = 0;
    std::uint32_t a_mask = 0;
};

struct Header {
    std::uint32_t flags = 0;
    std::uint32_t height = 0;
    std::uint32_t width = 0;
    std::uint32_t mip_count = 0;
    std::uint32_t caps2 = 0;
    PixelFormat pf;
};

// ---------------------------------------------------------------------------
// Mip size helpers
// ---------------------------------------------------------------------------

std::size_t compute_uncompressed_size(std::uint32_t bits, int w, int h) {
    return static_cast<std::size_t>((static_cast<std::uint64_t>(bits) * w * h + 7) / 8);
}

std::size_t compute_block_size(int w, int h, std::size_t bytes_per_block) {
    const int bw = std::max(1, (w + 3) / 4);
    const int bh = std::max(1, (h + 3) / 4);
    return static_cast<std::size_t>(bw) * bh * bytes_per_block;
}

// ---------------------------------------------------------------------------
// BC1 / BC3 / BC4 block decoders
// ---------------------------------------------------------------------------

void rgb565_to_rgb(std::uint16_t c, std::uint8_t& r, std::uint8_t& g,
                   std::uint8_t& b) noexcept {
    r = static_cast<std::uint8_t>(((c >> 11) & 0x1f) * 255 / 31);
    g = static_cast<std::uint8_t>(((c >>  5) & 0x3f) * 255 / 63);
    b = static_cast<std::uint8_t>((c        & 0x1f) * 255 / 31);
}

// Decode a single BC1 block into a 4x4 RGBA8 tile (row-major, 16 pixels).
void decode_bc1_block(const std::uint8_t* src, std::uint8_t* dst16) {
    const std::uint16_t c0 = static_cast<std::uint16_t>(src[0] | (src[1] << 8));
    const std::uint16_t c1 = static_cast<std::uint16_t>(src[2] | (src[3] << 8));
    const std::uint32_t codes =
        static_cast<std::uint32_t>(src[4]) |
        (static_cast<std::uint32_t>(src[5]) <<  8) |
        (static_cast<std::uint32_t>(src[6]) << 16) |
        (static_cast<std::uint32_t>(src[7]) << 24);

    std::array<std::array<std::uint8_t, 4>, 4> palette{};
    rgb565_to_rgb(c0, palette[0][0], palette[0][1], palette[0][2]);
    palette[0][3] = 255;
    rgb565_to_rgb(c1, palette[1][0], palette[1][1], palette[1][2]);
    palette[1][3] = 255;

    if (c0 > c1) {
        for (int k = 0; k < 3; ++k) {
            palette[2][k] = static_cast<std::uint8_t>((2 * palette[0][k] + palette[1][k]) / 3);
            palette[3][k] = static_cast<std::uint8_t>((palette[0][k] + 2 * palette[1][k]) / 3);
        }
        palette[2][3] = palette[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) {
            palette[2][k] = static_cast<std::uint8_t>((palette[0][k] + palette[1][k]) / 2);
            palette[3][k] = 0;
        }
        palette[2][3] = 255;
        palette[3][3] = 0;
    }

    for (int i = 0; i < 16; ++i) {
        const int code = (codes >> (i * 2)) & 0x3;
        std::memcpy(dst16 + i * 4, palette[code].data(), 4);
    }
}

// Expand a single BC4/BC3-alpha block into 16 alpha values.
void decode_bc4_block(const std::uint8_t* src, std::uint8_t out_alpha[16]) {
    const std::uint8_t a0 = src[0];
    const std::uint8_t a1 = src[1];
    std::array<std::uint8_t, 8> alphas{};
    alphas[0] = a0;
    alphas[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i) {
            alphas[1 + i] = static_cast<std::uint8_t>(((7 - i) * a0 + i * a1) / 7);
        }
    } else {
        for (int i = 1; i <= 4; ++i) {
            alphas[1 + i] = static_cast<std::uint8_t>(((5 - i) * a0 + i * a1) / 5);
        }
        alphas[6] = 0;
        alphas[7] = 255;
    }

    std::uint64_t codes = 0;
    for (int i = 0; i < 6; ++i) {
        codes |= static_cast<std::uint64_t>(src[2 + i]) << (i * 8);
    }
    for (int i = 0; i < 16; ++i) {
        out_alpha[i] = alphas[(codes >> (i * 3)) & 0x7];
    }
}

// Write 16 decoded pixels into a destination RGBA image at (px, py).
void blit_tile(std::uint8_t* dst, int w, int h, int px, int py,
               const std::uint8_t* tile16) {
    for (int ty = 0; ty < 4; ++ty) {
        const int y = py + ty;
        if (y >= h) break;
        for (int tx = 0; tx < 4; ++tx) {
            const int x = px + tx;
            if (x >= w) break;
            std::memcpy(dst + (y * w + x) * 4, tile16 + (ty * 4 + tx) * 4, 4);
        }
    }
}

std::vector<std::uint8_t> decode_bc1(const std::uint8_t* src, std::size_t size,
                                     int w, int h) {
    const std::size_t expected = compute_block_size(w, h, 8);
    if (size < expected) {
        throw Error(Error::Code::InvalidFile, "dds: BC1 payload truncated");
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4, 0);
    const int bw = std::max(1, (w + 3) / 4);
    const int bh = std::max(1, (h + 3) / 4);
    std::uint8_t tile[16 * 4];
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            decode_bc1_block(src + (by * bw + bx) * 8, tile);
            blit_tile(out.data(), w, h, bx * 4, by * 4, tile);
        }
    }
    return out;
}

std::vector<std::uint8_t> decode_bc3(const std::uint8_t* src, std::size_t size,
                                     int w, int h) {
    const std::size_t expected = compute_block_size(w, h, 16);
    if (size < expected) {
        throw Error(Error::Code::InvalidFile, "dds: BC3 payload truncated");
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4, 0);
    const int bw = std::max(1, (w + 3) / 4);
    const int bh = std::max(1, (h + 3) / 4);
    std::uint8_t tile[16 * 4];
    std::uint8_t alpha[16];
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const std::uint8_t* block = src + (by * bw + bx) * 16;
            decode_bc4_block(block, alpha);
            decode_bc1_block(block + 8, tile);
            for (int i = 0; i < 16; ++i) tile[i * 4 + 3] = alpha[i];
            blit_tile(out.data(), w, h, bx * 4, by * 4, tile);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Uncompressed RGB/BGR/RGBA decode
// ---------------------------------------------------------------------------

// Convert an arbitrary channel mask layout to RGBA8 bytes.
std::vector<std::uint8_t> decode_masked(const std::uint8_t* src, std::size_t size,
                                        int w, int h, const PixelFormat& pf) {
    const int bytes_pp = static_cast<int>((pf.rgb_bits + 7) / 8);
    if (bytes_pp < 1 || bytes_pp > 4) {
        throw Error(Error::Code::UnsupportedFormat,
                    "dds: unsupported bit depth " +
                    std::to_string(pf.rgb_bits));
    }
    const std::size_t expected = static_cast<std::size_t>(bytes_pp) * w * h;
    if (size < expected) {
        throw Error(Error::Code::InvalidFile, "dds: pixel payload truncated");
    }

    auto mask_to_shift_scale = [](std::uint32_t mask, int& shift, int& scale_num) {
        shift = 0;
        if (mask == 0) { scale_num = 0; return; }
        while (((mask >> shift) & 1u) == 0) ++shift;
        std::uint32_t m = mask >> shift;
        int bits = 0;
        while (m & 1u) { ++bits; m >>= 1; }
        scale_num = bits > 0 ? (1 << bits) - 1 : 1;
    };
    int rs, gs, bs, as;
    int rn, gn, bn, an;
    mask_to_shift_scale(pf.r_mask, rs, rn);
    mask_to_shift_scale(pf.g_mask, gs, gn);
    mask_to_shift_scale(pf.b_mask, bs, bn);
    mask_to_shift_scale(pf.a_mask, as, an);

    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint32_t pix = 0;
            std::memcpy(&pix, src + (static_cast<std::size_t>(y) * w + x) * bytes_pp,
                        static_cast<std::size_t>(bytes_pp));
            auto ext = [&](std::uint32_t mask, int sh, int denom) -> std::uint8_t {
                if (mask == 0 || denom == 0) return 0;
                std::uint32_t v = (pix & mask) >> sh;
                return static_cast<std::uint8_t>((v * 255u) / static_cast<std::uint32_t>(denom));
            };
            std::uint8_t* dst = out.data() + (y * w + x) * 4;
            dst[0] = ext(pf.r_mask, rs, rn);
            dst[1] = ext(pf.g_mask, gs, gn);
            dst[2] = ext(pf.b_mask, bs, bn);
            dst[3] = pf.a_mask ? ext(pf.a_mask, as, an) : 255;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Format dispatch
// ---------------------------------------------------------------------------

// Per-mip decoded output plus the number of source bytes consumed.
struct DecodeOut {
    std::vector<std::uint8_t> rgba;
    std::size_t consumed = 0;
};

enum class DecodeKind {
    Masked,   // uncompressed RGBA with channel masks (from the DX9 header)
    BC1,
    BC3,
    BC4,      // single-channel; alpha channel is duplicated to RGB
    Rgba8,    // explicit R8G8B8A8
    Bgra8,    // explicit B8G8R8A8
    Rgbx8,    // 32-bit RGB, ignore 4th byte
};

struct FormatInfo {
    DecodeKind kind = DecodeKind::Masked;
    bool is_compressed = false;
    std::uint32_t bits = 32;  // used for masked/uncompressed sizing
};

FormatInfo determine_format(const PixelFormat& pf, std::uint32_t dxgi_format) {
    FormatInfo f;
    if (dxgi_format != 0) {
        switch (dxgi_format) {
            case 28: case 29:  // R8G8B8A8_UNORM / _SRGB
                f.kind = DecodeKind::Rgba8; f.bits = 32; return f;
            case 87: case 91:  // B8G8R8A8_UNORM / _SRGB
                f.kind = DecodeKind::Bgra8; f.bits = 32; return f;
            case 88:           // B8G8R8X8_UNORM
                f.kind = DecodeKind::Rgbx8; f.bits = 32; return f;
            case 71: case 72:  // BC1_UNORM / _SRGB
                f.kind = DecodeKind::BC1; f.is_compressed = true; return f;
            case 77: case 78:  // BC3_UNORM / _SRGB
                f.kind = DecodeKind::BC3; f.is_compressed = true; return f;
            case 80: case 81:  // BC4_UNORM / _SNORM
                f.kind = DecodeKind::BC4; f.is_compressed = true; return f;
            default:
                throw Error(Error::Code::UnsupportedFormat,
                            "dds: unsupported DXGI format " +
                            std::to_string(dxgi_format));
        }
    }

    if (pf.flags & DDPF_FOURCC) {
        switch (pf.fourcc) {
            case 0x31545844: /* DXT1 */ f.kind = DecodeKind::BC1; break;
            case 0x33545844: /* DXT3 */  // treated as BC3 (alpha is different
                                          // but keeps alpha channel present)
            case 0x35545844: /* DXT5 */ f.kind = DecodeKind::BC3; break;
            case 0x31495441: /* ATI1 */ f.kind = DecodeKind::BC4; break;
            default:
                throw Error(Error::Code::UnsupportedFormat,
                            "dds: unsupported FourCC");
        }
        f.is_compressed = true;
        return f;
    }

    if (pf.flags & (DDPF_RGB | DDPF_LUMINANCE | DDPF_ALPHA)) {
        f.kind = DecodeKind::Masked;
        f.bits = pf.rgb_bits > 0 ? pf.rgb_bits : 32;
        return f;
    }

    throw Error(Error::Code::UnsupportedFormat, "dds: unknown pixel format");
}

std::size_t mip_size(const FormatInfo& f, int w, int h) {
    switch (f.kind) {
        case DecodeKind::BC1:  return compute_block_size(w, h, 8);
        case DecodeKind::BC4:  return compute_block_size(w, h, 8);
        case DecodeKind::BC3:  return compute_block_size(w, h, 16);
        case DecodeKind::Rgba8:
        case DecodeKind::Bgra8:
        case DecodeKind::Rgbx8: return static_cast<std::size_t>(w) * h * 4;
        case DecodeKind::Masked: return compute_uncompressed_size(f.bits, w, h);
    }
    return 0;
}

Mip decode_mip(const std::uint8_t* src, std::size_t size,
               int w, int h, const FormatInfo& f, const PixelFormat& pf) {
    Mip m;
    m.width = w;
    m.height = h;
    switch (f.kind) {
        case DecodeKind::BC1: m.rgba = decode_bc1(src, size, w, h); break;
        case DecodeKind::BC3: m.rgba = decode_bc3(src, size, w, h); break;
        case DecodeKind::BC4: {
            const std::size_t expected = compute_block_size(w, h, 8);
            if (size < expected) {
                throw Error(Error::Code::InvalidFile,
                            "dds: BC4 payload truncated");
            }
            m.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
            const int bw = std::max(1, (w + 3) / 4);
            const int bh = std::max(1, (h + 3) / 4);
            std::uint8_t tile[16 * 4];
            std::uint8_t alpha[16];
            for (int by = 0; by < bh; ++by) {
                for (int bx = 0; bx < bw; ++bx) {
                    decode_bc4_block(src + (by * bw + bx) * 8, alpha);
                    for (int i = 0; i < 16; ++i) {
                        const std::uint8_t v = alpha[i];
                        tile[i * 4 + 0] = v;
                        tile[i * 4 + 1] = v;
                        tile[i * 4 + 2] = v;
                        tile[i * 4 + 3] = 255;
                    }
                    blit_tile(m.rgba.data(), w, h, bx * 4, by * 4, tile);
                }
            }
            break;
        }
        case DecodeKind::Rgba8: {
            const std::size_t expected = static_cast<std::size_t>(w) * h * 4;
            if (size < expected) {
                throw Error(Error::Code::InvalidFile,
                            "dds: RGBA8 payload truncated");
            }
            m.rgba.assign(src, src + expected);
            break;
        }
        case DecodeKind::Bgra8: {
            const std::size_t expected = static_cast<std::size_t>(w) * h * 4;
            if (size < expected) {
                throw Error(Error::Code::InvalidFile,
                            "dds: BGRA8 payload truncated");
            }
            m.rgba.resize(expected);
            for (std::size_t i = 0; i < expected; i += 4) {
                m.rgba[i + 0] = src[i + 2];
                m.rgba[i + 1] = src[i + 1];
                m.rgba[i + 2] = src[i + 0];
                m.rgba[i + 3] = src[i + 3];
            }
            break;
        }
        case DecodeKind::Rgbx8: {
            const std::size_t expected = static_cast<std::size_t>(w) * h * 4;
            if (size < expected) {
                throw Error(Error::Code::InvalidFile,
                            "dds: RGBX8 payload truncated");
            }
            m.rgba.resize(expected);
            for (std::size_t i = 0; i < expected; i += 4) {
                m.rgba[i + 0] = src[i + 2];
                m.rgba[i + 1] = src[i + 1];
                m.rgba[i + 2] = src[i + 0];
                m.rgba[i + 3] = 255;
            }
            break;
        }
        case DecodeKind::Masked:
            m.rgba = decode_masked(src, size, w, h, pf);
            break;
    }
    return m;
}

}  // namespace

bool looks_like_dds(const std::uint8_t* data, std::size_t size) noexcept {
    return data && size >= 128 && rd32(data) == kMagic;
}

LoadedDDS decode_dds(const std::uint8_t* data, std::size_t size) {
    if (!looks_like_dds(data, size)) {
        throw Error(Error::Code::InvalidFile, "dds: invalid magic");
    }
    const std::uint8_t* p = data + 4;        // past magic
    const std::uint32_t hdr_size = rd32(p);  // should be 124
    if (hdr_size != 124) {
        throw Error(Error::Code::InvalidFile, "dds: bad header size");
    }
    Header h;
    h.flags     = rd32(p + 4);
    h.height    = rd32(p + 8);
    h.width     = rd32(p + 12);
    h.mip_count = rd32(p + 24);
    PixelFormat& pf = h.pf;
    const std::uint8_t* ppf = p + 72;  // ddpfPixelFormat offset within DDS_HEADER
    pf.flags    = rd32(ppf + 4);
    pf.fourcc   = rd32(ppf + 8);
    pf.rgb_bits = rd32(ppf + 12);
    pf.r_mask   = rd32(ppf + 16);
    pf.g_mask   = rd32(ppf + 20);
    pf.b_mask   = rd32(ppf + 24);
    pf.a_mask   = rd32(ppf + 28);
    h.caps2     = rd32(p + 104);

    std::size_t payload_off = 128;
    std::uint32_t dxgi_format = 0;
    std::uint32_t array_size = 1;
    bool is_cubemap = (h.caps2 & DDSCAPS2_CUBEMAP) != 0;

    const bool has_dx10 =
        (pf.flags & DDPF_FOURCC) != 0 &&
        pf.fourcc == 0x30315844u;  // 'DX10'
    if (has_dx10) {
        if (size < 148) {
            throw Error(Error::Code::InvalidFile, "dds: DX10 header truncated");
        }
        const std::uint8_t* dx10 = data + 128;
        dxgi_format = rd32(dx10 + 0);
        // resource dim / misc flag / array size
        const std::uint32_t misc_flag = rd32(dx10 + 8);
        array_size = rd32(dx10 + 12);
        if (misc_flag & DDS_RESOURCE_MISC_TEXTURECUBE) is_cubemap = true;
        payload_off = 148;
    }

    const int width  = static_cast<int>(h.width);
    const int height = static_cast<int>(h.height);
    int mip_count = static_cast<int>(h.mip_count);
    if ((h.flags & DDSD_MIPMAPCOUNT) == 0 || mip_count <= 0) mip_count = 1;

    // A DX9 cubemap without DX10 metadata packs 6 face blocks sequentially.
    int face_count = 1;
    if (is_cubemap && !has_dx10) face_count = 6;

    const int total_slices = static_cast<int>(array_size) * face_count;

    FormatInfo finfo = determine_format(pf, dxgi_format);

    LoadedDDS out;
    out.width  = width;
    out.height = height;
    out.num_mipmaps = mip_count;
    out.num_layers  = total_slices;
    out.is_cubemap  = is_cubemap;
    out.is_array    = array_size > 1 || face_count > 1;
    out.has_alpha =
        (pf.flags & DDPF_ALPHAPIXELS) != 0 ||
        (finfo.kind == DecodeKind::Rgba8 || finfo.kind == DecodeKind::Bgra8 ||
         finfo.kind == DecodeKind::BC3);

    out.layers.resize(static_cast<std::size_t>(total_slices));
    const std::uint8_t* cursor = data + payload_off;
    const std::uint8_t* end    = data + size;

    for (int slice = 0; slice < total_slices; ++slice) {
        auto& layer = out.layers[slice];
        layer.mips.reserve(mip_count);
        int mw = width;
        int mh = height;
        for (int mip = 0; mip < mip_count; ++mip) {
            const std::size_t need = mip_size(finfo, mw, mh);
            if (cursor + need > end) {
                throw Error(Error::Code::InvalidFile,
                            "dds: payload smaller than header claims");
            }
            layer.mips.push_back(
                decode_mip(cursor, static_cast<std::size_t>(end - cursor),
                           mw, mh, finfo, pf));
            cursor += need;
            mw = std::max(1, mw / 2);
            mh = std::max(1, mh / 2);
        }
    }
    return out;
}

}  // namespace gstextconv::ddsio
