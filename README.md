<div align="center">

# gstextconv

**A fast, self-contained CLI and Python library for GS2D textures.**

Read, write, and inspect the `.ast` / `.gs2d` texture container used by the
portable builds of the *Farming Simulator* franchise — with built-in
[astcenc][astcenc] for ASTC encoding/decoding and no external runtime
dependencies.

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg?logo=cplusplus)](CMakeLists.txt)
[![Python 3.13](https://img.shields.io/badge/python-3.13-3776AB.svg?logo=python&logoColor=white)](pyproject.toml)
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20Android-informational)](.github/workflows/build.yml)

</div>

> [!IMPORTANT]
> `gstextconv` itself is **open source**. The **GS2D container format is
> proprietary** and not owned by this project. See [License](#license) for the
> full interoperability disclaimer.

---

## Table of Contents

- [Highlights](#highlights)
- [Installation](#installation)
  - [CLI](#cli)
  - [Python library](#python-library)
- [CLI Usage](#cli-usage)
  - [`encoder`](#encoder)
  - [`decoder`](#decoder)
  - [`inspect`](#inspect)
  - [Verbose output](#verbose-output)
- [Python API](#python-api)
- [Supported Formats](#supported-formats)
- [Repository Layout](#repository-layout)
- [Contributing](#contributing)
- [License](#license)

---

## Highlights

- **Static CLI** — single binary for Windows x64 and Android aarch64, no DLLs
  or shared runtime.
- **Python 3.13 bindings** — `cp313-win_amd64` and `cp313-android_aarch64`
  wheels (pybind11).
- **Built-in astcenc** — ASTC 4×4 … 12×12 encoding and decoding without any
  external tool.
- **Wide DDS input** — BC1–BC7, BC6H HDR, DXGI `R16G16B16A16_FLOAT` (code 10),
  `R32G32B32A32_FLOAT`, `B5G6R5`, `B5G5R5A1`, and the DX9 FourCC variants.
- **Multi-layer textures** — full 2D-array / cubemap round-trip with
  per-layer selection in the decoder (`-L <index>` / `-l`).
- **Batch & recursive** — process a single file, a flat directory, or an
  entire tree with `-d -r`.
- **Deterministic verbose report** — machine-parsable status line per file.

---

## Installation

### CLI

Download a prebuilt binary from the [Releases][releases] tab, or build from
source:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/gstextconv --help
```

The exact build commands used for the Windows x64 (MSVC) and Android aarch64
(NDK) jobs are in [`.github/workflows/build.yml`](.github/workflows/build.yml).

### Python library

```bash
pip install gstextconv               # once published on PyPI
# or from a checkout:
pip install .
```

Requires **Python 3.13**.

---

## CLI Usage

```
gstextconv [global flags] <subcommand> [options]
```

Global flags:

| Flag | Description |
| :--- | :--- |
| `-h`, `--help` | Show help (global or per subcommand) |
| `-v`, `--version` | Print the library version |
| `-l`, `--license` | Print the full license text |
| `-i`, `--info` | Print build metadata and capabilities |

Subcommands: [`encoder`](#encoder), [`decoder`](#decoder),
[`inspect`](#inspect). Each accepts `-h` / `--help`.

### `encoder`

Convert `.png` / `.jpg` / `.dds` → `.ast` container, using ASTC compression
(or one of the uncompressed formats supported by GS2D).

```bash
# single file
gstextconv encoder -f texture.png -o texture.ast

# batch: every PNG in a folder, overwrite enabled
gstextconv encoder -d ./in -u ./out -O

# save next to the original file and remove the source
gstextconv encoder -f texture.png -p -x
```

| Flag | Description |
| :--- | :--- |
| `-f`, `-b`, `-d`, `-r` | Inputs (file / batch / directory / recursive) |
| `-g`, `--target-game <fs20\|fs23>` | Container format to generate |
| `-b`, `--block-size <NxM>` | ASTC block size (`4x4` … `12x12`) |
| `-q`, `--quality <fast\|medium\|thorough>` | astcenc preset |
| `-s`, `--color-space <srgb\|linear\|alpha>` | Color space |
| `-w`, `--resize <WxH>` | Resize before encoding |
| `-t`, `--texture-type <2d\|2darray>` | Texture type |
| `-n`, `--ideal-origin <topLeft\|bottomLeft>` | Stored Y-axis origin |
| `-o`, `-u`, `-O` | Output file / directory / overwrite |
| `-p`, `--preserve-file-path` | Save next to the original file |
| `-x`, `--delete-source-file` | Delete source after a successful write |
| `-v`, `--verbose` | Per-file report (see [below](#verbose-output)) |

### `decoder`

Convert `.ast` / `.gs2d` **or `.dds`** → `.png` (or any format picked by
`-F` / `-o`'s extension). DDS inputs are decoded directly without
going through the GS2D container — BC1/BC2/BC3/BC4/BC5/BC6H/BC7 and
uncompressed RGB(A) / DXGI 10–13 half-float are all handled.

```bash
# decode a DDS straight to PNG, without an intermediate .ast
gstextconv decoder -f texture.dds -F png -o texture.png -O

# pick a specific layer + mip out of a DDS array
gstextconv decoder -f array.dds -F png -c rgb -L 0 -i 0 -o layer0.png

# decode and save PNG next to the .ast
gstextconv decoder -f texture.ast -p

# extract every layer of an array at once
gstextconv decoder -f array.ast -l -u ./out -O

# recursive batch decode
gstextconv decoder -d ./in -r -u ./png -O
```

| Flag | Description |
| :--- | :--- |
| `-F`, `--format <png\|jpg\|astc\|raw-rgba>` | Output format (default: infer from `-o` or `png`) |
| `-c`, `--channels <swizzle>` | Channel remap, e.g. `rgba`, `r0b1` |
| `-i`, `--mip-index <n>` | Specific mip level (default `0`) |
| `-m`, `--all-mips` | Extract every mip level |
| `-L`, `--layer-index <n>` | Specific layer (default `0`) |
| `-l`, `--all-layers` | Extract every layer of a `2darray` |
| `-P`, `--pattern <tpl>` | Filename template for `-m`/`-l` |
| `-g`, `--real-origin` | Preserve stored `bottomLeft` (no auto-flip) |
| `-p`, `-x`, `-v` | Same semantics as in `encoder` |

### `inspect`

Print container metadata as JSON. By default pretty JSON is written to
`stdout`; with `-o` it is written to a file, or to a directory in batch
mode.

```bash
gstextconv inspect -f texture.ast -a               # everything
gstextconv inspect -d ./in -r -a -o ./json         # one .json per texture
gstextconv inspect -f texture.ast -c -s -n         # a subset of fields
```

`inspect` also accepts `.dds`, `.png` and `.jpg` directly — useful
for reporting the mip / layer count of a DDS array without first
encoding it to `.ast`.

Field selectors (combine freely — omitting them is equivalent to `-a`):

| Flag | Field |
| :--- | :--- |
| `-m`, `--num-mipmaps` | Number of mipmaps |
| `-l`, `--num-layers` | Number of layers |
| `-c`, `--compression` | Compression (`astc_NxM` or `uncompressed`) |
| `-s`, `--size` | Base width and height |
| `-i`, `--ideal-origin` | Stored ideal origin |
| `-S`, `--color-space` | `srgb` / `linear` / `alpha` |
| `-n`, `--channels` | Number of channels and `color_format` |
| `-a`, `--all` | All fields above |

The JSON output always includes `container_version` and `texture_type`.

### Verbose output

Passing `-v` / `--verbose` to any subcommand emits one machine-parsable
record per file:

```
gstextconv:
    index: 1;
    filename: in/texture.ast;
    new file: in/texture.png;
    process duration: 28.085ms;
    process type: decoding;
    status: success;
```

- `status` — `success` or `skipped` (destination exists and `-O` was not
  passed).
- `process type` — `encoding`, `decoding`, or `inspection`.

---

## Python API

```python
import gstextconv

# decode
img = gstextconv.decode(open("texture.ast", "rb").read())
img.width, img.height, img.num_mipmaps, img.num_layers
img.mip(0).layer(0).pixels        # RGBA bytes for that mip / layer

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
```

The authoritative API — field names, enums, and defaults — lives in
[`python/gstextconv/__init__.py`](python/gstextconv/__init__.py).

---

## Supported Formats

### GS2D containers

| Container | Version | Support |
| :--- | :--- | :--- |
| GS2D / FS20 | `v3`, `v4` | read + write |
| GS2D / FS23 | `v6` | read + write |

### ASTC block sizes

`4x4`, `5x4`, `5x5`, `6x5`, `6x6`, `8x5`, `8x6`, `8x8`, `10x5`, `10x6`,
`10x8`, `10x10`, `12x10`, `12x12` — all via the embedded astcenc backend.

### Uncompressed color formats

`r8`, `rg16`, `rgb24`, `bgr24`, `rgba32`, `bgra32`, `rgba64f`, `rgba128f`.

### DDS inputs accepted by `encoder`

| DXGI / FourCC | Format | Notes |
| :--- | :--- | :--- |
| 2 | `R32G32B32A32_FLOAT` | Clamped to `[0, 1]` |
| 10 | `R16G16B16A16_FLOAT` | Half-float → unorm8 |
| 11 / 13 | `R16G16B16A16_UNORM` / `_SNORM` | |
| 28 / 29 | `R8G8B8A8_UNORM` / `_SRGB` | |
| 70–72 | `BC1` | (DXT1) |
| 73–75 | `BC2` | (DXT3) |
| 76–78 | `BC3` | (DXT5) |
| 79–81 | `BC4` | |
| 82–84 | `BC5` | |
| 85 / 86 | `B5G6R5` / `B5G5R5A1` | |
| 87 / 91 | `B8G8R8A8` | |
| 88 | `B8G8R8X8` | |
| 94–96 | `BC6H_UF16` / `BC6H_SF16` | HDR, Reinhard-tonemapped |
| 97–99 | `BC7` | |
| FourCC | `DXT1`–`DXT5`, `ATI1/BC4U/BC4S`, `ATI2/BC5U/BC5S` | Legacy DX9 headers |

Block decoding for BC2/BC5/BC6H/BC7 uses the MIT-licensed
[`iOrange/bcdec`](https://github.com/iOrange/bcdec), vendored under
`third_party/bcdec/`.

### Validation

`tools/sweep_decode.py` currently passes **8651 / 8653** samples from
`samples/fs20/gs2d/` and `samples/fs23/gs2d/` (the remaining two are
zero-byte `.ast` files and are expected to fail).

---

## Repository Layout

```
cli/main.cpp              CLI entry point
include/gstextconv/       Public C++ API
src/                      container, codec, astc_bridge, image_io, dds_io, ...
python/_bindings.cpp      pybind11 → _gstextconv_native module
python/gstextconv/        Python facade
third_party/              astcenc, miniz, stb, bcdec (all vendored)
tools/sweep_decode.py     Mass-validation harness
samples/                  FS20 / FS23 reference textures
```

---

## Contributing

Pull requests are welcome. Before opening a larger PR, please file an issue
describing the problem or proposal. Keep changes focused and, whenever
possible, accompany them with test samples.

---

## License

The source code of this project is released under an **MIT-style license**
(see [`LICENSE`](LICENSE)) — use, modify, and redistribute freely.

The **GS2D / `.ast` / `.gs2d` format is proprietary** work owned by third
parties. This project exists for interoperability purposes; the source
license **does not grant any rights** over the format itself, nor over any
game assets / textures that use it.

You are solely responsible for ensuring that use of this tool with any file
complies with the original software terms, asset copyrights, and applicable
law.

> This project is **not affiliated with or endorsed by** Giants Software, or
> any other rights holder of the format or of the games that use it.

[astcenc]: https://github.com/ARM-software/astc-encoder
[releases]: ../../releases
