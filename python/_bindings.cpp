// SPDX-License-Identifier: MIT
// pybind11 bindings for gstextconv.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gstextconv/gstextconv.hpp"

namespace py = pybind11;
using namespace gstextconv;

namespace {

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    in.seekg(0, std::ios::end);
    const std::streamsize n = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(n));
    if (n > 0) in.read(reinterpret_cast<char*>(buf.data()), n);
    return buf;
}

void write_file(const std::string& path, const std::vector<std::uint8_t>& bytes,
                bool overwrite) {
    if (!overwrite) {
        std::ifstream probe(path);
        if (probe.good()) throw std::runtime_error("output exists: " + path);
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path);
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
}

TargetGame parse_game(const std::string& s) {
    if (s == "fs20") return TargetGame::FS20;
    if (s == "fs23") return TargetGame::FS23;
    if (s == "fs26") return TargetGame::FS26;
    throw std::invalid_argument("unknown target game: " + s);
}

Quality parse_quality(const std::string& s) {
    if (s == "fast")     return Quality::Fast;
    if (s == "medium")   return Quality::Medium;
    if (s == "thorough") return Quality::Thorough;
    throw std::invalid_argument("unknown quality: " + s);
}

ColorSpace parse_color_space(const std::string& s) {
    if (s == "srgb")   return ColorSpace::Srgb;
    if (s == "linear") return ColorSpace::Linear;
    if (s == "alpha")  return ColorSpace::Alpha;
    throw std::invalid_argument("unknown color space: " + s);
}

ColorFormat parse_color_format(const std::string& s) {
    if (s == "r8")       return ColorFormat::R8;
    if (s == "rg16")     return ColorFormat::RG16;
    if (s == "rgb24")    return ColorFormat::RGB24;
    if (s == "bgr24")    return ColorFormat::BGR24;
    if (s == "rgba32")   return ColorFormat::RGBA32;
    if (s == "bgra32")   return ColorFormat::BGRA32;
    if (s == "rgba64f")  return ColorFormat::RGBA64F;
    if (s == "rgba128f") return ColorFormat::RGBA128F;
    throw std::invalid_argument("unknown color format: " + s);
}

Origin parse_origin(const std::string& s) {
    if (s == "topLeft" || s == "top-left")       return Origin::TopLeft;
    if (s == "bottomLeft" || s == "bottom-left") return Origin::BottomLeft;
    throw std::invalid_argument("unknown origin: " + s);
}

TextureType parse_texture_type(const std::string& s) {
    if (s == "2d")      return TextureType::TwoD;
    if (s == "2darray") return TextureType::TwoDArray;
    throw std::invalid_argument("unknown texture type: " + s);
}

NormalMapFormat parse_normal_fmt(const std::string& s) {
    if (s == "rg")  return NormalMapFormat::RG;
    if (s == "rgb") return NormalMapFormat::RGB;
    throw std::invalid_argument("unknown normal map format: " + s);
}

BlockSize parse_block_size(const py::object& obj) {
    if (py::isinstance<py::str>(obj)) {
        auto s = obj.cast<std::string>();
        auto x_pos = s.find('x');
        if (x_pos == std::string::npos) throw std::invalid_argument("bad block size: " + s);
        return BlockSize{std::stoi(s.substr(0, x_pos)),
                         std::stoi(s.substr(x_pos + 1))};
    }
    auto t = obj.cast<std::pair<int, int>>();
    return BlockSize{t.first, t.second};
}

OutputFormat parse_output_format(const std::string& s) {
    if (s == "png")      return OutputFormat::PNG;
    if (s == "jpg" || s == "jpeg") return OutputFormat::JPG;
    if (s == "astc")     return OutputFormat::ASTC;
    if (s == "raw-rgb" || s == "raw-rgba" || s == "raw") return OutputFormat::RawRGBA;
    throw std::invalid_argument("unknown output format: " + s);
}

std::string color_format_to_str(ColorFormat f) {
    switch (f) {
        case ColorFormat::R8:       return "r8";
        case ColorFormat::RG16:     return "rg16";
        case ColorFormat::RGB24:    return "rgb24";
        case ColorFormat::BGR24:    return "bgr24";
        case ColorFormat::RGBA32:   return "rgba32";
        case ColorFormat::BGRA32:   return "bgra32";
        case ColorFormat::RGBA64F:  return "rgba64f";
        case ColorFormat::RGBA128F: return "rgba128f";
    }
    return "rgba32";
}

