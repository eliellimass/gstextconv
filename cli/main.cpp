// SPDX-License-Identifier: MIT
// gstextconv CLI — statically linked executable implementing the commands
// described in `cli.md`.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gstextconv/gstextconv.hpp"

namespace fs = std::filesystem;
using namespace gstextconv;

namespace {

constexpr const char* kName        = "gstextconv";
constexpr const char* kDescription =
    "texture converter for Giants Engine / Farming Simulator assets";
constexpr const char* kDeveloper   = "SnowBit64";
constexpr const char* kSource      = "https://github.com/snowbit64/gstextconv.git";
constexpr const char* kLicenseType = "MIT";
constexpr const char* kWebsite     = "github.com/snowbit64/gstextconv";

// ---------------------------------------------------------------------------
// Help / info / license / version
// ---------------------------------------------------------------------------

void print_main_help() {
    std::cout <<
        "gstextconv <command> [options]\n"
        "\n"
        "Commands:\n"
        "  encoder             encode images into a GS2D (.ast) container\n"
        "  decoder             decode a GS2D (.ast) container into PNG/JPG/ASTC/raw\n"
        "  inspect             print texture metadata as JSON\n"
        "\n"
        "Global flags:\n"
        "  -h, --help          show this message (or per-command help)\n"
        "  -v, --version       print library version\n"
        "  -l, --license       print license text\n"
        "  -i, --info          print build metadata and capabilities\n"
        "\n"
        "Run `gstextconv <command> --help` for per-command options.\n";
}

void print_encoder_help() {
    std::cout <<
        "gstextconv encoder [options]\n"
        "\n"
        "Inputs (at least one required):\n"
        "  -f, --file <path>                   single source image (png/jpg/raw)\n"
        "  -b, --batch <path>                  add another source (repeatable)\n"
        "  -d, --dir <path>                    every supported image in a folder\n"
        "  -r, --recursive                     walk --dir recursively\n"
        "  -a, --raw-rgba                      treat inputs as raw RGBA (needs\n"
        "                                      --raw-width / --raw-height and\n"
        "                                      optional --raw-format)\n"
        "  -W, --raw-width <N>                 width for raw inputs\n"
        "  -H, --raw-height <N>                height for raw inputs\n"
        "  -F, --raw-format <fmt>              color format of raw inputs\n"
        "                                      (r8|rg16|rgb24|bgr24|rgba32|\n"
        "                                       bgra32|rgba64f|rgba128f)\n"
        "\n"
        "Encoding options:\n"
        "  -g, --target-game <fs20|fs23|fs26>  container version (default: fs23)\n"
        "  -m, --num-mipmaps <max|N>           mipmap count; N is ADDITIONAL\n"
        "                                      mip levels beyond the base image\n"
        "                                      (default: 0 = only the base mip)\n"
        "  -k, --block-size <NxM>              ASTC block size (default: 6x6)\n"
        "  -q, --quality <fast|medium|thorough>\n"
        "  -s, --color-space <srgb|linear|alpha>\n"
        "  -c, --color-format <fmt>            storage color format (see above)\n"
        "  -w, --resize <WxH>                  resize source before encode\n"
        "  -t, --texture-type <2d|2darray>\n"
        "  -n, --ideal-origin <topLeft|bottomLeft>\n"
        "  -rc, --roughness-channel <CCCC>     channel layout for roughness,\n"
        "                                      e.g. rgba or r,g,b,a\n"
        "  -nmp, --normal-map-format <rg|rgb>\n"
        "\n"
        "Output:\n"
        "  -o, --output <file>                 single-output path (only when one input)\n"
        "  -u, --output-dir <dir>              directory for batch outputs\n"
        "  -O, --overwrite                     overwrite existing files\n"
        "  -p, --preserve-file-path            write output next to the source (overrides -o/-u)\n"
        "  -x, --delete-source-file            delete the source file after a successful encode\n"
        "  -v, --verbose                       print a report for every processed file\n"
        "  -h, --help                          show this message\n";
}

void print_decoder_help() {
    std::cout <<
        "gstextconv decoder [<input>] [<output>] [options]\n"
        "\n"
        "Inputs (at least one required):\n"
        "  -f, --file <path>                   single GS2D (.ast/.gs2d) or\n"
        "                                      DDS (.dds) source file\n"
        "  -b, --batch <path>                  add another source (repeatable)\n"
        "  -d, --dir <path>                    every .ast/.gs2d/.dds in a folder\n"
        "  -r, --recursive                     walk --dir recursively\n"
        "\n"
        "DDS inputs are decoded directly (BC1/BC2/BC3/BC4/BC5/BC6H/BC7 and\n"
        "uncompressed RGB(A) / DXGI 10-13 half-float), bypassing the GS2D\n"
        "container.\n"
        "\n"
        "Decoding options:\n"
        "  -F, --format <png|jpg|astc|raw-rgba>\n"
        "                                      output format (default: infer\n"
        "                                      from -o extension or png)\n"
        "  -c, --channels <swizzle>            channel selection, e.g. 'rgba', 'r0b1'\n"
        "  -i, --mip-index <n>                 emit a specific mip level (default: 0)\n"
        "  -M, --all-mips                      emit every mip level (uses --pattern)\n"
        "  -l, --layer-index <n>               emit a specific array layer (default: 0)\n"
        "  -L, --all-layers                    emit every array layer (uses --pattern)\n"
        "  -g, --real-origin                   keep bottomLeft orientation (no auto-flip)\n"
        "  -P, --pattern <tpl>                 filename pattern for --all-mips/\n"
        "                                      --all-layers (default:\n"
        "                                      '{filename}-{mipIndex}-{layerIndex}.{format}')\n"
        "\n"
        "Output:\n"
        "  -o, --output <file>                 single-output path (extension picks format)\n"
        "  -u, --output-dir <dir>              directory for batch outputs\n"
        "  -O, --overwrite                     overwrite existing files\n"
        "  -p, --preserve-file-path            write output next to the source (overrides -o/-u)\n"
        "  -x, --delete-source-file            delete the source file after a successful decode\n"
        "  -v, --verbose                       print a report for every processed file\n"
        "  -h, --help                          show this message\n";
}

void print_inspect_help() {
    std::cout <<
        "gstextconv inspect [options]\n"
        "\n"
        "Inputs (at least one required):\n"
        "  -f, --file <path>                   single GS2D (.ast/.gs2d), DDS\n"
        "                                      (.dds), PNG or JPG file\n"
        "  -b, --batch <path>                  add another source (repeatable)\n"
        "  -d, --dir <path>                    every .ast/.gs2d/.dds/.png/.jpg\n"
        "                                      in a folder\n"
        "  -r, --recursive                     walk --dir recursively\n"
        "\n"
        "Field selectors (repeatable; default is --all):\n"
        "  -m, --num-mipmaps                   mipmap count\n"
        "  -l, --num-layers                    layer count\n"
        "  -c, --compression                   compression format (astc_NxM or uncompressed)\n"
        "  -s, --size                          width / height of the base mip\n"
        "  -i, --ideal-origin                  ideal origin recorded in the container\n"
        "  -S, --color-space                   color space (srgb/linear/alpha)\n"
        "  -n, --channels                      number of channels\n"
        "  -a, --all                           print every available field (default)\n"
        "\n"
        "Output:\n"
        "  -o, --output <path>                 JSON file (single input) or output directory\n"
        "                                      (multiple inputs, one .json per input)\n"
        "  -v, --verbose                       print a report for every processed file\n"
        "  -h, --help                          show this message\n"
        "\n"
        "With no --output the result is printed to stdout as a JSON array.\n";
}

void print_version() { std::cout << library_version() << std::endl; }

void print_license() {
    std::cout <<
        "gstextconv License\n"
        "Copyright (c) 2026 snowbit64 and contributors.\n"
        "\n"
        "The source code of this project is released under MIT-style terms\n"
        "(use, copy, modify, merge, publish, distribute, sublicense, sell),\n"
        "WITHOUT WARRANTY OF ANY KIND.\n"
        "\n"
        "IMPORTANT: the GS2D file format (.gs2d / .ast) and any assets stored\n"
        "inside it are proprietary works of their respective owners and are\n"
        "NOT covered by this license. This tool is provided strictly for\n"
        "interoperability; you are responsible for complying with the terms\n"
        "of service of the originating software and with the copyright of\n"
        "any asset you process. This project is not affiliated with, endorsed\n"
        "by, or sponsored by Garena, 111dots Studio, Sea Ltd., or any other\n"
        "rights holder of GS2D-based games.\n"
        "\n"
        "See the LICENSE file distributed with the source for the full text,\n"
        "including the list of third-party components (astc-encoder, stb,\n"
        "miniz, pybind11) and their individual licenses.\n" << std::endl;
}

void print_info() {
    std::cout <<
        "build release: " << build_release() << ";\n"
        "build date: " << build_date() << ";\n"
        "tool name: " << kName << ";\n"
        "tool description: " << kDescription << ";\n"
        "developer: " << kDeveloper << ";\n"
        "source: " << kSource << ";\n"
        "license type: " << kLicenseType << ";\n"
        "astc version: " << astcenc_version_string() << ";\n"
        "compression backend: astcenc;\n"
        "supported textures versions: v3, v6;\n"
        "supported formats input: ast, astc, raw-rgba, png, jpg;\n"
        "supported formats output: png, jpg, raw-rgba, astc, ast;\n"
        "supported platforms and architectures:\n"
        "    windows (x86, x64);\n"
        "    linux (x86, x64);\n"
        "    android (armv7, aarch64);\n"
        "cli support: yes;\n"
        "gui support: no;\n"
        "multithreading: yes;\n"
        "gpu acceleration: no;\n"
        "unicode paths: yes;\n"
        "batch conversion: yes;\n"
        "recursive folders: yes;\n"
        "max texture size: 16384x16384;\n"
        "alpha channel support: yes;\n"
        "mipmap support: yes;\n"
        "endianness support: little-endian;\n"
        "portable binary: yes;\n"
        "dependencies: none;\n"
        "static build: yes;\n"
        "exit codes:\n"
        "    0 success;\n"
        "    1 invalid file;\n"
        "    2 unsupported format;\n"
        "    3 conversion failed;\n"
        "website: " << kWebsite << ";\n"
        "issues: " << kWebsite << "/issues;\n";
}

// ---------------------------------------------------------------------------
// File I/O helpers
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> read_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw Error(Error::Code::InvalidFile, "cannot open " + path.string());
    f.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(size);
    if (size > 0 && !f.read(reinterpret_cast<char*>(data.data()), size)) {
        throw Error(Error::Code::InvalidFile, "read failure: " + path.string());
    }
    return data;
}

