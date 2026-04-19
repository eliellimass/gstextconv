// SPDX-License-Identifier: MIT
#include "color_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace gstextconv::cfmt {

int bytes_per_pixel(ColorFormat f) {
    switch (f) {
        case ColorFormat::R8:       return 1;
        case ColorFormat::RG16:     return 2;
        case ColorFormat::RGB24:    return 3;
        case ColorFormat::BGR24:    return 3;
        case ColorFormat::RGBA32:   return 4;
        case ColorFormat::BGRA32:   return 4;
        case ColorFormat::RGBA64F:  return 8;   // 4 × fp16
        case ColorFormat::RGBA128F: return 16;  // 4 × fp32
    }
    return 4;
}

int channels_in(ColorFormat f) {
    switch (f) {
        case ColorFormat::R8:       return 1;
        case ColorFormat::RG16:     return 2;
        case ColorFormat::RGB24:
        case ColorFormat::BGR24:    return 3;
        default: return 4;
    }
}

bool is_float(ColorFormat f) {
    return f == ColorFormat::RGBA64F || f == ColorFormat::RGBA128F;
}

namespace {

inline std::uint8_t f32_to_u8(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(v * 255.0f));
}

inline float f16_to_f32(std::uint16_t h) {
    // Standard IEEE 754 half -> float conversion.
    const std::uint32_t sign = (h >> 15) & 0x1u;
    std::uint32_t exp = (h >> 10) & 0x1Fu;
    std::uint32_t mant = h & 0x3FFu;
    std::uint32_t bits = 0;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign << 31;
        } else {
            // Subnormal.
            while ((mant & 0x400u) == 0) {
                mant <<= 1;
                exp = static_cast<std::uint32_t>(exp) - 1;
            }
            exp = exp + 1;
            mant &= ~0x400u;
            bits = (sign << 31) | ((exp + (127 - 15)) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        bits = (sign << 31) | (0xFFu << 23) | (mant << 13);
    } else {
        bits = (sign << 31) | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float out;
    std::memcpy(&out, &bits, sizeof out);
    return out;
}

inline std::uint16_t f32_to_f16(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    std::uint32_t sign = (bits >> 16) & 0x8000u;
    std::int32_t  exp  = static_cast<std::int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    std::uint32_t mant = (bits >> 13) & 0x3FFu;
    if (exp <= 0) {
        return static_cast<std::uint16_t>(sign);
    }
    if (exp >= 31) {
        return static_cast<std::uint16_t>(sign | (0x1Fu << 10));
    }
    return static_cast<std::uint16_t>(sign | (exp << 10) | mant);
}

}  // namespace

std::vector<std::uint8_t> to_rgba8(
    const std::uint8_t* src, std::size_t width, std::size_t height,
    ColorFormat src_format) {
    const std::size_t n = width * height;
    std::vector<std::uint8_t> out(n * 4, 0);
    switch (src_format) {
        case ColorFormat::R8:
            for (std::size_t i = 0; i < n; ++i) {
                std::uint8_t v = src[i];
                out[i*4+0] = v; out[i*4+1] = v; out[i*4+2] = v; out[i*4+3] = 255;
            }
            break;
        case ColorFormat::RG16:
            for (std::size_t i = 0; i < n; ++i) {
                out[i*4+0] = src[i*2+0];
                out[i*4+1] = src[i*2+1];
                out[i*4+2] = 0;
                out[i*4+3] = 255;
            }
            break;
        case ColorFormat::RGB24:
            for (std::size_t i = 0; i < n; ++i) {
                out[i*4+0] = src[i*3+0];
                out[i*4+1] = src[i*3+1];
                out[i*4+2] = src[i*3+2];
                out[i*4+3] = 255;
            }
            break;
        case ColorFormat::BGR24:
            for (std::size_t i = 0; i < n; ++i) {
                out[i*4+0] = src[i*3+2];
                out[i*4+1] = src[i*3+1];
                out[i*4+2] = src[i*3+0];
                out[i*4+3] = 255;
            }
            break;
        case ColorFormat::RGBA32:
            std::memcpy(out.data(), src, n * 4);
            break;
        case ColorFormat::BGRA32:
            for (std::size_t i = 0; i < n; ++i) {
                out[i*4+0] = src[i*4+2];
                out[i*4+1] = src[i*4+1];
                out[i*4+2] = src[i*4+0];
                out[i*4+3] = src[i*4+3];
            }
            break;
        case ColorFormat::RGBA64F: {
            const std::uint16_t* s = reinterpret_cast<const std::uint16_t*>(src);
            for (std::size_t i = 0; i < n; ++i) {
                for (int c = 0; c < 4; ++c) {
                    out[i*4+c] = f32_to_u8(f16_to_f32(s[i*4+c]));
                }
            }
        } break;
        case ColorFormat::RGBA128F: {
            const float* s = reinterpret_cast<const float*>(src);
            for (std::size_t i = 0; i < n; ++i) {
                for (int c = 0; c < 4; ++c) {
                    out[i*4+c] = f32_to_u8(s[i*4+c]);
                }
            }
        } break;
    }
    return out;
}

