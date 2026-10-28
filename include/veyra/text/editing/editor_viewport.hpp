#ifndef VEYRA_TEXT_EDITOR_VIEWPORT_HPP
#define VEYRA_TEXT_EDITOR_VIEWPORT_HPP

#include <cstddef>
#include <cstdint>

namespace veyra {

enum class EditorViewportResult : std::uint8_t {
    ignored,
    handled,
    changed,
    invalid_input,
};

class EditorViewport {
  public:
    EditorViewport() noexcept = default;

    EditorViewportResult
    set_total_lines(std::size_t total, std::size_t inserted_before = 0) noexcept;
    EditorViewportResult set_line_height(float height_px) noexcept;
    EditorViewportResult set_view_height(float height_px) noexcept;
    EditorViewportResult scroll_to_line(std::size_t line) noexcept;
    EditorViewportResult scroll_by_px(float delta_px) noexcept;
    EditorViewportResult ensure_visible(std::size_t line, std::size_t context_lines) noexcept;
    void visible_range(std::size_t& first_line, std::size_t& line_count) const noexcept;

    std::size_t total_lines() const noexcept;
    float line_height() const noexcept;
    float view_height() const noexcept;
    std::size_t first_line() const noexcept;
    float offset_in_first() const noexcept;
    std::size_t visible_count() const noexcept;
    std::uint64_t generation() const noexcept;

  private:
    double max_offset_px() const noexcept;
    double offset_px() const noexcept;
    void anchor(double offset) noexcept;
    void bump_generation() noexcept;

    std::size_t total_lines_{};
    float line_height_px_{};
    float view_height_px_{};
    std::size_t first_line_{};
    float offset_in_first_{};
    std::uint64_t generation_{1};
};

}

namespace veyra::text {
using ::veyra::EditorViewport;
using ::veyra::EditorViewportResult;
}

namespace veyra::text::editing {
using ::veyra::EditorViewport;
using ::veyra::EditorViewportResult;
}

#endif