void write_file(const fs::path& path, const std::vector<std::uint8_t>& data) {
    fs::create_directories(path.parent_path().empty() ? fs::path{"."} : path.parent_path());
    std::ofstream f(path, std::ios::binary);
    if (!f) throw Error(Error::Code::ConversionFailed, "cannot open for write: " + path.string());
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
}

void write_text(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path().empty() ? fs::path{"."} : path.parent_path());
    std::ofstream f(path, std::ios::binary);
    if (!f) throw Error(Error::Code::ConversionFailed, "cannot open for write: " + path.string());
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
}

// ---------------------------------------------------------------------------
// Verbose reporting
// ---------------------------------------------------------------------------

struct VerboseClock {
    using clock = std::chrono::steady_clock;
    clock::time_point start = clock::now();
    [[nodiscard]] std::string elapsed_str() const {
        const auto delta = clock::now() - start;
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(delta).count();
        std::ostringstream os;
        os << std::fixed << std::setprecision(3)
           << (static_cast<double>(us) / 1000.0) << "ms";
        return os.str();
    }
};

void print_verbose_entry(std::size_t index,
                         const fs::path& filename,
                         const std::string& new_file,
                         const std::string& duration,
                         std::string_view process_type,
                         std::string_view status) {
    std::cout <<
        "gstextconv:\n"
        "\tindex: " << index << ";\n"
        "\tfilename: " << filename.generic_string() << ";\n"
        "\tnew file: " << new_file << ";\n"
        "\tprocess duration: " << duration << ";\n"
        "\tprocess type: " << process_type << ";\n"
        "\tstatus: " << status << ";\n";
}