std::vector<std::uint8_t> from_rgba8(
    const std::uint8_t* rgba, std::size_t width, std::size_t height,
    ColorFormat dst_format) {
    const std::size_t n = width * height;
    const int bpp = bytes_per_pixel(dst_format);
    std::vector<std::uint8_t> out(n * bpp, 0);
    switch (dst_format) {
        case ColorFormat::R8:
            for (std::size_t i = 0; i < n; ++i) {
                // Convert to grayscale by weighted average.
                out[i] = static_cast<std::uint8_t>(
                    (rgba[i*4+0] * 299 + rgba[i*4+1] * 587 + rgba[i*4+2] * 114) / 1000);
            }
            break;
        case ColorFormat::RG16:
            for (std::size_t i = 0; i < n; ++i) {
                out[i*2+0] = rgba[i*4+0];
                out[i*2+1] = rgba[i*4+1];
            }
            break;
        case ColorFormat::RGB24:
            for (std::size_t i = 0; i < n; ++i) {
                out[i*3+0] = rgba[i*4+0];
                out[i*3+1] = rgba[i*4+1];
                out[i*3+2] = rgba[i*4+2];
            }
            break;
        case ColorFormat::BGR24:
            for (std::size_t i = 0; i < n; ++i) {
                out[i*3+0] = rgba[i*4+2];
                out[i*3+1] = rgba[i*4+1];
                out[i*3+2] = rgba[i*4+0];
            }
            break;
        case ColorFormat::RGBA32:
            std::memcpy(out.data(), rgba, n * 4);
            break;
        case ColorFormat::BGRA32:
            for (std::size_t i = 0; i < n; ++i) {
                out[i*4+0] = rgba[i*4+2];
                out[i*4+1] = rgba[i*4+1];
                out[i*4+2] = rgba[i*4+0];
                out[i*4+3] = rgba[i*4+3];
            }
            break;
        case ColorFormat::RGBA64F: {
            std::uint16_t* d = reinterpret_cast<std::uint16_t*>(out.data());
            for (std::size_t i = 0; i < n; ++i) {
                for (int c = 0; c < 4; ++c) {
                    d[i*4+c] = f32_to_f16(rgba[i*4+c] / 255.0f);
                }
            }
        } break;
        case ColorFormat::RGBA128F: {
            float* d = reinterpret_cast<float*>(out.data());
            for (std::size_t i = 0; i < n; ++i) {
                for (int c = 0; c < 4; ++c) {
                    d[i*4+c] = rgba[i*4+c] / 255.0f;
                }
            }
        } break;
    }
    return out;
}

std::vector<std::uint8_t> swizzle_rgba8(
    const std::uint8_t* rgba, std::size_t width, std::size_t height,
    const char* channels, std::size_t channel_count) {
    const std::size_t n = width * height;
    std::vector<std::uint8_t> out(n * channel_count);
    int map[8] = {0};
    for (std::size_t c = 0; c < channel_count; ++c) {
        switch (channels[c]) {
            case 'r': case 'R': map[c] = 0; break;
            case 'g': case 'G': map[c] = 1; break;
            case 'b': case 'B': map[c] = 2; break;
            case 'a': case 'A': map[c] = 3; break;
            default:            map[c] = 0; break;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t c = 0; c < channel_count; ++c) {
            out[i * channel_count + c] = rgba[i * 4 + map[c]];
        }
    }
    return out;
}

}  // namespace gstextconv::cfmt
