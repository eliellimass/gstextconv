// SPDX-License-Identifier: MIT
// Copyright (c) 2026 snowbit64
//
// gstextconv — Public C++ API for reading and writing Giants Engine
// texture containers (GS2D/.ast) used by Farming Simulator titles.
//
// The library statically links an embedded copy of astcenc (Apache-2.0)
// for ASTC compression/decompression and miniz (MIT) for zlib segments.

#ifndef GSTEXTCONV_HPP_INCLUDED
#define GSTEXTCONV_HPP_INCLUDED

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gstextconv {

/// Pixel/color storage format used for raw images and decoder output.
enum class ColorFormat {
    R8,        // 8-bit 1 channel
    RG16,      // 8-bit 2 channels
    RGB24,     // 8-bit 3 channels
    BGR24,     // 8-bit 3 channels (B before R)
    RGBA32,    // 8-bit 4 channels (default)
    BGRA32,    // 8-bit 4 channels (B before R)
    RGBA64F,   // 16-bit float 4 channels
    RGBA128F,  // 32-bit float 4 channels
};

enum class ColorSpace { Srgb, Linear, Alpha };
enum class Quality    { Fast, Medium, Thorough };
enum class Origin     { TopLeft, BottomLeft };
enum class TextureType{ TwoD, TwoDArray };
enum class NormalMapFormat { RG, RGB };

/// Target Farming Simulator release; determines the GS2D container version.
enum class TargetGame { FS20, FS23, FS26 };

/// Container version as exposed to Python/CLI ("v3" ≈ v4 binary, "v6" = v6).
enum class ContainerVersion { V3, V6 };

struct BlockSize {
    int x = 6;
    int y = 6;
};

/// Raster data for one mip level of one layer, stored as tightly packed bytes
/// in the image's declared @ref ColorFormat.
struct MipLevel {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> data;
};

/// In-memory texture representation. `layers[L][M]` is layer @p L, mip @p M.
/// Mip level 0 is always the base (largest) resolution.
struct Image {
    int width = 0;
    int height = 0;
    int num_layers = 1;
    int num_mipmaps = 1;
    ColorFormat color_format = ColorFormat::RGBA32;
    ColorSpace  color_space  = ColorSpace::Srgb;
    Origin      origin       = Origin::TopLeft;
    TextureType type         = TextureType::TwoD;
    BlockSize   compression  = {0, 0};                  // zero means uncompressed source
    ContainerVersion version = ContainerVersion::V6;
    std::vector<std::vector<MipLevel>> layers;

    /// Convenience: return the raw bytes of mip 0 of layer 0.
    [[nodiscard]] const std::vector<std::uint8_t>& raw() const noexcept {
        return layers.at(0).at(0).data;
    }
};

struct EncodeOptions {
    TargetGame  target_game = TargetGame::FS23;
    int         mipmaps     = 0;                         // 0 -> base only, N -> N additional, <0 -> auto/max
    BlockSize   block_size  = {6, 6};
    Quality     quality     = Quality::Fast;
    ColorSpace  color_space = ColorSpace::Srgb;
    ColorFormat color_format = ColorFormat::RGBA32;
    std::optional<std::pair<int, int>> resize;
    Origin      ideal_origin    = Origin::TopLeft;
    TextureType texture_type    = TextureType::TwoD;
    std::array<char, 4> roughness_channel = {'r', 'g', 'b', 'a'};
    NormalMapFormat normal_map_format = NormalMapFormat::RGB;
    /// When true and the source image already carries a mip chain (e.g. a DDS
    /// with stored mip levels), feed those mips straight to the encoder instead
    /// of regenerating the chain from the base level.
    bool inherit_mipmaps = false;
    /// When true and the source image has multiple array slices, encode every
    /// slice as a 2DArray layer instead of requiring the caller to split them.
    bool inherit_layers  = false;
};

/// Decode GS2D (.ast), ASTC (.astc) or raw-RGBA into an @ref Image.
/// ASTC payloads are decompressed back to RGBA8 using the embedded astcenc.
[[nodiscard]] Image decode(const std::uint8_t* data, std::size_t size);
[[nodiscard]] inline Image decode(const std::vector<std::uint8_t>& d) {
    return decode(d.data(), d.size());
}

/// Parse an .ast file without ASTC decompression. Mip levels hold the raw
/// ASTC blocks (for compressed textures) or raw pixels (for uncompressed).
/// This mirrors the behaviour of `gstextconv.loader(...)` in Python.
[[nodiscard]] Image load(const std::uint8_t* data, std::size_t size);
[[nodiscard]] inline Image load(const std::vector<std::uint8_t>& d) {
    return load(d.data(), d.size());
}

/// Encode an Image into a GS2D .ast container according to @p opts.
[[nodiscard]] std::vector<std::uint8_t> encode(
    const Image& src, const EncodeOptions& opts);

/// Encode one or more already-decoded images into a GS2D .ast.
/// When `opts.texture_type == TwoDArray`, all images must share dimensions;
/// otherwise each image is encoded as a separate 2D texture and the caller
/// receives the first encoded blob (additional blobs are returned via the
/// `extra_outputs` pointer when provided).
[[nodiscard]] std::vector<std::uint8_t> encode_many(
    const std::vector<Image>& inputs,
    const EncodeOptions& opts,
    std::vector<std::vector<std::uint8_t>>* extra_outputs = nullptr);

/// Read an image file from memory (PNG, JPG, or raw RGBA when
/// @p raw_width/height are provided) and return it as an RGBA8 Image.
[[nodiscard]] Image load_source_image(
    const std::uint8_t* data, std::size_t size,
    int raw_width  = 0,
    int raw_height = 0,
    ColorFormat raw_format = ColorFormat::RGBA32);

/// Serialise a decoded Image to a target file format.
enum class OutputFormat { PNG, JPG, RawRGBA, ASTC };

struct DecodeWriteOptions {
    OutputFormat format = OutputFormat::PNG;
    int   jpg_quality   = 90;
    int   mip_index     = 0;
    int   layer_index   = 0;
    bool  undo_flip     = true;
    std::array<char, 4> channels = {'r', 'g', 'b', 'a'};
};

[[nodiscard]] std::vector<std::uint8_t> write_image(
    const Image& img, const DecodeWriteOptions& opts);

/// Version string for the library (semver).
[[nodiscard]] std::string_view library_version() noexcept;

/// Short string describing the embedded astcenc version.
[[nodiscard]] std::string_view astcenc_version_string() noexcept;

/// Build-time constants (set via CMake definitions at compile time).
[[nodiscard]] std::string_view build_release() noexcept;
[[nodiscard]] std::string_view build_date() noexcept;

/// Exception thrown by every API function when an unrecoverable error occurs.
class Error : public std::exception {
public:
    enum class Code {
        InvalidFile      = 1,
        UnsupportedFormat = 2,
        ConversionFailed  = 3,
    };

    Error(Code code, std::string msg)
        : code_{code}, msg_{std::move(msg)} {}

    [[nodiscard]] const char* what() const noexcept override { return msg_.c_str(); }
    [[nodiscard]] Code code() const noexcept { return code_; }

private:
    Code        code_;
    std::string msg_;
};

}  // namespace gstextconv

#endif  // GSTEXTCONV_HPP_INCLUDED