// ---------------------------------------------------------------------------
// Enum parsers
// ---------------------------------------------------------------------------

OutputFormat parse_output_format(std::string_view s) {
    if (s == "png")       return OutputFormat::PNG;
    if (s == "jpg" || s == "jpeg") return OutputFormat::JPG;
    if (s == "astc")      return OutputFormat::ASTC;
    if (s == "raw-rgba" || s == "raw" || s == "rgba" || s == "raw-rgb") {
        return OutputFormat::RawRGBA;
    }
    throw Error(Error::Code::UnsupportedFormat, "unknown output format");
}

OutputFormat output_format_from_ext(const fs::path& p) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".png")  return OutputFormat::PNG;
    if (ext == ".jpg" || ext == ".jpeg") return OutputFormat::JPG;
    if (ext == ".astc") return OutputFormat::ASTC;
    if (ext == ".bin" || ext == ".raw") return OutputFormat::RawRGBA;
    throw Error(Error::Code::UnsupportedFormat, "cannot infer output format: " + ext);
}

std::string output_format_ext(OutputFormat f) {
    switch (f) {
        case OutputFormat::PNG:     return ".png";
        case OutputFormat::JPG:     return ".jpg";
        case OutputFormat::ASTC:    return ".astc";
        case OutputFormat::RawRGBA: return ".bin";
    }
    return ".png";
}

std::string output_format_name(OutputFormat f) {
    switch (f) {
        case OutputFormat::PNG:     return "png";
        case OutputFormat::JPG:     return "jpg";
        case OutputFormat::ASTC:    return "astc";
        case OutputFormat::RawRGBA: return "raw-rgba";
    }
    return "png";
}

BlockSize parse_block_size(std::string_view s) {
    auto sep = s.find('x');
    if (sep == std::string_view::npos) throw Error(Error::Code::UnsupportedFormat, "block-size");
    return {std::stoi(std::string(s.substr(0, sep))),
            std::stoi(std::string(s.substr(sep + 1)))};
}

Quality parse_quality(std::string_view s) {
    if (s == "fast") return Quality::Fast;
    if (s == "medium") return Quality::Medium;
    if (s == "thorough") return Quality::Thorough;
    throw Error(Error::Code::UnsupportedFormat, "quality");
}

ColorSpace parse_color_space(std::string_view s) {
    if (s == "srgb")   return ColorSpace::Srgb;
    if (s == "linear") return ColorSpace::Linear;
    if (s == "alpha")  return ColorSpace::Alpha;
    throw Error(Error::Code::UnsupportedFormat, "color-space");
}

ColorFormat parse_color_format(std::string_view s) {
    if (s == "r8")       return ColorFormat::R8;
    if (s == "rg16")     return ColorFormat::RG16;
    if (s == "rgb24")    return ColorFormat::RGB24;
    if (s == "bgr24")    return ColorFormat::BGR24;
    if (s == "rgba32")   return ColorFormat::RGBA32;
    if (s == "bgra32")   return ColorFormat::BGRA32;
    if (s == "rgba64f")  return ColorFormat::RGBA64F;
    if (s == "rgba128f") return ColorFormat::RGBA128F;
    throw Error(Error::Code::UnsupportedFormat, "color-format: " + std::string(s));
}

TargetGame parse_target_game(std::string_view s) {
    if (s == "fs20") return TargetGame::FS20;
    if (s == "fs23") return TargetGame::FS23;
    if (s == "fs26") return TargetGame::FS26;
    throw Error(Error::Code::UnsupportedFormat, "target-game");
}

NormalMapFormat parse_normal_map_format(std::string_view s) {
    if (s == "rg")  return NormalMapFormat::RG;
    if (s == "rgb") return NormalMapFormat::RGB;
    throw Error(Error::Code::UnsupportedFormat, "normal-map-format");
}

