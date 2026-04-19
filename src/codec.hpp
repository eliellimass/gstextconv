// SPDX-License-Identifier: MIT
// Internal codec interface — orchestrates container + astcenc + zlib.

#ifndef GSTEXTCONV_SRC_CODEC_HPP_INCLUDED
#define GSTEXTCONV_SRC_CODEC_HPP_INCLUDED

#include <cstdint>
#include <optional>
#include <vector>

#include "container.hpp"

namespace gstextconv::codec {

struct Mip {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> data;  // ASTC blocks (compressed) or raw pixels
};

/// Result of decoding a GS2D container. Mip 0 is the base (largest) mip.
struct DecodedContainer {
    container::Header header{};
    bool is_astc = false;
    int  block_x = 0;
    int  block_y = 0;
    int  bytes_per_pixel = 0;
    std::vector<Mip> mips;
};

DecodedContainer decode_container(const std::uint8_t* data, std::size_t size);

/// Encode a GS2D v6 container from per-mip ASTC buffers (base mip first).
std::vector<std::uint8_t> encode_container_v6(
    const std::vector<std::vector<std::uint8_t>>& mip_blocks_base_first,
    int dim_x, int dim_y, int num_layers, int block_x, int block_y,
    std::uint32_t format_code_a, std::uint32_t flip_mode, int mip_count_total);

std::vector<std::uint8_t> encode_container_v4(
    const std::vector<std::vector<std::uint8_t>>& mip_blocks_base_first,
    int dim_x, int dim_y);

}  // namespace gstextconv::codec

#endif  // GSTEXTCONV_SRC_CODEC_HPP_INCLUDED
