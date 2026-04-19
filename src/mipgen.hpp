// SPDX-License-Identifier: MIT
#ifndef GSTEXTCONV_SRC_MIPGEN_HPP_INCLUDED
#define GSTEXTCONV_SRC_MIPGEN_HPP_INCLUDED
#include <cstdint>
#include <vector>

namespace gstextconv::mip {

/// Downsample an RGBA8 image by 2×2 box filter. `w`/`h` are source dims.
std::vector<std::uint8_t> downsample_rgba8(
    const std::uint8_t* rgba, int w, int h, int& out_w, int& out_h);

/// Generate a mip chain of RGBA8 images starting from `base`. The returned
/// vector contains `count` entries starting with `base` at index 0.
std::vector<std::vector<std::uint8_t>> build_chain_rgba8(
    const std::uint8_t* base, int w, int h, int count,
    std::vector<std::pair<int, int>>& out_sizes);

}  // namespace gstextconv::mip
#endif
