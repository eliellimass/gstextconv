"""gstextconv — texture converter for Giants Engine / Farming Simulator assets.

This package wraps the native C++ core (astcenc embedded, zlib via miniz).
All three public entry points mirror the specification from ``cli.md``:

* :func:`encoder` — encode image(s) into a GS2D ``.ast`` container.
* :func:`decoder` — decode a ``.ast`` / ``.astc`` container back to images.
* :func:`loader` — inspect a ``.ast`` without converting.
"""
from __future__ import annotations

from ._gstextconv_native import (  # type: ignore[attr-defined]
    Image,
    __version__,
    astcenc_version,
    decoder,
    encoder,
    loader,
)

__all__ = [
    "Image",
    "__version__",
    "astcenc_version",
    "decoder",
    "encoder",
    "loader",
]
