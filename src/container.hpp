// SPDX-License-Identifier: MIT
// Internal GS2D container structures and helpers. Not part of the public API.

#ifndef GSTEXTCONV_SRC_CONTAINER_HPP_INCLUDED
#define GSTEXTCONV_SRC_CONTAINER_HPP_INCLUDED

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gstextconv::container {

inline constexpr std::size_t kHeaderV4 = 0x3C;
inline constexpr std::size_t kHeaderV6 = 0x50;
inline constexpr std::size_t kMaxSegments = 4;

inline constexpr std::uint32_t kFlagCompressed = 0x01;
inline constexpr std::uint32_t kFlagHasMipmaps = 0x02;
inline constexpr std::uint32_t kFlagHasAux     = 0x04;

struct Header {
    std::array<std::uint8_t, 4> magic{'G', 'S', '2', 'D'};
    std::uint32_t version = 6;
    std::uint32_t total_payload_size = 0;
    std::uint32_t dim_x = 0;
    std::uint32_t dim_y = 0;
    std::uint32_t dim_z = 1;
    std::uint16_t field_u16_a = 4;
    std::uint16_t mip_count = 0;
    std::uint32_t field_count = 1;
    std::uint32_t flags = 0;
    std::array<std::uint32_t, kMaxSegments> seg_sizes{};
    std::array<std::uint32_t, 4> aux{};
    std::uint32_t format_code_a = 34;
    std::uint32_t flip_mode = 0;
    std::uint32_t format_code_b = 1;
    // v4 specific legacy fields (mirroring the reversed binary)
    std::uint32_t v4_field_0 = 2;
    std::uint32_t v4_field_1 = 24;
    std::uint32_t v4_field_2 = 0;
    std::uint32_t v4_field_3 = 0;
    std::uint32_t v4_field_4 = 0;
    std::uint32_t v4_payload_copy = 0;
};

struct SegmentLayout {
    std::array<std::size_t, kMaxSegments> offsets{};
};

bool is_gs2d(const std::uint8_t* data, std::size_t size) noexcept;
std::uint32_t version_of(const std::uint8_t* data, std::size_t size);

std::size_t header_size_for_version(std::uint32_t version) noexcept;

Header parse_header(const std::uint8_t* data, std::size_t size);
SegmentLayout compute_segment_layout(const Header& h);
std::vector<std::uint8_t> write_header(const Header& h);

std::vector<std::uint8_t> zlib_decompress(const std::uint8_t* src,
                                          std::size_t src_size,
                                          std::size_t expected_size);
std::vector<std::uint8_t> zlib_compress(const std::uint8_t* src,
                                        std::size_t src_size,
                                        int level = 6);

}  // namespace gstextconv::container

#endif  // GSTEXTCONV_SRC_CONTAINER_HPP_INCLUDED
