// SPDX-License-Identifier: MIT
// Color-format helpers — convert between the ColorFormat variants and
// tightly-packed RGBA8 (the internal working representation).

#ifndef GSTEXTCONV_SRC_COLOR_FORMAT_HPP_INCLUDED
#define GSTEXTCONV_SRC_COLOR_FORMAT_HPP_INCLUDED

#include <cstdint>
#include <vector>

#include "gstextconv/gstextconv.hpp"

namespace gstextconv::cfmt {

int  bytes_per_pixel(ColorFormat f);
int  channels_in(ColorFormat f);
bool is_float(ColorFormat f);

/// Convert an arbitrary source format buffer to tightly-packed RGBA8.
std::vector<std::uint8_t> to_rgba8(
    const std::uint8_t* src, std::size_t width, std::size_t height,
    ColorFormat src_format);

/// Convert a tightly-packed RGBA8 buffer to an arbitrary destination format.
std::vector<std::uint8_t> from_rgba8(
    const std::uint8_t* rgba, std::size_t width, std::size_t height,
    ColorFormat dst_format);

/// Apply a per-channel swizzle (e.g. drop alpha to get RGB, or reorder
/// channels). @p channels selects which RGBA8 channel provides each output
/// channel, in order. Returns a tightly-packed buffer with `len(channels)`
/// bytes per pixel.
std::vector<std::uint8_t> swizzle_rgba8(
    const std::uint8_t* rgba, std::size_t width, std::size_t height,
    const char* channels, std::size_t channel_count);

}  // namespace gstextconv::cfmt

#endif  // GSTEXTCONV_SRC_COLOR_FORMAT_HPP_INCLUDED