std::string color_space_to_str(ColorSpace s) {
    switch (s) {
        case ColorSpace::Srgb:   return "srgb";
        case ColorSpace::Linear: return "linear";
        case ColorSpace::Alpha:  return "alpha";
    }
    return "srgb";
}

std::string origin_to_str(Origin o) {
    return o == Origin::TopLeft ? "topLeft" : "bottomLeft";
}
std::string type_to_str(TextureType t) {
    return t == TextureType::TwoD ? "2d" : "2darray";
}
std::string version_to_str(ContainerVersion v) {
    return v == ContainerVersion::V3 ? "v3" : "v6";
}

EncodeOptions build_encode_options(
    const std::string& target_game, int mipmaps, const py::object& block_size,
    const std::string& quality, const std::string& color_space,
    const std::string& color_format, const py::object& resize,
    const std::string& ideal_origin, const std::string& texture_type,
    const py::tuple& roughness_channel, const std::string& normal_map_fmt) {
    EncodeOptions o;
    o.target_game = parse_game(target_game);
    o.mipmaps     = mipmaps;
    o.block_size  = parse_block_size(block_size);
    o.quality     = parse_quality(quality);
    o.color_space = parse_color_space(color_space);
    o.color_format = parse_color_format(color_format);
    if (!resize.is_none()) {
        auto rp = resize.cast<std::pair<int, int>>();
        o.resize = rp;
    }
    o.ideal_origin = parse_origin(ideal_origin);
    o.texture_type = parse_texture_type(texture_type);
    for (int i = 0; i < 4 && i < static_cast<int>(roughness_channel.size()); ++i) {
        auto s = roughness_channel[i].cast<std::string>();
        o.roughness_channel[i] = s.empty() ? 'r' : s[0];
    }
    o.normal_map_format = parse_normal_fmt(normal_map_fmt);
    return o;
}

py::bytes encoder_py(
    const py::object& file, const py::object& files, const py::object& output,
    const std::string& target_game, int mipmaps, const py::object& block_size,
    const std::string& quality, const std::string& color_space,
    const std::string& color_format, const py::object& resize,
    const std::string& ideal_origin, const std::string& texture_type,
    const py::tuple& roughness_channel, const std::string& normal_map_format) {
    EncodeOptions opts = build_encode_options(
        target_game, mipmaps, block_size, quality, color_space, color_format,
        resize, ideal_origin, texture_type, roughness_channel, normal_map_format);

    std::vector<Image> imgs;
    if (!files.is_none()) {
        for (auto item : files.cast<py::sequence>()) {
            auto path = item.cast<std::string>();
            auto bytes = read_file(path);
            imgs.push_back(load_source_image(bytes.data(), bytes.size()));
        }
    } else if (!file.is_none()) {
        auto bytes = read_file(file.cast<std::string>());
        imgs.push_back(load_source_image(bytes.data(), bytes.size()));
    } else {
        throw std::invalid_argument("encoder: provide `file` or `files`");
    }

    std::vector<std::vector<std::uint8_t>> extras;
    auto blob = encode_many(imgs, opts, &extras);

    if (!output.is_none()) {
        write_file(output.cast<std::string>(), blob, true);
        return py::bytes();
    }
    return py::bytes(reinterpret_cast<const char*>(blob.data()), blob.size());
}

py::bytes decoder_py(
    const std::string& file, const py::object& output, const std::string& format,
    bool all_mipmaps, int mip_index, bool all_layers, int layer_index,
    const py::object& channels, bool undo_ideal_origin,
    const std::string& pattern) {
    (void)all_mipmaps; (void)all_layers; (void)pattern;
    auto bytes = read_file(file);
    auto img = decode(bytes.data(), bytes.size());

    DecodeWriteOptions wo;
    wo.format = parse_output_format(format);
    wo.mip_index = mip_index;
    wo.layer_index = layer_index;
    wo.undo_flip = undo_ideal_origin;
    if (!channels.is_none()) {
        std::string s;
        for (auto c : channels.cast<py::sequence>()) s += c.cast<std::string>()[0];
        while (s.size() < 4) s += '\0';
        for (int i = 0; i < 4; ++i) wo.channels[i] = s[i];
    }
    auto blob = write_image(img, wo);
    if (!output.is_none()) {
        write_file(output.cast<std::string>(), blob, true);
        return py::bytes();
    }
    return py::bytes(reinterpret_cast<const char*>(blob.data()), blob.size());
}