// Parse a list of channel letters separated by commas or a contiguous string
// (e.g. "r,g,b,a" or "rgba"). Stores up to 4 chars; pads missing slots with the
// previous RGBA default.
std::array<char, 4> parse_channel_list(std::string_view s,
                                       std::array<char, 4> def = {'r', 'g', 'b', 'a'}) {
    std::array<char, 4> out = def;
    std::size_t i = 0;
    for (std::size_t k = 0; k < s.size() && i < out.size(); ++k) {
        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[k])));
        if (c == ',' || c == ' ' || c == ';') continue;
        out[i++] = c;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Tiny argument parser (supports short/long flags and value-less switches).
// Each subcommand passes its own set of value-less flag names so short-flag
// collisions between subcommands don't break value-taking flags.
// ---------------------------------------------------------------------------

struct Args {
    std::vector<std::string> positional;
    std::vector<std::pair<std::string, std::string>> options;

    [[nodiscard]] bool has(std::initializer_list<std::string_view> names) const {
        for (const auto& [k, _] : options) {
            for (auto n : names) {
                if (k == n) return true;
            }
        }
        return false;
    }
    [[nodiscard]] std::optional<std::string> get(
        std::initializer_list<std::string_view> names) const {
        for (const auto& [k, v] : options) {
            for (auto n : names) if (k == n) return v;
        }
        return std::nullopt;
    }
    [[nodiscard]] std::vector<std::string> getall(
        std::initializer_list<std::string_view> names) const {
        std::vector<std::string> out;
        for (const auto& [k, v] : options) {
            for (auto n : names) if (k == n) out.push_back(v);
        }
        return out;
    }
};

