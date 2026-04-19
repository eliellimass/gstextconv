// SPDX-License-Identifier: MIT
#include "mipgen.hpp"

#include <algorithm>
#include <cstring>

namespace gstextconv::mip {

std::vector<std::uint8_t> downsample_rgba8(
    const std::uint8_t* rgba, int w, int h, int& out_w, int& out_h) {
    out_w = std::max(1, w / 2);
    out_h = std::max(1, h / 2);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(out_w) * out_h * 4);
    for (int y = 0; y < out_h; ++y) {
        const int sy0 = y * 2;
        const int sy1 = std::min(sy0 + 1, h - 1);
        for (int x = 0; x < out_w; ++x) {
            const int sx0 = x * 2;
            const int sx1 = std::min(sx0 + 1, w - 1);
            for (int c = 0; c < 4; ++c) {
                unsigned s = rgba[(sy0 * w + sx0) * 4 + c]
                           + rgba[(sy0 * w + sx1) * 4 + c]
                           + rgba[(sy1 * w + sx0) * 4 + c]
                           + rgba[(sy1 * w + sx1) * 4 + c];
                out[(y * out_w + x) * 4 + c] = static_cast<std::uint8_t>((s + 2) / 4);
            }
        }
    }
    return out;
}

std::vector<std::vector<std::uint8_t>> build_chain_rgba8(
    const std::uint8_t* base, int w, int h, int count,
    std::vector<std::pair<int, int>>& out_sizes) {
    std::vector<std::vector<std::uint8_t>> out;
    out_sizes.clear();
    out.emplace_back(base, base + static_cast<std::size_t>(w) * h * 4);
    out_sizes.emplace_back(w, h);
    int cur_w = w, cur_h = h;
    for (int i = 1; i < count; ++i) {
        if (cur_w <= 1 && cur_h <= 1) break;
        int nw = 0, nh = 0;
        auto next = downsample_rgba8(out.back().data(), cur_w, cur_h, nw, nh);
        out.push_back(std::move(next));
        out_sizes.emplace_back(nw, nh);
        cur_w = nw;
        cur_h = nh;
    }
    return out;
}

}  // namespace gstextconv::mip
