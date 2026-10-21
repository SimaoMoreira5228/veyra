# Third-party code

Two shapes, two rules.

## Vendored single headers

`stb/` holds single-header libraries copied verbatim from `local/clones/stb`
and committed here. Copies are only ever refreshed from that clone, never
from the network, and every file records its upstream version plus sha256 in
`stb/README.md`. Vendored code compiles warning-clean under the project's
flags, needs no build-system support beyond an include path, and works on
every platform by construction. Overrides (`STB_ASSERT`, `STBI_NO_STDIO`)
live in the single translation unit that instantiates the library.

## Fetched dependencies

Anything bigger than a header — a library with its own sources to compile —
is *not* vendored. The CMake build prefers a system package and otherwise
fetches a pinned archive at configure time: `FetchContent_Declare` with a
fixed `URL` plus `URL_HASH`, both exposed as `VEYRA_<NAME>_URL` and
`VEYRA_<NAME>_SHA256` cache variables so forks and offline builds can
redirect or pre-seed them (`FETCHCONTENT_SOURCE_DIR_<NAME>` also works).
Pinned means frozen: no floating tags, no `master` tracking, no silent
upgrades. Each fetched dependency documents its version, hash, license, and
why the system package was not enough, next to the `FetchContent_Declare`
call and in the user-facing guide for the feature that needs it.

## Current inventory

| Name | Version | Hash | License | Notes |
| --- | --- | --- | --- | --- |
| nanosvg | commit `239e102` | sha256 `e34fd5d0…c8b2b0` (+rasterizer `79a9c5f4…92854f`) | zlib/MIT (Mikko Mononen) | headers vendored under `third_party/nanosvg` (no system package exists, self-contained) |
| libwebp | 1.6.0 | sha256 `e4ab7009…9a9e38c4564` | BSD | system libwebp + demux/mux preferred; fetch builds libs only |
| libavif | 1.4.2 | sha256 `2b645287…8366921d` | BSD-2 | system libavif preferred; fetch sets `AVIF_CODEC_AOM=LOCAL`, all other codecs off |
| libaom | v3.14.1 (via libavif `LOCAL`) | git tag (libavif-pinned) | BSD-2 + AoM patent license | pulled by libavif's own FetchContent; needs perl, plus nasm/yasm for SIMD (else generic target) |
