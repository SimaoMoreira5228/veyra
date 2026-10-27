#ifndef VEYRA_TEXT_FONT_FALLBACK_HPP
#define VEYRA_TEXT_FONT_FALLBACK_HPP

#include <hb.h>
#include <unicode/ubrk.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace veyra {

enum class FontFallbackResult : std::uint8_t {
    complete,
    invalid_utf8,
    input_limit_exceeded,
    invalid_fonts,
    missing_glyph,
    insufficient_output,
    initialization_failed,
};

struct FontFallbackRun {
    std::size_t byte_offset{};
    std::size_t byte_length{};
    hb_font_t* font{};
};

class FontFallbackResolver {
  public:
    explicit FontFallbackResolver(std::size_t maximum_input_bytes) noexcept;
    ~FontFallbackResolver();

    FontFallbackResolver(const FontFallbackResolver&) = delete;
    FontFallbackResolver& operator=(const FontFallbackResolver&) = delete;
    FontFallbackResolver(FontFallbackResolver&&) = delete;
    FontFallbackResolver& operator=(FontFallbackResolver&&) = delete;

    FontFallbackResult resolve_utf8(
        std::string_view utf8,
        std::span<hb_font_t* const> fonts,
        std::span<FontFallbackRun> output,
        std::size_t& run_count
    ) noexcept;

  private:
    struct BreakIteratorDeleter {
        void operator()(UBreakIterator* iterator) const noexcept;
    };

    std::unique_ptr<UBreakIterator, BreakIteratorDeleter> iterator_;
    std::size_t maximum_input_bytes_{};
};

}

#endif
