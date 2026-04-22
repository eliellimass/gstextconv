// SPDX-License-Identifier: MIT
// GS2D codec — orchestrates container I/O with astcenc and zlib segments.

#include "codec.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "astc_bridge.hpp"
#include "color_format.hpp"
#include "container.hpp"
#include "flip.hpp"
#include "mipgen.hpp"

namespace gstextconv::codec {

namespace {

constexpr std::size_t kAstcBlockBytes = 16;

inline std::size_t mip_dim(int base, int level) {
    int v = base >> level;
    return static_cast<std::size_t>(v > 0 ? v : 1);
}

inline std::size_t mip_raw_astc(std::size_t w, std::size_t h, int bx, int by) {
    return ((w + bx - 1) / bx) * ((h + by - 1) / by) * kAstcBlockBytes;
}

inline std::size_t mip_raw_uncompressed(std::size_t w, std::size_t h, int bpp) {
    return w * h * static_cast<std::size_t>(bpp);
}

int bpp_for_channels(int channels) {
    switch (channels) {
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        default: return 4;
    }
}

/// Valid 2D ASTC block shapes per the specification.
constexpr std::array<std::pair<int, int>, 14> kAstcBlocks = {{
    {4, 4}, {5, 4}, {5, 5}, {6, 5}, {6, 6}, {8, 5}, {8, 6},
    {8, 8}, {10, 5}, {10, 6}, {10, 8}, {10, 10}, {12, 10}, {12, 12},
}};

/// Try to infer ASTC block size + mip count for a v4 payload.
std::optional<std::pair<std::pair<int, int>, int>> infer_astc_v4(
    std::size_t dim_x, std::size_t dim_y, std::size_t payload_size) {
    for (auto [bx, by] : kAstcBlocks) {
        std::size_t total = 0;
        int n_mips = 0;
        std::size_t w = dim_x, h = dim_y;
        while (w >= 1 && h >= 1) {
            total += mip_raw_astc(w, h, bx, by);
            ++n_mips;
            if (total == payload_size) {
                return std::pair{std::pair{bx, by}, n_mips};
            }
            if (total > payload_size) break;
            if (w == 1 && h == 1) break;
            w = w > 1 ? w >> 1 : 1;
            h = h > 1 ? h >> 1 : 1;
        }
    }
    return std::nullopt;
}

std::optional<std::pair<int, int>> infer_astc_from_size(
    std::size_t dim_x, std::size_t dim_y, std::size_t payload_size) {
    for (auto [bx, by] : kAstcBlocks) {
        if (mip_raw_astc(dim_x, dim_y, bx, by) == payload_size) {
            return std::pair{bx, by};
        }
    }
    return std::nullopt;
}

bool fmt_is_astc(std::uint32_t format_code_a) {
    // Known codes that indicate ASTC vs raw formats. 1..5 are raw channel
    // counts; anything >= 6 in the v6 lookup table is ASTC.
    return format_code_a >= 6;
}

std::pair<int, int> astc_block_for_code(std::uint32_t format_code_a) {
    // Empirical mapping derived from the FS23 binary's texture format table.
    // `format_code_a == 34` is ASTC 6x6 (the common case).
    switch (format_code_a) {
        case 34: return {6, 6};
        default:
            // Heuristic fallback: treat any ASTC code as 6x6 so the decoder
            // can still retry with payload-size inference.
            return {6, 6};
    }
}

}  // namespace

DecodedContainer decode_container(const std::uint8_t* data, std::size_t size) {
    DecodedContainer out{};
    auto hdr = container::parse_header(data, size);
    out.header = hdr;

    const int total_mips = hdr.version >= 6
                            ? static_cast<int>(hdr.mip_count + 1)
                            : 0;  // inferred below for v4

    if (hdr.version >= 6) {
        auto layout = container::compute_segment_layout(hdr);
        const int num_segs = std::min(total_mips, 4);

        // Determine block size / bpp from header.
        std::pair<int, int> block = {0, 0};
        int bpp = 0;
        bool is_astc = fmt_is_astc(hdr.format_code_a);
        if (is_astc) {
            block = astc_block_for_code(hdr.format_code_a);
        } else {
            bpp = bpp_for_channels(hdr.field_u16_a);
        }

        out.block_x = block.first;
        out.block_y = block.second;
        out.is_astc = is_astc;
        out.bytes_per_pixel = bpp;

        std::vector<Mip> mips(total_mips);
        for (int mip = 0; mip < total_mips; ++mip) {
            mips[mip].width  = static_cast<int>(mip_dim(hdr.dim_x, mip));
            mips[mip].height = static_cast<int>(mip_dim(hdr.dim_y, mip));
        }

        // Each v6 segment packs every array slice for one mip level as
        // `num_layers` contiguous single-layer buffers. The header's
        // `field_count` carries the slice count; fall back to 1 when the
        // value is obviously bogus.
        int num_layers = static_cast<int>(hdr.field_count);
        if (num_layers < 1 || num_layers > 4096) num_layers = 1;

        // per_layer_seg_bytes[mip_level] tracks the single-layer slice size
        // we commit to when splitting the decompressed segment; segments we
        // haven't touched yet stay at 0.
        std::vector<std::size_t> per_layer_seg_bytes(total_mips, 0);
        std::vector<std::vector<std::uint8_t>> full_seg(total_mips);

        // For textures with more than 4 mip levels, segment 3 aggregates the
        // remaining levels (base mip + tail). The current decoder surfaces
        // only the last four segments as distinct buffers; any extra mip
        // levels will share segment 3's decompressed buffer.
        for (int i = 0; i < num_segs; ++i) {
            if (hdr.seg_sizes[i] == 0) continue;
            const std::size_t off = layout.offsets[i];
            const std::size_t sz  = hdr.seg_sizes[i];
            if (off + sz > size) {
                throw std::runtime_error("v6: segment overruns file");
            }

            const int mip_level = (num_segs - 1) - i;
            const std::size_t w = mip_dim(hdr.dim_x, mip_level);
            const std::size_t h = mip_dim(hdr.dim_y, mip_level);
            const std::size_t expected_raw =
                is_astc ? mip_raw_astc(w, h, block.first, block.second)
                        : mip_raw_uncompressed(w, h, bpp);
            // Decompress assuming `num_layers` copies so zlib has a sane hint.
            const std::size_t expected_all =
                expected_raw * static_cast<std::size_t>(num_layers);

            std::vector<std::uint8_t> seg_data;
            const std::uint8_t* src = data + off;
            const bool zlib_framed =
                sz >= 2 && src[0] == 0x78 &&
                (src[1] == 0x01 || src[1] == 0x9C || src[1] == 0xDA ||
                 src[1] == 0x5E || src[1] == 0x7D);
            if (zlib_framed) {
                seg_data = container::zlib_decompress(
                    src, sz, expected_all > 0 ? expected_all : expected_raw);
            } else {
                seg_data.assign(src, src + sz);
            }

            // Pick the slice size for this mip. When the segment is exactly
            // `num_layers * expected_raw` we trust the header's layer count;
            // otherwise we fall back to whatever divides the segment evenly
            // (keeps single-layer textures working when field_count is stale).
            std::size_t per_layer = expected_raw;
            if (expected_raw > 0 && seg_data.size() > 0 &&
                seg_data.size() % expected_raw == 0) {
                per_layer = expected_raw;
            } else if (num_layers > 0 && seg_data.size() > 0 &&
                       seg_data.size() % num_layers == 0) {
                per_layer = seg_data.size() / num_layers;
            } else {
                per_layer = seg_data.size();
            }

            per_layer_seg_bytes[mip_level] = per_layer;
            full_seg[mip_level] = std::move(seg_data);
            mips[mip_level].data.assign(
                full_seg[mip_level].begin(),
                full_seg[mip_level].begin() +
                    static_cast<std::ptrdiff_t>(std::min(per_layer,
                                                         full_seg[mip_level].size())));
        }

        out.mips = std::move(mips);
        out.num_layers = num_layers;

        // Build layer_data[L][M] from each segment's per_layer slice.
        out.layer_data.assign(static_cast<std::size_t>(num_layers),
                              std::vector<std::vector<std::uint8_t>>(total_mips));
        for (int mip_level = 0; mip_level < total_mips; ++mip_level) {
            const auto& seg = full_seg[mip_level];
            const std::size_t per_layer = per_layer_seg_bytes[mip_level];
            if (seg.empty() || per_layer == 0) continue;
            const int available_layers =
                static_cast<int>(std::min<std::size_t>(
                    seg.size() / per_layer,
                    static_cast<std::size_t>(num_layers)));
            for (int L = 0; L < available_layers; ++L) {
                const std::size_t start = static_cast<std::size_t>(L) * per_layer;
                out.layer_data[L][mip_level].assign(
                    seg.begin() + static_cast<std::ptrdiff_t>(start),
                    seg.begin() + static_cast<std::ptrdiff_t>(start + per_layer));
            }
            // Pad missing layers with layer 0's bytes so downstream code
            // never reads out-of-range when the header lies about layers.
            for (int L = available_layers; L < num_layers; ++L) {
                out.layer_data[L][mip_level] = out.layer_data[0][mip_level];
            }
        }
    } else {
        // v4: continuous payload, infer ASTC block/mip count from total_payload_size.
        const std::size_t hdr_size = container::kHeaderV4;
        if (size < hdr_size + hdr.total_payload_size) {
            throw std::runtime_error("v4: payload truncated");
        }
        const std::uint8_t* raw_payload = data + hdr_size;
        std::size_t raw_size = hdr.total_payload_size;

        // Detect zlib-compressed payload (most non-ASTC v4 files use zlib).
        std::vector<std::uint8_t> decompressed;
        const std::uint8_t* payload = raw_payload;
        std::size_t payload_size = raw_size;
        const bool zlib_framed =
            raw_size >= 2 && raw_payload[0] == 0x78 &&
            (raw_payload[1] == 0x01 || raw_payload[1] == 0x9C ||
             raw_payload[1] == 0xDA || raw_payload[1] == 0x5E ||
             raw_payload[1] == 0x7D);
        if (zlib_framed) {
            const std::size_t hint =
                hdr.v4_payload_copy > raw_size ? hdr.v4_payload_copy : raw_size * 4;
            decompressed = container::zlib_decompress(raw_payload, raw_size, hint);
            payload = decompressed.data();
            payload_size = decompressed.size();
        }

        auto inferred = infer_astc_v4(hdr.dim_x, hdr.dim_y, payload_size);
        if (inferred) {
            const auto [bx, by] = inferred->first;
            const int n_mips = inferred->second;
            out.block_x = bx;
            out.block_y = by;
            out.is_astc = true;
            out.mips.resize(n_mips);

            std::size_t off = 0;
            for (int mip = 0; mip < n_mips; ++mip) {
                const std::size_t w = mip_dim(hdr.dim_x, mip);
                const std::size_t h = mip_dim(hdr.dim_y, mip);
                const std::size_t mip_size = mip_raw_astc(w, h, bx, by);
                out.mips[mip].width  = static_cast<int>(w);
                out.mips[mip].height = static_cast<int>(h);
                out.mips[mip].data.assign(payload + off, payload + off + mip_size);
                off += mip_size;
            }
            return out;
        }

        // Try uncompressed raw for channel counts 1..4, single mip or mip chain.
        auto try_raw = [&](int bpp) -> bool {
            // Single mip.
            const std::size_t mip0 =
                mip_raw_uncompressed(hdr.dim_x, hdr.dim_y, bpp);
            if (mip0 == payload_size) {
                out.is_astc = false;
                out.bytes_per_pixel = bpp;
                out.mips.resize(1);
                out.mips[0].width  = static_cast<int>(hdr.dim_x);
                out.mips[0].height = static_cast<int>(hdr.dim_y);
                out.mips[0].data.assign(payload, payload + mip0);
                return true;
            }
            // Mip chain down to 1x1.
            std::size_t total = 0;
            std::size_t w = hdr.dim_x, h = hdr.dim_y;
            int n_mips = 0;
            while (w >= 1 && h >= 1) {
                total += mip_raw_uncompressed(w, h, bpp);
                ++n_mips;
                if (total == payload_size) {
                    out.is_astc = false;
                    out.bytes_per_pixel = bpp;
                    out.mips.resize(n_mips);
                    std::size_t off = 0;
                    std::size_t ww = hdr.dim_x, hh = hdr.dim_y;
                    for (int m = 0; m < n_mips; ++m) {
                        const std::size_t ms = mip_raw_uncompressed(ww, hh, bpp);
                        out.mips[m].width  = static_cast<int>(ww);
                        out.mips[m].height = static_cast<int>(hh);
                        out.mips[m].data.assign(payload + off, payload + off + ms);
                        off += ms;
                        ww = ww > 1 ? ww >> 1 : 1;
                        hh = hh > 1 ? hh >> 1 : 1;
                        if (ww == 1 && hh == 1 && m + 1 == n_mips) break;
                    }
                    return true;
                }
                if (total > payload_size) break;
                w = w > 1 ? w >> 1 : 0;
                h = h > 1 ? h >> 1 : 0;
                if (w == 0 || h == 0) break;
            }
            return false;
        };
        for (int bpp : {1, 2, 3, 4, 8, 12, 16}) {
            if (try_raw(bpp)) return out;
        }

        // Multi-layer (array / cubemap / 3D) ASTC with mip chain: payload is N
        // contiguous per-layer mip chains. Surface every layer.
        for (auto [bx, by] : kAstcBlocks) {
            std::size_t per_layer = 0;
            std::size_t w = hdr.dim_x, h = hdr.dim_y;
            std::vector<std::pair<std::size_t, std::size_t>> chain;
            while (w >= 1 && h >= 1) {
                per_layer += mip_raw_astc(w, h, bx, by);
                chain.emplace_back(w, h);
                if (per_layer > 0 && payload_size % per_layer == 0) {
                    const std::size_t layers = payload_size / per_layer;
                    if (layers >= 1 && layers <= 64) {
                        out.block_x = bx;
                        out.block_y = by;
                        out.is_astc = true;
                        out.mips.resize(chain.size());
                        out.num_layers = static_cast<int>(layers);
                        out.layer_data.assign(
                            layers,
                            std::vector<std::vector<std::uint8_t>>(chain.size()));
                        std::size_t off = 0;
                        for (std::size_t k = 0; k < chain.size(); ++k) {
                            const auto [cw, ch] = chain[k];
                            const std::size_t ms = mip_raw_astc(cw, ch, bx, by);
                            out.mips[k].width  = static_cast<int>(cw);
                            out.mips[k].height = static_cast<int>(ch);
                            out.mips[k].data.assign(payload + off, payload + off + ms);
                            off += ms;
                        }
                        for (std::size_t L = 0; L < layers; ++L) {
                            std::size_t lo =
                                L * per_layer;  // start of layer L
                            for (std::size_t k = 0; k < chain.size(); ++k) {
                                const auto [cw, ch] = chain[k];
                                const std::size_t ms = mip_raw_astc(cw, ch, bx, by);
                                out.layer_data[L][k].assign(
                                    payload + lo, payload + lo + ms);
                                lo += ms;
                            }
                        }
                        return out;
                    }
                }
                if (w == 1 && h == 1) break;
                w = w > 1 ? w >> 1 : 1;
                h = h > 1 ? h >> 1 : 1;
            }
        }
        for (int bpp : {1, 2, 3, 4, 6, 8, 12, 16}) {
            const std::size_t per_layer =
                mip_raw_uncompressed(hdr.dim_x, hdr.dim_y, bpp);
            if (per_layer > 0 && payload_size % per_layer == 0) {
                const std::size_t layers = payload_size / per_layer;
                if (layers >= 1 && layers <= 256) {
                    out.is_astc = false;
                    out.bytes_per_pixel = bpp;
                    out.mips.resize(1);
                    out.mips[0].width  = static_cast<int>(hdr.dim_x);
                    out.mips[0].height = static_cast<int>(hdr.dim_y);
                    out.mips[0].data.assign(payload, payload + per_layer);
                    out.num_layers = static_cast<int>(layers);
                    out.layer_data.assign(
                        layers, std::vector<std::vector<std::uint8_t>>(1));
                    for (std::size_t L = 0; L < layers; ++L) {
                        out.layer_data[L][0].assign(
                            payload + L * per_layer,
                            payload + (L + 1) * per_layer);
                    }
                    return out;
                }
            }
        }

        // Multi-layer raw with mip chain that may stop before 1x1 (common for
        // cubemaps / array textures that truncate at 4x4).
        for (int bpp : {1, 2, 3, 4, 6, 8, 12, 16}) {
            std::size_t per_layer = 0;
            std::size_t w = hdr.dim_x, h = hdr.dim_y;
            std::vector<std::pair<std::size_t, std::size_t>> chain;
            while (w >= 1 && h >= 1) {
                per_layer += mip_raw_uncompressed(w, h, bpp);
                chain.emplace_back(w, h);
                if (per_layer > 0 && payload_size % per_layer == 0) {
                    const std::size_t layers = payload_size / per_layer;
                    if (layers >= 1 && layers <= 64) {
                        out.is_astc = false;
                        out.bytes_per_pixel = bpp;
                        out.mips.resize(chain.size());
                        out.num_layers = static_cast<int>(layers);
                        out.layer_data.assign(
                            layers,
                            std::vector<std::vector<std::uint8_t>>(chain.size()));
                        std::size_t off = 0;
                        for (std::size_t k = 0; k < chain.size(); ++k) {
                            const auto [cw, ch] = chain[k];
                            const std::size_t ms = mip_raw_uncompressed(cw, ch, bpp);
                            out.mips[k].width  = static_cast<int>(cw);
                            out.mips[k].height = static_cast<int>(ch);
                            out.mips[k].data.assign(payload + off, payload + off + ms);
                            off += ms;
                        }
                        for (std::size_t L = 0; L < layers; ++L) {
                            std::size_t lo = L * per_layer;
                            for (std::size_t k = 0; k < chain.size(); ++k) {
                                const auto [cw, ch] = chain[k];
                                const std::size_t ms =
                                    mip_raw_uncompressed(cw, ch, bpp);
                                out.layer_data[L][k].assign(
                                    payload + lo, payload + lo + ms);
                                lo += ms;
                            }
                        }
                        return out;
                    }
                }
                if (w == 1 && h == 1) break;
                w = w > 1 ? w >> 1 : 1;
                h = h > 1 ? h >> 1 : 1;
            }
        }

        throw std::runtime_error("v4: cannot infer ASTC block size or raw format");
    }

    return out;
}

std::vector<std::uint8_t> encode_container_v6(
    const std::vector<std::vector<std::uint8_t>>& mip_blocks_base_first,
    int dim_x, int dim_y, int num_layers, int block_x, int block_y,
    std::uint32_t format_code_a, std::uint32_t flip_mode, int mip_count_total) {
    // Pack up to 4 segments. Segments store mip levels in reverse order:
    // seg0 -> highest mip (smallest), seg3 -> base mip (largest).
    if (mip_blocks_base_first.empty()) {
        throw std::runtime_error("no mip data supplied for v6 encode");
    }
    const int n_mips = static_cast<int>(mip_blocks_base_first.size());
    const int num_segs = std::min(n_mips, 4);

    // For textures with more than 4 mip levels, fold the extra (smallest)
    // levels into segment 0 by concatenation so the last segment still
    // aligns with the base mip.
    std::vector<std::vector<std::uint8_t>> seg_data(num_segs);
    for (int i = 0; i < num_segs; ++i) {
        const int mip_level = (num_segs - 1) - i;
        seg_data[i] = mip_blocks_base_first[mip_level];
    }
    for (int extra = num_segs; extra < n_mips; ++extra) {
        // Append tail levels to seg 0 (smallest) preserving order.
        auto& s0 = seg_data[0];
        const auto& tail = mip_blocks_base_first[extra];
        s0.insert(s0.begin(), tail.begin(), tail.end());
    }

    // zlib compress each segment; keep raw if compression offers no gain.
    std::array<std::uint32_t, 4> seg_sizes{};
    std::vector<std::vector<std::uint8_t>> seg_final(4);
    for (int i = 0; i < num_segs; ++i) {
        const auto& raw = seg_data[i];
        auto comp = container::zlib_compress(raw.data(), raw.size(), 6);
        if (comp.size() + 8 >= raw.size()) {
            seg_final[i] = raw;
        } else {
            seg_final[i] = std::move(comp);
        }
        seg_sizes[i] = static_cast<std::uint32_t>(seg_final[i].size());
    }

    // Build header
    container::Header hdr{};
    hdr.version = 6;
    hdr.dim_x = static_cast<std::uint32_t>(dim_x);
    hdr.dim_y = static_cast<std::uint32_t>(dim_y);
    hdr.dim_z = 1;
    hdr.field_u16_a = 4;  // channel count (RGBA)
    hdr.mip_count = static_cast<std::uint16_t>(mip_count_total > 0 ? mip_count_total - 1 : 0);
    hdr.field_count = static_cast<std::uint32_t>(num_layers);
    hdr.flags = container::kFlagHasAux | (mip_count_total > 1 ? container::kFlagHasMipmaps : 0u);
    hdr.seg_sizes = seg_sizes;
    hdr.format_code_a = format_code_a;
    hdr.flip_mode = flip_mode;
    hdr.format_code_b = 1;

    // Compute total payload size including padding between segments.
    std::size_t payload = 0;
    for (int i = 0; i < num_segs; ++i) {
        payload += seg_final[i].size();
        if (i < num_segs - 1) {
            const std::size_t aligned = (seg_final[i].size() + 0x0F) & ~std::size_t{0x0F};
            payload += aligned - seg_final[i].size();
        }
    }
    hdr.total_payload_size = static_cast<std::uint32_t>(payload);

    auto header_bytes = container::write_header(hdr);
    std::vector<std::uint8_t> out;
    out.reserve(header_bytes.size() + payload);
    out.insert(out.end(), header_bytes.begin(), header_bytes.end());
    for (int i = 0; i < num_segs; ++i) {
        out.insert(out.end(), seg_final[i].begin(), seg_final[i].end());
        if (i < num_segs - 1) {
            const std::size_t aligned = (seg_final[i].size() + 0x0F) & ~std::size_t{0x0F};
            out.insert(out.end(), aligned - seg_final[i].size(), std::uint8_t{0});
        }
    }
    return out;
}

std::vector<std::uint8_t> encode_container_v4(
    const std::vector<std::vector<std::uint8_t>>& mip_blocks_base_first,
    int dim_x, int dim_y) {
    // v4 stores mip levels contiguously, base mip first, no compression.
    container::Header hdr{};
    hdr.version = 4;
    hdr.dim_x = static_cast<std::uint32_t>(dim_x);
    hdr.dim_y = static_cast<std::uint32_t>(dim_y);
    hdr.dim_z = 1;
    hdr.field_u16_a = 4;
    hdr.mip_count = static_cast<std::uint16_t>(
        mip_blocks_base_first.empty() ? 0 : mip_blocks_base_first.size() - 1);
    hdr.field_count = 1;
    hdr.flags = container::kFlagCompressed;  // ASTC compressed payload
    hdr.v4_field_0 = 2;
    hdr.v4_field_1 = 24;

    std::size_t total = 0;
    for (const auto& m : mip_blocks_base_first) total += m.size();
    hdr.total_payload_size = static_cast<std::uint32_t>(total);
    hdr.v4_payload_copy = hdr.total_payload_size;

    auto header_bytes = container::write_header(hdr);
    std::vector<std::uint8_t> out;
    out.reserve(header_bytes.size() + total);
    out.insert(out.end(), header_bytes.begin(), header_bytes.end());
    for (const auto& m : mip_blocks_base_first) {
        out.insert(out.end(), m.begin(), m.end());
    }
    return out;
}

}  // namespace gstextconv::codec
