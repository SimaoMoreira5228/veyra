# third_party/nanosvg

Pinned single-header SVG rasterizer from nanosvg, copied verbatim. Nothing
here is modified. `image_codecs_svg.cpp` instantiates both implementations
(`NANOSVG_IMPLEMENTATION` / `NANOSVGRAST_IMPLEMENTATION`) in its own
translation unit, so these headers need no build-system support beyond an
include path and work on every platform by construction.

## Pinned files

| File | Version | sha256 |
|---|---|---|
| `nanosvg.h` | commit `239e102` (2026-07-09) | `e34fd5d084be106cea972d19ce5d27fd96d17ba89f8d06bdceee058420c8b2b0` |
| `nanosvgrast.h` | commit `239e102` (2026-07-09) | `79a9c5f4db19debf9f3a648a1589e96d92854f245a5cb4f3d823f263785234d8` |
| `LICENSE.txt` | zlib-style (Mikko Mononen) | `e82bc3dd03400aecabe12201219ba14750dbc4b36faab58663a7a6068548d372` |

Vendored instead of fetched: the headers are self-contained (no library to
compile, no system dependency), and the pinned GitHub archive endpoint proved
unreliable at configure time. Archive sha256 for the reviewed upstream zip:
`10155d7f1027088307283785a3c06f6cb739574dcb110dc612ef83f47588e653`.
Refresh only from a reviewed upstream commit, never from `master`, and record
the new hashes here.
