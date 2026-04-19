// SPDX-License-Identifier: MIT
// astcenc bridge — wraps the embedded astcenc library.

#ifndef GSTEXTCONV_SRC_ASTC_BRIDGE_HPP_INCLUDED
#define GSTEXTCONV_SRC_ASTC_BRIDGE_HPP_INCLUDED

#include <cstdint>
#include <cstddef>
#include <vector>

#include "gstextconv/gstextconv.hpp"

namespace gstextconv::astc {

/// Compress an RGBA8 mip slice into ASTC blocks.
/// @p rgba must have `width * height * 4` bytes, top-left origin.
std::vector<std::uint8_t> compress_rgba8(
    const std::uint8_t* rgba,
    int width, int height,
    int block_x, int block_y,
    Quality quality,
    ColorSpace color_space);

/// Decompress an ASTC block buffer back to tightly packed RGBA8.
/// Output size: `width * height * 4` bytes (top-left origin).
std::vector<std::uint8_t> decompress_to_rgba8(
    const std::uint8_t* astc_blocks,
    std::size_t blocks_size,
    int width, int height,
    int block_x, int block_y,
    ColorSpace color_space);

/// Build a 16-byte standalone .astc file header.
std::vector<std::uint8_t> build_astc_file_header(
    int width, int height, int depth, int block_x, int block_y);

}  // namespace gstextconv::astc

#endif  // GSTEXTCONV_SRC_ASTC_BRIDGE_HPP_INCLUDED
