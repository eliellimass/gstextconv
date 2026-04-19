// SPDX-License-Identifier: MIT
// Image I/O (PNG, JPG, raw-RGBA) via stb_image / stb_image_write.

#ifndef GSTEXTCONV_SRC_IMAGE_IO_HPP_INCLUDED
#define GSTEXTCONV_SRC_IMAGE_IO_HPP_INCLUDED

#include <cstdint>
#include <vector>

namespace gstextconv::imageio {

struct LoadedImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // always 4 bytes per pixel
};

/// Decode a PNG or JPG from memory into RGBA8 (top-left origin).
LoadedImage decode_png_jpg(const std::uint8_t* data, std::size_t size);

std::vector<std::uint8_t> encode_png(
    const std::uint8_t* pixels, int width, int height, int channels);

std::vector<std::uint8_t> encode_jpg(
    const std::uint8_t* pixels, int width, int height, int channels, int quality);

}  // namespace gstextconv::imageio

#endif
