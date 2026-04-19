// SPDX-License-Identifier: MIT
// GS2D container parser and writer (v4 / v6).

#include "container.hpp"

#include <cstring>
#include <stdexcept>

#include "miniz.h"

namespace gstextconv::container {

namespace {

inline std::uint16_t rd_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
inline std::uint32_t rd_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(
        p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t{p[3]} << 24));
}
inline void wr_u16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}
inline void wr_u32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

inline std::size_t align16(std::size_t v) noexcept {
    return (v + 0x0F) & ~static_cast<std::size_t>(0x0F);
}

}  // namespace

bool is_gs2d(const std::uint8_t* data, std::size_t size) noexcept {
    if (size < 8) return false;
    return data[0] == 'G' && data[1] == 'S' && data[2] == '2' && data[3] == 'D';
}

std::uint32_t version_of(const std::uint8_t* data, std::size_t size) {
    if (!is_gs2d(data, size)) {
        throw std::runtime_error("not a GS2D container");
    }
    return rd_u32(data + 4);
}

std::size_t header_size_for_version(std::uint32_t version) noexcept {
    return version >= 6 ? kHeaderV6 : kHeaderV4;
}

Header parse_header(const std::uint8_t* data, std::size_t size) {
    if (size < 8) {
        throw std::runtime_error("file smaller than 8 bytes");
    }
    if (!is_gs2d(data, size)) {
        throw std::runtime_error("invalid GS2D magic");
    }
    Header h{};
    std::memcpy(h.magic.data(), data, 4);
    h.version = rd_u32(data + 4);
    const std::size_t hdr_size = header_size_for_version(h.version);
    if (size < hdr_size) {
        throw std::runtime_error("truncated GS2D header");
    }
    h.total_payload_size = rd_u32(data + 0x08);
    h.dim_x              = rd_u32(data + 0x0C);
    h.dim_y              = rd_u32(data + 0x10);
    h.dim_z              = rd_u32(data + 0x14);
    h.field_u16_a        = rd_u16(data + 0x18);
    h.mip_count          = rd_u16(data + 0x1A);
    h.field_count        = rd_u32(data + 0x1C);
    h.flags              = rd_u32(data + 0x20);
    for (int i = 0; i < 4; ++i) {
        h.seg_sizes[i] = rd_u32(data + 0x24 + i * 4);
    }
    if (h.version >= 6) {
        for (int i = 0; i < 4; ++i) {
            h.aux[i] = rd_u32(data + 0x34 + i * 4);
        }
        h.format_code_a = rd_u32(data + 0x44);
        h.flip_mode     = rd_u32(data + 0x48);
        h.format_code_b = rd_u32(data + 0x4C);
    } else {
        h.aux.fill(0);
        h.format_code_a = 0;
        h.flip_mode     = 0;
        h.format_code_b = 0;
        h.v4_field_0     = rd_u32(data + 0x24);
        h.v4_field_1     = rd_u32(data + 0x28);
        h.v4_field_2     = rd_u32(data + 0x2C);
        h.v4_field_3     = rd_u32(data + 0x30);
        h.v4_field_4     = rd_u32(data + 0x34);
        h.v4_payload_copy = rd_u32(data + 0x38);
    }
    return h;
}

SegmentLayout compute_segment_layout(const Header& h) {
    SegmentLayout s{};
    s.offsets[0] = header_size_for_version(h.version);
    for (int i = 0; i < 3; ++i) {
        s.offsets[i + 1] =
            s.offsets[i] + align16(static_cast<std::size_t>(h.seg_sizes[i]));
    }
    return s;
}

