#ifndef VEYRA_EXAMPLES_DEBUG_TEXT_HPP
#define VEYRA_EXAMPLES_DEBUG_TEXT_HPP

#include "veyra/foundation/color.hpp"
#include "veyra/render/cpu/software_renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

inline constexpr std::size_t kDebugGlyphWidth = 5;
inline constexpr std::size_t kDebugGlyphHeight = 7;
inline constexpr std::size_t kDebugGlyphScale = 2;
inline constexpr float kDebugAdvance = 12.0F;

struct DebugSourceGlyph {
    char code{};
    std::array<std::uint8_t, kDebugGlyphHeight> rows{};
};

inline constexpr std::array<DebugSourceGlyph, 56> kDebugGlyphs{{
    {.code = ' ', .rows = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}, // ' '
    {.code = 'A', .rows = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, // 'A'
    {.code = 'B', .rows = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}}, // 'B'
    {.code = 'C', .rows = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, // 'C'
    {.code = 'D', .rows = {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}}, // 'D'
    {.code = 'E', .rows = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, // 'E'
    {.code = 'F', .rows = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}}, // 'F'
    {.code = 'G', .rows = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, // 'G'
    {.code = 'H', .rows = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, // 'H'
    {.code = 'I', .rows = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, // 'I'
    {.code = 'J', .rows = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}}, // 'J'
    {.code = 'K', .rows = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, // 'K'
    {.code = 'L', .rows = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}}, // 'L'
    {.code = 'M', .rows = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, // 'M'
    {.code = 'N', .rows = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}}, // 'N'
    {.code = 'O', .rows = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, // 'O'
    {.code = 'P', .rows = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}}, // 'P'
    {.code = 'Q', .rows = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}, // 'Q'
    {.code = 'R', .rows = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}}, // 'R'
    {.code = 'S', .rows = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, // 'S'
    {.code = 'T', .rows = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}}, // 'T'
    {.code = 'U', .rows = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, // 'U'
    {.code = 'V', .rows = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}}, // 'V'
    {.code = 'W', .rows = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}}, // 'W'
    {.code = 'X', .rows = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}}, // 'X'
    {.code = 'Y', .rows = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}}, // 'Y'
    {.code = 'Z', .rows = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}}, // 'Z'
    {.code = '0', .rows = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, // '0'
    {.code = '1', .rows = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}}, // '1'
    {.code = '2', .rows = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, // '2'
    {.code = '3', .rows = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}}, // '3'
    {.code = '4', .rows = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, // '4'
    {.code = '5', .rows = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}}, // '5'
    {.code = '6', .rows = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, // '6'
    {.code = '7', .rows = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}}, // '7'
    {.code = '8', .rows = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, // '8'
    {.code = '9', .rows = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}}, // '9'
    {.code = '_', .rows = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}}, // '_'
    {.code = '.', .rows = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}}, // '.'
    {.code = ':', .rows = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}}, // ':'
    {.code = '-', .rows = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}}, // '-'
    {.code = '/', .rows = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}}, // '/'
    {.code = '=', .rows = {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}}, // '='
    {.code = '+', .rows = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}}, // '+'
    {.code = '%', .rows = {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}}, // '%'
    {.code = '#', .rows = {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}}, // '#'
    {.code = '(', .rows = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}}, // '('
    {.code = ')', .rows = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}}, // ')'
    {.code = '[', .rows = {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}}, // '['
    {.code = ']', .rows = {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}}, // ']'
    {.code = '<', .rows = {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}}, // '<'
    {.code = '>', .rows = {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}}, // '>'
    {.code = '!', .rows = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}}, // '!'
    {.code = '?', .rows = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}}, // '?'
    {.code = ',', .rows = {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}}, // ','
    {.code = '*', .rows = {0x00, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0x00}}, // '*'
}};

class DebugFont {
  public:
    static const DebugFont& instance() noexcept {
        static const DebugFont font;
        return font;
    }

    const SoftwareMask* mask(char value) const noexcept {
        auto code = static_cast<unsigned char>(value);
        if (code >= 'a' && code <= 'z') {
            code = static_cast<unsigned char>(code - 32);
        }
        for (const auto& glyph : glyphs_) {
            if (glyph.code == static_cast<char>(code)) {
                return &glyph.mask;
            }
        }
        return nullptr;
    }

  private:
    static constexpr std::size_t kMaskWidth = kDebugGlyphWidth * kDebugGlyphScale;
    static constexpr std::size_t kMaskHeight = kDebugGlyphHeight * kDebugGlyphScale;

    struct Glyph {
        char code{};
        std::array<std::uint8_t, kMaskWidth * kMaskHeight> coverage{};
        SoftwareMask mask{};
    };

    DebugFont() noexcept {
        for (auto index = std::size_t{}; index < kDebugGlyphs.size(); ++index) {
            auto& glyph = glyphs_[index];
            glyph.code = kDebugGlyphs[index].code;
            for (auto row = std::size_t{}; row < kDebugGlyphHeight; ++row) {
                for (auto column = std::size_t{}; column < kDebugGlyphWidth; ++column) {
                    const auto bit = static_cast<std::uint8_t>(1U << (4U - column));
                    if ((kDebugGlyphs[index].rows[row] & bit) == 0) {
                        continue;
                    }
                    for (auto y = std::size_t{}; y < kDebugGlyphScale; ++y) {
                        for (auto x = std::size_t{}; x < kDebugGlyphScale; ++x) {
                            const auto pixel = (row * kDebugGlyphScale + y) * kMaskWidth +
                                column * kDebugGlyphScale + x;
                            glyph.coverage[pixel] = 255;
                        }
                    }
                }
            }
            glyph.mask = {
                .width = kMaskWidth,
                .height = kMaskHeight,
                .stride = kMaskWidth,
                .coverage = std::span<const std::uint8_t>(glyph.coverage),
                .generation = 1,
            };
        }
    }

    std::array<Glyph, kDebugGlyphs.size()> glyphs_{};
};

inline float debug_text_width(std::string_view text) noexcept {
    return static_cast<float>(text.size()) * kDebugAdvance;
}

template <typename Push>
void draw_debug_text(std::string_view text, float x, float y, Srgba8 color, Push& push) noexcept {
    const auto& font = DebugFont::instance();
    auto cursor = x;
    for (const auto value : text) {
        const auto* mask = font.mask(value);
        if (mask != nullptr) {
            push(DrawMask{.x = cursor, .y = y, .mask = mask, .color = color});
        }
        cursor += kDebugAdvance;
    }
}

}

#endif