struct PyImage {
    Image inner;
    std::string color_format_str;
    std::string color_space_str;
    std::string origin_str;
    std::string type_str;
    std::string version_str;
    py::tuple channels;

    py::tuple size() const { return py::make_tuple(inner.width, inner.height); }
    py::tuple compression() const {
        return py::make_tuple(inner.compression.x, inner.compression.y);
    }
    py::bytes rawRGBA() const {
        if (inner.layers.empty() || inner.layers[0].empty()) return py::bytes();
        const auto& d = inner.layers[0][0].data;
        return py::bytes(reinterpret_cast<const char*>(d.data()), d.size());
    }
};

PyImage loader_py(const std::string& path) {
    auto bytes = read_file(path);
    auto img = decode(bytes.data(), bytes.size());
    PyImage p;
    p.inner = std::move(img);
    p.color_format_str = color_format_to_str(p.inner.color_format);
    p.color_space_str  = color_space_to_str(p.inner.color_space);
    p.origin_str       = origin_to_str(p.inner.origin);
    p.type_str         = type_to_str(p.inner.type);
    p.version_str      = version_to_str(p.inner.version);
    p.channels = py::make_tuple("r", "g", "b", "a");
    return p;
}

}  // namespace

PYBIND11_MODULE(_gstextconv_native, m) {
    m.doc() = "gstextconv — native C++ bindings (astcenc embedded, zlib via miniz)";
    m.attr("__version__") = std::string(library_version());
    m.attr("astcenc_version") = std::string(astcenc_version_string());

    py::class_<PyImage>(m, "Image")
        .def_readonly("colorFormat", &PyImage::color_format_str)
        .def_readonly("colorSpace",  &PyImage::color_space_str)
        .def_readonly("origin",      &PyImage::origin_str)
        .def_readonly("type",        &PyImage::type_str)
        .def_readonly("version",     &PyImage::version_str)
        .def_readonly("channels",    &PyImage::channels)
        .def_property_readonly("size", &PyImage::size)
        .def_property_readonly("compression", &PyImage::compression)
        .def_property_readonly("numMipmaps",
            [](const PyImage& i) { return i.inner.num_mipmaps; })
        .def_property_readonly("numLayers",
            [](const PyImage& i) { return i.inner.num_layers; })
        .def_property_readonly("rawRGBA", &PyImage::rawRGBA);

    m.def("encoder", &encoder_py,
          py::arg("file") = py::none(),
          py::arg("files") = py::none(),
          py::arg("output") = py::none(),
          py::arg("targetGame") = "fs23",
          py::arg("mipmaps") = 0,
          py::arg("blockSize") = py::make_tuple(6, 6),
          py::arg("quality") = "fast",
          py::arg("colorSpace") = "srgb",
          py::arg("colorFormat") = "rgba32",
          py::arg("resize") = py::none(),
          py::arg("idealOrigin") = "topLeft",
          py::arg("textureType") = "2d",
          py::arg("roughnessChannel") = py::make_tuple("r", "g", "b", "a"),
          py::arg("normalMapFormat") = "rgb");

    m.def("decoder", &decoder_py,
          py::arg("file"),
          py::arg("output") = py::none(),
          py::arg("format") = "png",
          py::arg("allMipmaps") = false,
          py::arg("mipIndex") = 0,
          py::arg("allLayers") = false,
          py::arg("layerIndex") = 0,
          py::arg("channels") = py::none(),
          py::arg("undoIdealOrigin") = true,
          py::arg("pattern") = "{filename}-{mipIndex}-{layerIndex}.{format}");

    m.def("loader", &loader_py, py::arg("file"));
}