std::vector<std::uint8_t> zlib_decompress(const std::uint8_t* src,
                                          std::size_t src_size,
                                          std::size_t expected_size) {
    // If the segment is clearly not zlib-framed and the size matches the
    // expected raw size, copy through.
    const bool zlib_framed =
        src_size >= 2 && src[0] == 0x78 &&
        (src[1] == 0x01 || src[1] == 0x9C || src[1] == 0xDA ||
         src[1] == 0x5E || src[1] == 0x7D);
    if (!zlib_framed && expected_size == src_size) {
        return {src, src + src_size};
    }

    // Stream-decompress with a growing output buffer to handle segments whose
    // decompressed size is unknown or much larger than expected_size.
    mz_stream strm{};
    if (mz_inflateInit(&strm) != MZ_OK) {
        throw std::runtime_error("zlib init failed");
    }
    strm.next_in = src;
    strm.avail_in = static_cast<unsigned int>(src_size);

    std::vector<std::uint8_t> out;
    std::size_t cap = expected_size > 0 ? expected_size : src_size * 4;
    if (cap < 256) cap = 256;
    out.resize(cap);

    std::size_t total = 0;
    for (;;) {
        if (total == out.size()) {
            out.resize(out.size() * 2);
        }
        strm.next_out = out.data() + total;
        strm.avail_out = static_cast<unsigned int>(out.size() - total);
        int rc = mz_inflate(&strm, MZ_NO_FLUSH);
        total = out.size() - strm.avail_out;
        if (rc == MZ_STREAM_END) break;
        if (rc == MZ_OK) continue;
        if (rc == MZ_BUF_ERROR && strm.avail_in == 0) {
            // Input exhausted without reaching stream end.
            break;
        }
        mz_inflateEnd(&strm);
        throw std::runtime_error("zlib decompression failed");
    }
    mz_inflateEnd(&strm);
    out.resize(total);
    return out;
}

std::vector<std::uint8_t> zlib_compress(const std::uint8_t* src,
                                        std::size_t src_size,
                                        int level) {
    mz_ulong dst_bound = mz_compressBound(static_cast<mz_ulong>(src_size));
    std::vector<std::uint8_t> out(dst_bound);
    mz_ulong dst_len = dst_bound;
    int rc = mz_compress2(out.data(), &dst_len, src,
                          static_cast<mz_ulong>(src_size), level);
    if (rc != MZ_OK) {
        throw std::runtime_error("zlib compression failed");
    }
    out.resize(dst_len);
    return out;
}

std::vector<std::uint8_t> write_header(const Header& h) {
    const std::size_t hdr_size = header_size_for_version(h.version);
    std::vector<std::uint8_t> buf(hdr_size, 0);
    std::memcpy(buf.data(), h.magic.data(), 4);
    wr_u32(buf.data() + 0x04, h.version);
    wr_u32(buf.data() + 0x08, h.total_payload_size);
    wr_u32(buf.data() + 0x0C, h.dim_x);
    wr_u32(buf.data() + 0x10, h.dim_y);
    wr_u32(buf.data() + 0x14, h.dim_z);
    wr_u16(buf.data() + 0x18, h.field_u16_a);
    wr_u16(buf.data() + 0x1A, h.mip_count);
    wr_u32(buf.data() + 0x1C, h.field_count);
    wr_u32(buf.data() + 0x20, h.flags);
    for (int i = 0; i < 4; ++i) {
        wr_u32(buf.data() + 0x24 + i * 4, h.seg_sizes[i]);
    }
    if (h.version >= 6) {
        for (int i = 0; i < 4; ++i) {
            wr_u32(buf.data() + 0x34 + i * 4, h.aux[i]);
        }
        wr_u32(buf.data() + 0x44, h.format_code_a);
        wr_u32(buf.data() + 0x48, h.flip_mode);
        wr_u32(buf.data() + 0x4C, h.format_code_b);
    } else {
        wr_u32(buf.data() + 0x24, h.v4_field_0);
        wr_u32(buf.data() + 0x28, h.v4_field_1);
        wr_u32(buf.data() + 0x2C, h.v4_field_2);
        wr_u32(buf.data() + 0x30, h.v4_field_3);
        wr_u32(buf.data() + 0x34, h.v4_field_4);
        wr_u32(buf.data() + 0x38, h.v4_payload_copy);
    }
    return buf;
}

}  // namespace gstextconv::container
