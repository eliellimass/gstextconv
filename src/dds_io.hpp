// SPDX-License-Identifier: MIT
// Direct DDS (DirectDraw Surface) loader — decodes uncompressed RGB/RGBA,
// BGR/BGRA, BC1 (DXT1), BC3 (DXT5) and BC4 (alpha only) payloads into RGBA8.
// Preserves the full mip chain and, for DX10-style arrays, every slice.

#ifndef GSTEXTCONV_SRC_DDS_IO_HPP_INCLUDED
#define GSTEXTCONV_SRC_DDS_IO_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gstextconv::ddsio {

struct Mip {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // 4 bytes per pixel, top-left origin
};

struct LoadedDDS {
    int width = 0;
    int height = 0;
    int num_mipmaps = 1;  // total mip levels, including the base
    int num_layers  = 1;  // array size (1 for plain textures, 6 for cubemaps)
    bool is_cubemap = false;
    bool is_array   = false;
    bool has_alpha  = true;
    // layers[L].mips[M] — layer L, mip level M. mips[0] is always the base.
    struct Layer { std::vector<Mip> mips; };
    std::vector<Layer> layers;
};

/// True when `data` starts with the 'DDS ' magic (little-endian 0x20534444).
bool looks_like_dds(const std::uint8_t* data, std::size_t size) noexcept;

/// Decode a .dds buffer into RGBA8 layers/mips.
///
/// Throws gstextconv::Error for unsupported pixel formats or malformed files.
LoadedDDS decode_dds(const std::uint8_t* data, std::size_t size);

}  // namespace gstextconv::ddsio

#endif
