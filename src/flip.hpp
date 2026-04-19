// SPDX-License-Identifier: MIT
#ifndef GSTEXTCONV_SRC_FLIP_HPP_INCLUDED
#define GSTEXTCONV_SRC_FLIP_HPP_INCLUDED
#include <cstdint>
#include <cstddef>

namespace gstextconv::flipops {

/// Flip @p buffer vertically in place. Buffer must contain
/// `width * height * bpp` bytes with row-major layout.
void flip_vertical(std::uint8_t* buffer, std::size_t width, std::size_t height, int bpp);

}  // namespace gstextconv::flipops
#endif
