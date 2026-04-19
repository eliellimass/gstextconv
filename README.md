# gstextconv

Tool and library to read, write, and inspect **GS2D** textures  
(the `.ast` / `.gs2d` container used in the Farming Simulator franchise on portable versions).  
`gstextconv` is an **open-source** project, but the GS2D format itself  
**is not open** — see the [License](#license) section.

- Static CLI: `gstextconv` (Windows x64, Android aarch64).
- Python library (pybind11): `gstextconv` (`cp313-win_amd64` and `cp313-android_aarch64` wheels).
- [`astcenc`](https://github.com/ARM-software/astc-encoder) built-in — no dependency on external binaries for ASTC encoding/decoding.

## Installation

### CLI

Download the binary for your platform from the Releases tab, or build from source:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/gstextconv --help

For Windows x64 (MSVC) and Android aarch64 (NDK), the GitHub Actions jobs in .github/workflows/build.yml show the exact commands.

Python Library

pip install gstextconv                     # when published on PyPI
# or from the repo:
pip install .

Requires Python 3.13.

Usage — CLI

gstextconv [global flags] <subcommand> [options]

global flags:
  -h, --help     show help
  -v, --version  print version
  -l, --license  print full license
  -i, --info     print build metadata

Subcommands: encoder, decoder, inspect. Each accepts -h/--help.

gstextconv encoder

Converts .png / .jpg → .ast container with ASTC (or an uncompressed format supported by GS2D).

# single file
gstextconv encoder -f texture.png -o texture.ast

# batch: all PNGs in a folder, overwrite enabled
gstextconv encoder -d ./in -u ./out -O

# save next to original file and remove source
gstextconv encoder -f texture.png -p -x

Main flags:

flag	description

-f/--file, -b/--batch, -d/--dir, -r/--recursive	inputs
-g/--target-game <fs20|fs23>	container format to generate
-b/--block-size <NxM>	ASTC block size (4x4 … 12x12)
-q/--quality <fast|medium|thorough>	astcenc preset
-s/--color-space <srgb|linear|alpha>	color space
-w/--resize <WxH>	resize before encoding
-t/--texture-type <2d|2darray>	texture type
-n/--ideal-origin <topLeft|bottomLeft>	stored Y-axis origin
-o/--output, -u/--output-dir, -O/--overwrite	output
-p/--preserve-file-path	save next to original file
-x/--delete-source-file	delete source file after success
-v/--verbose	print report for each file (see below)


gstextconv decoder

Converts .ast / .gs2d → .png (or another format supported by the extension passed to -o).

# decode a texture and save PNG next to it
gstextconv decoder -f texture.ast -p

# extract a specific layer from a 2D array
gstextconv decoder -f array.ast -L 3 -o layer3.png

# recursive batch mode
gstextconv decoder -d ./in -r -u ./png -O

Main flags:

flag	description

-c/--channels <swizzle>	e.g. rgba, r0b1
-i/--mip-index <n>	specific mip (default 0)
-L/--layer-index <n>	specific layer (default 0)
-g/--real-origin	preserve bottomLeft (no automatic flip)
-p/--preserve-file-path, -x/--delete-source-file, -v/--verbose	same as encoder


gstextconv inspect

Prints container metadata as JSON. By default it outputs pretty JSON to stdout; with -o it writes to a file (or a directory in batch mode).

gstextconv inspect -f texture.ast -a              # print everything
gstextconv inspect -d ./in -r -a -o ./json       # one .json per texture
gstextconv inspect -f texture.ast -c -s -n       # compression, size, channels only

Selectors (can be combined; if none are passed, equivalent to -a):

flag	field

-m/--num-mipmaps	number of mipmaps
-l/--num-layers	number of layers
-c/--compression	compression (astc_NxM or uncompressed)
-s/--size	base width and height
--ideal-origin	stored ideal origin
--color-space	srgb / linear / alpha
-n/--channels	number of channels and color_format
-a/--all	all fields above


The JSON always includes container_version and texture_type.

--verbose Output

Passing -v/--verbose to any subcommand reports each processed file in this format:

gstextconv:
    index: 1;
    filename: in/texture.ast;
    new file: in/texture.png;
    process duration: 28.085ms;
    process type: decoding;
    status: success;

status may be success or skipped (when destination already exists and -O was not passed).
process type is encoding, decoding, or inspection.

Usage — Python Library

import gstextconv

# decode
img = gstextconv.decode(open("texture.ast", "rb").read())
img.width, img.height, img.num_mipmaps, img.num_layers
img.mip(0).layer(0).pixels  # RGBA bytes for mip/layer

# inspect
meta = gstextconv.inspect(open("texture.ast", "rb").read())
print(meta.compression, meta.color_space, meta.ideal_origin)

# encode
png_bytes = open("texture.png", "rb").read()
ast = gstextconv.encode(
    png_bytes,
    target_game="fs23",
    block_size=(6, 6),
    quality="medium",
    color_space="srgb",
)
open("texture.ast", "wb").write(ast)

The exact API, field names, and enums are in:

python/gstextconv/__init__.py

Supported Formats

Container	Version	Support

GS2D / FS20	v3, v4	read + write
GS2D / FS23	v6	read + write


Tested ASTC compressions: 4x4, 5x4, 5x5, 6x5, 6x6, 8x5, 8x6, 8x8, 10x5,
10x6, 10x8, 10x10, 12x10, 12x12 (all via built-in astcenc).

Uncompressed formats: r8, rg16, rgb24, bgr24, rgba32, bgra32,
rgba64f, rgba128f.

Validation sweep (tools/sweep_decode.py) passes 8651/8653 samples from samples/fs20/gs2d/ and samples/fs23/gs2d/ (the remaining 2 are zero-byte .ast files).

Architecture

cli/main.cpp          # gstextconv executable
include/gstextconv/   # public C++ API
src/                  # container, codec, astc_bridge, image_io, ...
python/_bindings.cpp  # pybind11 → _gstextconv_native module
python/gstextconv/    # Python facade
third_party/          # astcenc, miniz, stb (vendored)
tools/sweep_decode.py # mass validation
samples/              # FS20 / FS23 samples

License

See LICENSE.

Plain-language summary:

The source code of this project is released under an MIT-style license — use, modify, and redistribute freely.

The GS2D / .ast / .gs2d format is proprietary work owned by third parties. This project exists for interoperability purposes, and the source code license does not grant any rights over the format itself, nor over any game assets/textures that use it.

You are solely responsible for ensuring that use of this tool with any file complies with the original software terms, asset copyrights, and applicable law.

This project is not affiliated with or endorsed by Giants Software, or any other rights holder of the format or games that use it.


Contributing

Pull requests are welcome. Before opening a larger PR, open an issue describing the problem / proposal. Keep changes focused and accompanied by test samples whenever possible.