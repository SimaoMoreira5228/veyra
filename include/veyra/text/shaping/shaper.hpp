#ifndef VEYRA_TEXT_SHAPER_HPP
#define VEYRA_TEXT_SHAPER_HPP

#include "veyra/text/shaping/direction.hpp"
#include "veyra/text/shaping/shaped_glyph.hpp"

#include <hb.h>

#if !HB_VERSION_ATLEAST(14, 0, 0)
#error "Veyra text shaping requires HarfBuzz 14.0.0 or newer"
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace veyra {

struct TextRunProperties {
    TextDirection direction{TextDirection::left_to_right};
    std::string_view script;
    std::string_view language;
};

enum class ShapeResult : std::uint8_t {
    complete,
    invalid_input,
    input_limit_exceeded,
    insufficient_output,
    allocation_failure,
};

class TextShaper {
  public:
    explicit TextShaper(std::size_t maximum_input_bytes) noexcept;
    ~TextShaper();

    TextShaper(TextShaper&&) noexcept;
    TextShaper& operator=(TextShaper&&) noexcept;
    TextShaper(const TextShaper&) = delete;
    TextShaper& operator=(const TextShaper&) = delete;

    ShapeResult shape_utf8(
        hb_font_t* font,
        std::string_view text,
        TextRunProperties properties,
        std::span<ShapedGlyph> output,
        std::size_t& glyph_count
    ) noexcept;

  private:
    struct BufferDeleter {
        void operator()(hb_buffer_t* buffer) const noexcept;
    };

    std::unique_ptr<hb_buffer_t, BufferDeleter> buffer_;
    std::size_t maximum_input_bytes_{};
};

}

namespace veyra::text {
using ::veyra::ShapeResult;
using ::veyra::TextRunProperties;
using ::veyra::TextShaper;
}

namespace veyra::text::shaping {
using ::veyra::ShapeResult;
using ::veyra::TextRunProperties;
using ::veyra::TextShaper;
}

#endif
