// SPDX-License-Identifier: MIT
#include "flip.hpp"
#include <cstring>
#include <vector>

namespace gstextconv::flipops {

void flip_vertical(std::uint8_t* buffer, std::size_t width, std::size_t height, int bpp) {
    if (height < 2) return;
    const std::size_t row = width * static_cast<std::size_t>(bpp);
    std::vector<std::uint8_t> tmp(row);
    for (std::size_t y = 0; y < height / 2; ++y) {
        std::uint8_t* a = buffer + y * row;
        std::uint8_t* b = buffer + (height - 1 - y) * row;
        std::memcpy(tmp.data(), a, row);
        std::memcpy(a, b, row);
        std::memcpy(b, tmp.data(), row);
    }
}

}  // namespace gstextconv::flipops