Args parse_args(int argc, char** argv, int start,
                const std::unordered_set<std::string>& flag_names) {
    Args out;
    for (int i = start; i < argc; ++i) {
        std::string a = argv[i];
        if (a.size() >= 2 && a[0] == '-') {
            std::string name = a[1] == '-' ? a.substr(2) : a.substr(1);
            std::string value;
            const bool is_flag = flag_names.count(name) > 0;
            if (!is_flag && i + 1 < argc && argv[i + 1][0] != '-') {
                value = argv[++i];
            }
            out.options.emplace_back(std::move(name), std::move(value));
        } else {
            out.positional.push_back(std::move(a));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Per-subcommand value-less flag sets
// ---------------------------------------------------------------------------

const std::unordered_set<std::string>& encoder_flags() {
    static const std::unordered_set<std::string> s = {
        "r", "recursive", "O", "overwrite", "h", "help",
        "v", "verbose", "p", "preserve-file-path",
        "x", "delete-source-file", "a", "raw-rgba",
    };
    return s;
}

const std::unordered_set<std::string>& decoder_flags() {
    static const std::unordered_set<std::string> s = {
        "r", "recursive", "O", "overwrite", "h", "help",
        "v", "verbose", "p", "preserve-file-path",
        "x", "delete-source-file",
        "g", "real-origin",
        // NB: lowercase = pick a single index; UPPERCASE = "all of them".
        "M", "all-mips",
        "L", "all-layers",
    };
    return s;
}

const std::unordered_set<std::string>& inspect_flags() {
    static const std::unordered_set<std::string> s = {
        "r", "recursive", "h", "help", "v", "verbose",
        "m", "num-mipmaps", "l", "num-layers",
        "c", "compression", "s", "size",
        "i", "ideal-origin", "S", "color-space",
        "n", "channels", "a", "all",
    };
    return s;
}

// ---------------------------------------------------------------------------
// Directory walking for batch operations
// ---------------------------------------------------------------------------

void collect_from_dir(std::vector<fs::path>& out, const fs::path& root,
                      bool recurse,
                      std::initializer_list<std::string_view> extensions) {
    auto matches = [&](const fs::path& p) {
        auto ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        for (auto e : extensions) if (ext == e) return true;
        return false;
    };
    auto walk = [&](auto&& it) {
        for (const auto& e : it) {
            if (!e.is_regular_file()) continue;
            if (matches(e.path())) out.push_back(e.path());
        }
    };
    if (recurse) walk(fs::recursive_directory_iterator(root));
    else         walk(fs::directory_iterator(root));
}

std::vector<fs::path> collect_inputs(
    const Args& args,
    std::initializer_list<std::string_view> dir_extensions,
    bool use_positional = true,
    std::size_t positional_limit = std::string_view::npos) {
    std::vector<fs::path> inputs;
    if (auto f = args.get({"f", "file"})) inputs.emplace_back(*f);
    for (auto& b : args.getall({"b", "batch"})) inputs.emplace_back(b);
    if (use_positional) {
        for (std::size_t i = 0;
             i < args.positional.size() && i < positional_limit;
             ++i) {
            inputs.emplace_back(args.positional[i]);
        }
    }
    if (auto d = args.get({"d", "dir"})) {
        collect_from_dir(inputs, *d, args.has({"r", "recursive"}), dir_extensions);
    }
    return inputs;
}

// ---------------------------------------------------------------------------
// Filename pattern expansion (decoder --pattern)
// ---------------------------------------------------------------------------

std::string expand_pattern(std::string_view pattern,
                           const fs::path& source,
                           int mip_index,
                           int layer_index,
                           OutputFormat fmt) {
    const std::string filename = source.stem().string();
    const std::string format   = output_format_name(fmt);
    std::string out;
    out.reserve(pattern.size() + 16);
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{') {
            auto end = pattern.find('}', i);
            if (end == std::string_view::npos) { out.push_back(pattern[i]); continue; }
            auto key = pattern.substr(i + 1, end - i - 1);
            if      (key == "filename")   out += filename;
            else if (key == "mipIndex")   out += std::to_string(mip_index);
            else if (key == "layerIndex") out += std::to_string(layer_index);
            else if (key == "format")     out += format;
            else {
                out.append(pattern.begin() + static_cast<std::ptrdiff_t>(i),
                           pattern.begin() + static_cast<std::ptrdiff_t>(end + 1));
            }
            i = end;
        } else {
            out.push_back(pattern[i]);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Encoder command
// ---------------------------------------------------------------------------

int run_encoder(int argc, char** argv, int start) {
    auto args = parse_args(argc, argv, start, encoder_flags());
    if (args.has({"h", "help"})) { print_encoder_help(); return 0; }

    EncodeOptions opts;
    const bool raw_inputs   = args.has({"a", "raw-rgba"});
    const auto raw_w_opt    = args.get({"W", "raw-width"});
    const auto raw_h_opt    = args.get({"H", "raw-height"});
    const int raw_width     = raw_w_opt ? std::stoi(*raw_w_opt)  : 0;
    const int raw_height    = raw_h_opt ? std::stoi(*raw_h_opt)  : 0;
    ColorFormat raw_fmt     = ColorFormat::RGBA32;
    if (auto rf = args.get({"F", "raw-format"})) raw_fmt = parse_color_format(*rf);

    auto inputs = collect_inputs(args, {".png", ".jpg", ".jpeg", ".dds"});
    if (inputs.empty()) {
        throw Error(Error::Code::InvalidFile, "encoder: no inputs provided");
    }

    if (auto g = args.get({"g", "target-game"})) opts.target_game = parse_target_game(*g);
    const bool user_set_mipmaps = args.get({"m", "num-mipmaps", "mipmaps"}).has_value();
    if (auto m = args.get({"m", "num-mipmaps", "mipmaps"})) {
        if (*m == "max" || *m == "auto") {
            opts.mipmaps = -1;
        } else {
            opts.mipmaps = std::stoi(*m);
            if (opts.mipmaps < 0) opts.mipmaps = -1;
        }
    }
    const bool user_set_origin  = args.get({"n", "ideal-origin"}).has_value();
    const bool user_set_layers  = args.get({"t", "texture-type"}).has_value();
    if (auto b = args.get({"k", "block-size"})) opts.block_size = parse_block_size(*b);
    if (auto q = args.get({"q", "quality"}))     opts.quality = parse_quality(*q);
    if (auto s = args.get({"s", "color-space"})) opts.color_space = parse_color_space(*s);
    if (auto c = args.get({"c", "color-format"})) opts.color_format = parse_color_format(*c);
    if (auto w = args.get({"w", "resize"})) {
        auto sep = w->find('x');
        if (sep != std::string::npos) {
            opts.resize = std::pair{std::stoi(w->substr(0, sep)),
                                     std::stoi(w->substr(sep + 1))};
        }
    }
    if (auto t = args.get({"t", "texture-type"})) {
        opts.texture_type = (*t == "2darray") ? TextureType::TwoDArray
                                              : TextureType::TwoD;
    }
    if (auto n = args.get({"n", "ideal-origin"})) {
        opts.ideal_origin = (*n == "bottomLeft" || *n == "bottom-left")
                                ? Origin::BottomLeft
                                : Origin::TopLeft;
    }
    if (auto rc = args.get({"rc", "roughness-channel"})) {
        opts.roughness_channel = parse_channel_list(*rc, opts.roughness_channel);
    }
    if (auto nm = args.get({"nmp", "normal-map-format"})) {
        opts.normal_map_format = parse_normal_map_format(*nm);
    }

    const fs::path output_file = args.get({"o", "output"}).value_or("");
    const fs::path output_dir  = args.get({"u", "output-dir"}).value_or("");
    const bool overwrite       = args.has({"O", "overwrite"});
    const bool preserve_path   = args.has({"p", "preserve-file-path"});
    const bool delete_source   = args.has({"x", "delete-source-file"});
    const bool verbose         = args.has({"v", "verbose"});

    auto load_input = [&](const fs::path& in, EncodeOptions& per_file) -> Image {
        auto bytes = read_file(in);
        Image img;
        if (raw_inputs) {
            if (raw_width <= 0 || raw_height <= 0) {
                throw Error(Error::Code::UnsupportedFormat,
                            "--raw-rgba requires --raw-width and --raw-height");
            }
            img = load_source_image(bytes.data(), bytes.size(),
                                    raw_width, raw_height, raw_fmt);
        } else {
            img = load_source_image(bytes.data(), bytes.size());
        }
        // If the source already carries a mip chain or multiple array slices
        // (e.g. a DDS), inherit them by default — this is the "conversão dds
        // direta" path. The user can still override with -m or -t.
        const bool has_chain  = !img.layers.empty() && img.layers[0].size() > 1;
        const bool has_layers = img.layers.size() > 1;
        if (has_chain && !user_set_mipmaps) {
            per_file.inherit_mipmaps = true;
        }
        if (has_layers && !user_set_layers) {
            per_file.texture_type   = TextureType::TwoDArray;
            per_file.inherit_layers = true;
        } else if (has_layers) {
            per_file.inherit_layers = per_file.texture_type == TextureType::TwoDArray;
        }
        if (!user_set_origin) {
            per_file.ideal_origin = img.origin;  // preserve source orientation
        }
        return img;
    };

    if (!preserve_path && inputs.size() == 1 && !output_file.empty()) {
        const auto& in = inputs.front();
        VerboseClock clock;
        EncodeOptions per_file = opts;
        auto src = load_input(in, per_file);
        auto enc = encode(src, per_file);
        if (!overwrite && fs::exists(output_file)) {
            throw Error(Error::Code::ConversionFailed,
                        "output exists: " + output_file.string());
        }
        write_file(output_file, enc);
        if (delete_source && in != output_file && fs::exists(in)) fs::remove(in);
        if (verbose) {
            print_verbose_entry(1, in, output_file.generic_string(),
                                clock.elapsed_str(), "encoding", "success");
        }
        return 0;
    }

    std::size_t index = 0;
    for (const auto& in : inputs) {
        ++index;
        VerboseClock clock;
        fs::path out;
        if (preserve_path) {
            out = in;
            out.replace_extension(".ast");
        } else if (!output_dir.empty()) {
            out = output_dir / (in.stem().string() + ".ast");
        } else {
            out = in;
            out.replace_extension(".ast");
        }
        if (!overwrite && fs::exists(out)) {
            if (verbose) {
                print_verbose_entry(index, in, out.generic_string(),
                                    clock.elapsed_str(), "encoding", "skipped");
            }
            continue;
        }
        EncodeOptions per_file = opts;
        auto src = load_input(in, per_file);
        auto enc = encode(src, per_file);
        write_file(out, enc);
        if (delete_source && in != out && fs::exists(in)) fs::remove(in);
        if (verbose) {
            print_verbose_entry(index, in, out.generic_string(),
                                clock.elapsed_str(), "encoding", "success");
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Decoder command
// ---------------------------------------------------------------------------

int run_decoder(int argc, char** argv, int start) {
    auto args = parse_args(argc, argv, start, decoder_flags());
    if (args.has({"h", "help"})) { print_decoder_help(); return 0; }

    // Decoder supports `<input> <output>` as bare positional arguments.
    // Reserve the last positional as the output path when 2+ positionals
    // are present AND no -o/--output was given.
    fs::path pos_output;
    std::size_t positional_input_count = args.positional.size();
    const bool has_explicit_output =
        args.get({"o", "output"}).has_value() ||
        args.get({"u", "output-dir"}).has_value();
    if (!has_explicit_output && args.positional.size() >= 2) {
        pos_output = args.positional.back();
        positional_input_count = args.positional.size() - 1;
    }

    auto inputs = collect_inputs(args, {".ast", ".gs2d", ".dds"},
                                 /*use_positional=*/true,
                                 positional_input_count);
    if (inputs.empty()) throw Error(Error::Code::InvalidFile, "decoder: no inputs");

    fs::path output_file = args.get({"o", "output"}).value_or("");
    if (output_file.empty() && !pos_output.empty()) output_file = pos_output;
    const fs::path output_dir  = args.get({"u", "output-dir"}).value_or("");
    const bool overwrite       = args.has({"O", "overwrite"});
    const bool undo_flip       = !args.has({"g", "real-origin"});
    const bool all_mips        = args.has({"M", "all-mips"});
    const bool all_layers      = args.has({"L", "all-layers"});
    const std::string pattern  = args.get({"P", "pattern"}).value_or(
        "{filename}-{mipIndex}-{layerIndex}.{format}");

    DecodeWriteOptions dopts;
    dopts.undo_flip = undo_flip;
    if (auto c = args.get({"c", "channels"})) {
        auto parsed = parse_channel_list(*c, dopts.channels);
        dopts.channels = parsed;
        // Fill trailing slots that the user did not specify with '\0' so the
        // writer emits the right channel count.
        std::size_t written = 0;
        for (char ch : *c) {
            if (ch == ',' || ch == ' ' || ch == ';') continue;
            ++written;
        }
        for (std::size_t i = written; i < dopts.channels.size(); ++i) {
            dopts.channels[i] = '\0';
        }
    }
    const int fixed_mip   = args.get({"i", "mip-index"})   ? std::stoi(*args.get({"i", "mip-index"}))   : 0;
    const int fixed_layer = args.get({"l", "layer-index"}) ? std::stoi(*args.get({"l", "layer-index"})) : 0;
    dopts.mip_index   = fixed_mip;
    dopts.layer_index = fixed_layer;

    std::optional<OutputFormat> explicit_format;
    if (auto f = args.get({"F", "format"})) explicit_format = parse_output_format(*f);

    const bool preserve_path = args.has({"p", "preserve-file-path"});
    const bool delete_source = args.has({"x", "delete-source-file"});
    const bool verbose       = args.has({"v", "verbose"});

    auto resolve_format = [&](const fs::path& out) -> OutputFormat {
        if (explicit_format) return *explicit_format;
        try { return output_format_from_ext(out); } catch (...) {}
        return OutputFormat::PNG;
    };

    auto pattern_output = [&](const fs::path& in, int mip_i, int layer_i,
                              OutputFormat fmt) -> fs::path {
        fs::path base;
        if (preserve_path) base = in.parent_path();
        else if (!output_dir.empty()) base = output_dir;
        else if (!output_file.empty() && fs::is_directory(output_file)) base = output_file;
        else base = in.parent_path();
        return base / expand_pattern(pattern, in, mip_i, layer_i, fmt);
    };

    std::size_t index = 0;
    for (const auto& in : inputs) {
        ++index;
        VerboseClock clock;
        auto bytes = read_file(in);
        auto img = decode(bytes.data(), bytes.size());

        // Determine which mips/layers to emit.
        std::vector<int> mip_indices;
        std::vector<int> layer_indices;
        if (all_mips) {
            const int n = std::max(1, img.num_mipmaps);
            for (int i = 0; i < n; ++i) mip_indices.push_back(i);
        } else {
            mip_indices.push_back(fixed_mip);
        }
        if (all_layers) {
            const int n = std::max(1, img.num_layers);
            for (int i = 0; i < n; ++i) layer_indices.push_back(i);
        } else {
            layer_indices.push_back(fixed_layer);
        }

        const bool multi_out =
            mip_indices.size() > 1 || layer_indices.size() > 1;
        bool any_written = false;
        for (int layer_i : layer_indices) {
            for (int mip_i : mip_indices) {
                fs::path out;
                OutputFormat fmt;
                if (multi_out) {
                    fmt = explicit_format.value_or(OutputFormat::PNG);
                    out = pattern_output(in, mip_i, layer_i, fmt);
                } else if (preserve_path) {
                    out = in;
                    fmt = explicit_format.value_or(OutputFormat::PNG);
                    out.replace_extension(output_format_ext(fmt));
                } else if (inputs.size() == 1 && !output_file.empty() &&
                           !(fs::exists(output_file) && fs::is_directory(output_file))) {
                    out = output_file;
                    fmt = resolve_format(out);
                } else if (!output_dir.empty() ||
                           (!output_file.empty() && fs::exists(output_file) &&
                            fs::is_directory(output_file))) {
                    fmt = explicit_format.value_or(OutputFormat::PNG);
                    fs::path base = !output_dir.empty() ? output_dir : output_file;
                    out = base / (in.stem().string() + output_format_ext(fmt));
                } else {
                    out = in;
                    fmt = explicit_format.value_or(OutputFormat::PNG);
                    out.replace_extension(output_format_ext(fmt));
                }
                dopts.format      = fmt;
                dopts.mip_index   = mip_i;
                dopts.layer_index = layer_i;

                if (!overwrite && fs::exists(out)) {
                    if (verbose) {
                        print_verbose_entry(index, in, out.generic_string(),
                                            clock.elapsed_str(), "decoding",
                                            "skipped");
                    }
                    continue;
                }
                auto blob = write_image(img, dopts);
                write_file(out, blob);
                any_written = true;
                if (verbose) {
                    print_verbose_entry(index, in, out.generic_string(),
                                        clock.elapsed_str(), "decoding",
                                        "success");
                }
            }
        }
        if (delete_source && any_written && fs::exists(in)) fs::remove(in);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Inspect command
// ---------------------------------------------------------------------------

std::string_view color_space_name(ColorSpace c) {
    switch (c) {
        case ColorSpace::Srgb:   return "srgb";
        case ColorSpace::Linear: return "linear";
        case ColorSpace::Alpha:  return "alpha";
    }
    return "unknown";
}

std::string_view origin_name(Origin o) {
    return o == Origin::BottomLeft ? "bottomLeft" : "topLeft";
}

std::string_view color_format_name(ColorFormat f) {
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
    return "unknown";
}

int channel_count(ColorFormat f) {
    switch (f) {
        case ColorFormat::R8:       return 1;
        case ColorFormat::RG16:     return 2;
        case ColorFormat::RGB24:
        case ColorFormat::BGR24:    return 3;
        case ColorFormat::RGBA32:
        case ColorFormat::BGRA32:
        case ColorFormat::RGBA64F:
        case ColorFormat::RGBA128F: return 4;
    }
    return 0;
}

std::string_view container_name(ContainerVersion v) {
    return v == ContainerVersion::V3 ? "v3" : "v6";
}

std::string compression_string(const Image& img) {
    if (img.compression.x <= 0 || img.compression.y <= 0) return "uncompressed";
    std::ostringstream os;
    os << "astc_" << img.compression.x << "x" << img.compression.y;
    return os.str();
}

// Minimal JSON string escaper (ASCII + common escapes).
std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(c) & 0xff);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

struct InspectFields {
    bool num_mipmaps  = false;
    bool num_layers   = false;
    bool compression  = false;
    bool size         = false;
    bool ideal_origin = false;
    bool color_space  = false;
    bool channels     = false;

    [[nodiscard]] bool any() const {
        return num_mipmaps || num_layers || compression || size ||
               ideal_origin || color_space || channels;
    }
    void enable_all() {
        num_mipmaps = num_layers = compression = size =
        ideal_origin = color_space = channels = true;
    }
};

std::string build_inspect_json(const fs::path& file, const Image& img,
                               const InspectFields& fields, int indent = 2) {
    const std::string pad(indent, ' ');
    std::ostringstream os;
    os << "{\n";
    os << pad << json_escape("file") << ": " << json_escape(file.generic_string());
    if (fields.size) {
        os << ",\n" << pad << json_escape("width")  << ": " << img.width;
        os << ",\n" << pad << json_escape("height") << ": " << img.height;
    }
    if (fields.num_mipmaps) {
        os << ",\n" << pad << json_escape("num_mipmaps") << ": " << img.num_mipmaps;
    }
    if (fields.num_layers) {
        os << ",\n" << pad << json_escape("num_layers")  << ": " << img.num_layers;
    }
    if (fields.compression) {
        os << ",\n" << pad << json_escape("compression") << ": "
           << json_escape(compression_string(img));
    }
    if (fields.color_space) {
        os << ",\n" << pad << json_escape("color_space") << ": "
           << json_escape(color_space_name(img.color_space));
    }
    if (fields.ideal_origin) {
        os << ",\n" << pad << json_escape("ideal_origin") << ": "
           << json_escape(origin_name(img.origin));
    }
    if (fields.channels) {
        os << ",\n" << pad << json_escape("channels") << ": "
           << channel_count(img.color_format);
        os << ",\n" << pad << json_escape("color_format") << ": "
           << json_escape(color_format_name(img.color_format));
    }
    // Always include container version + texture type; they are cheap and
    // useful context for any inspection output.
    os << ",\n" << pad << json_escape("container_version") << ": "
       << json_escape(container_name(img.version));
    os << ",\n" << pad << json_escape("texture_type") << ": "
       << json_escape(img.type == TextureType::TwoDArray ? "2darray" : "2d");
    os << "\n}";
    return os.str();
}

int run_inspect(int argc, char** argv, int start) {
    auto args = parse_args(argc, argv, start, inspect_flags());
    if (args.has({"h", "help"})) { print_inspect_help(); return 0; }

    auto inputs = collect_inputs(args, {".ast", ".gs2d", ".dds", ".png", ".jpg", ".jpeg"});
    if (inputs.empty()) throw Error(Error::Code::InvalidFile, "inspect: no inputs");

    InspectFields fields;
    if (args.has({"a", "all"})) fields.enable_all();
    if (args.has({"m", "num-mipmaps"}))  fields.num_mipmaps  = true;
    if (args.has({"l", "num-layers"}))   fields.num_layers   = true;
    if (args.has({"c", "compression"}))  fields.compression  = true;
    if (args.has({"s", "size"}))         fields.size         = true;
    if (args.has({"i", "ideal-origin"})) fields.ideal_origin = true;
    if (args.has({"S", "color-space"}))  fields.color_space  = true;
    if (args.has({"n", "channels"}))     fields.channels     = true;
    if (!fields.any()) fields.enable_all();

    const fs::path output = args.get({"o", "output"}).value_or("");
    const bool verbose = args.has({"v", "verbose"});

    // Single input
    if (inputs.size() == 1) {
        const auto& in = inputs.front();
        VerboseClock clock;
        auto bytes = read_file(in);
        auto img = load(bytes.data(), bytes.size());
        auto json = build_inspect_json(in, img, fields);
        std::string new_file_label;
        if (output.empty()) {
            std::cout << json << "\n";
            new_file_label = "<stdout>";
        } else {
            fs::path out = output;
            if (fs::exists(output) && fs::is_directory(output)) {
                out = output / (in.stem().string() + ".json");
            }
            write_text(out, json + "\n");
            new_file_label = out.generic_string();
        }
        if (verbose) {
            print_verbose_entry(1, in, new_file_label,
                                clock.elapsed_str(), "inspection", "success");
        }
        return 0;
    }

    // Multiple inputs
    if (!output.empty()) {
        // Treat --output as a directory; create it if missing.
        fs::create_directories(output);
        std::size_t index = 0;
        for (const auto& in : inputs) {
            ++index;
            VerboseClock clock;
            auto bytes = read_file(in);
            auto img = load(bytes.data(), bytes.size());
            auto json = build_inspect_json(in, img, fields);
            const fs::path out = output / (in.stem().string() + ".json");
            write_text(out, json + "\n");
            if (verbose) {
                print_verbose_entry(index, in, out.generic_string(),
                                    clock.elapsed_str(), "inspection", "success");
            }
        }
        return 0;
    }

    // No --output, multiple inputs -> one JSON array to stdout.
    std::vector<VerboseClock> clocks;
    clocks.reserve(inputs.size());
    std::cout << "[\n";
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        clocks.emplace_back();
        auto bytes = read_file(inputs[i]);
        auto img = load(bytes.data(), bytes.size());
        auto json = build_inspect_json(inputs[i], img, fields, 4);
        // Indent the object by two spaces for readability in the array.
        std::istringstream is(json);
        std::string line;
        bool first_line = true;
        while (std::getline(is, line)) {
            if (!first_line) std::cout << "\n";
            std::cout << "  " << line;
            first_line = false;
        }
        if (i + 1 < inputs.size()) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "]\n";
    if (verbose) {
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            print_verbose_entry(i + 1, inputs[i], "<stdout>",
                                clocks[i].elapsed_str(),
                                "inspection", "success");
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { print_main_help(); return 0; }
    std::string cmd = argv[1];
    try {
        // Global help / info / license / version flags take precedence over
        // commands so they can appear anywhere on the command line.
        if (cmd == "-h" || cmd == "--help")    { print_main_help();    return 0; }
        if (cmd == "-v" || cmd == "--version") { print_version();      return 0; }
        if (cmd == "-l" || cmd == "--license") { print_license();      return 0; }
        if (cmd == "-i" || cmd == "--info")    { print_info();         return 0; }

        if (cmd == "encoder") return run_encoder(argc, argv, 2);
        if (cmd == "decoder") return run_decoder(argc, argv, 2);
        if (cmd == "inspect") return run_inspect(argc, argv, 2);

        std::cerr << "unknown command: " << cmd << "\n";
        print_main_help();
        return 2;
    } catch (const Error& e) {
        std::cerr << kName << ": " << e.what() << std::endl;
        return static_cast<int>(e.code());
    } catch (const std::exception& e) {
        std::cerr << kName << ": " << e.what() << std::endl;
        return 3;
    }
}
