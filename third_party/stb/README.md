# third_party/stb

Pinned single-header libraries from stb, copied verbatim. Nothing here is
modified, and nothing here is wired into the library yet. These headers exist
so the validation tests can prove toolchain behavior before any integration
lands.

## Pinned files

| File | Version | sha256 |
|---|---|---|
| `stb_rect_pack.h` | v1.01 (`STB_RECT_PACK_VERSION` 1) | `6388366a30de57c1b4c29b6eed4ed1c7d758fc1443d2dd95cfce943370bb3aa1` |
| `stb_sprintf.h` | v1.10 (banner only, no version macro) | `e0b8c56e1084602290b9a17ef59f429b5293deb8cb9d8b0f20a14abc39b56520` |

Copies come from `local/clones/stb`, never from the network. That clone is
read-only research input: do not edit it, do not reference it from code or
build files, and do not commit it. Verify any copy with `cmp` against the
clone before trusting it.

## License

Both headers carry the same dual license in their tail comments, and the
matching text sits in the upstream `LICENSE` file: pick either side.

- MIT: keep the copyright and permission notice in all copies. That is the
  whole attribution rule. The vendored headers already contain the notice,
  so leave them alone.
- Unlicense (public domain): no conditions, where it applies.

No new dependency beyond these two files. No patent grant beyond plain MIT.

## What is vendored, and what stays out

| Header | Status | Why |
|---|---|---|
| `stb_rect_pack.h` | Vendored, validated only | Skyline packer for a future glyph atlas. Zero allocations, caller-owned nodes. |
| `stb_sprintf.h` | Vendored, validated only | Locale-independent formatting for logs and diagnostics. Same 64-bit format strings on MSVC and GCC. |
| `stb_image.h` | Out | Decode of untrusted bytes needs pre-flight caps and a fuzzed pin first. |
| `stb_image_resize2.h` | Out | Thumbnail work needs caller-owned buffers and explicit SIMD flags first. |
| `stb_image_write.h` | Out | Debug-dump tooling only, and its flip and compression flags are process-global. |
| `stb_textedit.h` | Out | Edit-state machine with fixed undo budgets, needs binding to the editor buffer first. |
| `stb_truetype.h` | Out | Upstream disclaims hostile input. FreeType stays for user fonts. |

## Override policy

`stb_rect_pack.h` compiles against `STBRP_SORT` and `STBRP_ASSERT`. The
validation test defines both, which proves the hooks take without touching
the library. Future atlas code should route these to Veyra asserts and sort.

`stb_sprintf.h` needs no overrides. One caveat travels with it:
`stbsp_set_separators` writes process-global state, so it is not thread-safe.
The validation test sets custom separators and restores the defaults in the
same check. Any future logging use must serialize that call or set it once at
startup.

## Integration status

Both headers: validated only. No library target links them, no library
source includes them, and `third_party/stb` is on the include path of the two
validation tests alone. Wiring the packer into the glyph path or the
formatter into logging is later work with its own tests.

## Update procedure

1. Copy the new file from the frozen clone, never from the network.
2. Confirm byte identity with `cmp` and record the new `sha256sum` in the
   table above plus the banner version.
3. Re-run `ctest --output-on-failure` with default flags and require 100%.
4. Re-check the license tail if the version moved; the dual license has held
   across versions so far, but trust the file, not the habit.
