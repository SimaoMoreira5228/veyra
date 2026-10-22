#include "veyra/text/editor_viewport.hpp"

#include <cmath>
#include <limits>

namespace veyra {

namespace {

constexpr std::uint64_t kFirstGeneration = 1;

}

EditorViewportResult
EditorViewport::set_total_lines(std::size_t total, std::size_t inserted_before) noexcept {
    if (inserted_before > total) {
        return EditorViewportResult::invalid_input;
    }

    if (total == total_lines_ && inserted_before == 0) {
        return EditorViewportResult::handled;
    }

    const std::size_t old_first = first_line_;
    const float old_offset = offset_in_first_;
    const std::size_t old_total = total_lines_;

    total_lines_ = total;

    if (inserted_before > std::numeric_limits<std::size_t>::max() - first_line_) {
        first_line_ = std::numeric_limits<std::size_t>::max();
    } else {
        first_line_ += inserted_before;
    }

    anchor(offset_px());

    if (total == old_total && first_line_ == old_first && offset_in_first_ == old_offset) {
        return EditorViewportResult::handled;
    }

    bump_generation();

    return EditorViewportResult::changed;
}

EditorViewportResult EditorViewport::set_line_height(float height_px) noexcept {
    if (!std::isfinite(height_px) || height_px <= 0.0F) {
        return EditorViewportResult::invalid_input;
    }

    if (height_px == line_height_px_) {
        return EditorViewportResult::handled;
    }

    line_height_px_ = height_px;
    anchor(offset_px());
    bump_generation();

    return EditorViewportResult::changed;
}

EditorViewportResult EditorViewport::set_view_height(float height_px) noexcept {
    if (!std::isfinite(height_px) || height_px <= 0.0F) {
        return EditorViewportResult::invalid_input;
    }

    if (height_px == view_height_px_) {
        return EditorViewportResult::handled;
    }

    view_height_px_ = height_px;
    anchor(offset_px());
    bump_generation();

    return EditorViewportResult::changed;
}

EditorViewportResult EditorViewport::scroll_to_line(std::size_t line) noexcept {
    if (total_lines_ == 0) {
        return EditorViewportResult::ignored;
    }

    if (line_height_px_ <= 0.0F) {
        return EditorViewportResult::invalid_input;
    }

    const double line_height = static_cast<double>(line_height_px_);
    const double target = static_cast<double>(line) * line_height;
    const double clamped = target < max_offset_px() ? target : max_offset_px();

    const std::size_t old_first = first_line_;
    const float old_offset = offset_in_first_;

    anchor(clamped);

    if (first_line_ == old_first && offset_in_first_ == old_offset) {
        return EditorViewportResult::ignored;
    }

    bump_generation();

    return EditorViewportResult::changed;
}

EditorViewportResult EditorViewport::scroll_by_px(float delta_px) noexcept {
    if (!std::isfinite(delta_px)) {
        return EditorViewportResult::invalid_input;
    }

    if (total_lines_ == 0) {
        return EditorViewportResult::ignored;
    }

    if (line_height_px_ <= 0.0F) {
        return EditorViewportResult::invalid_input;
    }

    const std::size_t old_first = first_line_;
    const float old_offset = offset_in_first_;

    anchor(offset_px() + static_cast<double>(delta_px));

    if (first_line_ == old_first && offset_in_first_ == old_offset) {
        return EditorViewportResult::ignored;
    }

    bump_generation();

    return EditorViewportResult::changed;
}

EditorViewportResult
EditorViewport::ensure_visible(std::size_t line, std::size_t context_lines) noexcept {
    if (total_lines_ == 0) {
        return EditorViewportResult::ignored;
    }

    if (line_height_px_ <= 0.0F || view_height_px_ <= 0.0F) {
        return EditorViewportResult::invalid_input;
    }

    const std::size_t last_line = total_lines_ - 1;
    const std::size_t target = line < last_line ? line : last_line;
    const std::size_t count = visible_count();

    if (count == 0) {
        return EditorViewportResult::invalid_input;
    }

    const std::size_t context = context_lines < count ? context_lines : count - 1;
    const std::size_t top_guard = first_line_ + context;
    const std::size_t bottom_guard = first_line_ + count - 1 - context;

    const std::size_t old_first = first_line_;
    const float old_offset = offset_in_first_;
    const double line_height = static_cast<double>(line_height_px_);

    if (target < top_guard) {
        const std::size_t shifted = target < context ? 0 : target - context;

        anchor(static_cast<double>(shifted) * line_height);
    } else if (target > bottom_guard) {
        anchor(static_cast<double>(target + 1 + context - count) * line_height);
    } else {
        return EditorViewportResult::ignored;
    }

    if (first_line_ == old_first && offset_in_first_ == old_offset) {
        return EditorViewportResult::ignored;
    }

    bump_generation();

    return EditorViewportResult::changed;
}

void EditorViewport::visible_range(
    std::size_t& first_line,
    std::size_t& line_count
) const noexcept {
    if (total_lines_ == 0 || line_height_px_ <= 0.0F || view_height_px_ <= 0.0F) {
        first_line = 0;
        line_count = 0;

        return;
    }

    first_line = first_line_ < total_lines_ ? first_line_ : total_lines_ - 1;
    line_count = visible_count();
}

std::size_t EditorViewport::total_lines() const noexcept {
    return total_lines_;
}

float EditorViewport::line_height() const noexcept {
    return line_height_px_;
}

float EditorViewport::view_height() const noexcept {
    return view_height_px_;
}

std::size_t EditorViewport::first_line() const noexcept {
    return first_line_;
}

float EditorViewport::offset_in_first() const noexcept {
    return offset_in_first_;
}

std::size_t EditorViewport::visible_count() const noexcept {
    if (total_lines_ == 0 || line_height_px_ <= 0.0F || view_height_px_ <= 0.0F) {
        return 0;
    }

    const double line_height = static_cast<double>(line_height_px_);
    const double start = offset_px();
    const double end = start + static_cast<double>(view_height_px_);

    const auto first = static_cast<std::size_t>(start / line_height);
    auto last_exclusive = static_cast<std::size_t>(std::ceil(end / line_height));

    if (last_exclusive > total_lines_) {
        last_exclusive = total_lines_;
    }

    if (last_exclusive <= first || first >= total_lines_) {
        return 0;
    }

    return last_exclusive - first;
}

std::uint64_t EditorViewport::generation() const noexcept {
    return generation_;
}

double EditorViewport::max_offset_px() const noexcept {
    if (total_lines_ == 0 || line_height_px_ <= 0.0F) {
        return 0.0;
    }

    const double extent = static_cast<double>(total_lines_) * static_cast<double>(line_height_px_);
    const double view = static_cast<double>(view_height_px_);

    return extent > view ? extent - view : 0.0;
}

double EditorViewport::offset_px() const noexcept {
    if (line_height_px_ <= 0.0F) {
        return 0.0;
    }

    return static_cast<double>(first_line_) * static_cast<double>(line_height_px_) +
        static_cast<double>(offset_in_first_);
}

void EditorViewport::anchor(double offset) noexcept {
    if (line_height_px_ <= 0.0F) {
        first_line_ = 0;
        offset_in_first_ = 0.0F;

        return;
    }

    const double line_height = static_cast<double>(line_height_px_);
    const double maximum = max_offset_px();

    const double clamped = offset < 0.0 ? 0.0 : (offset > maximum ? maximum : offset);
    const auto first = static_cast<std::size_t>(clamped / line_height);

    first_line_ = first;
    offset_in_first_ = static_cast<float>(clamped - static_cast<double>(first) * line_height);

    if (offset_in_first_ < 0.0F) {
        offset_in_first_ = 0.0F;
    } else if (offset_in_first_ >= line_height_px_) {
        ++first_line_;
        offset_in_first_ -= line_height_px_;
    }
}

void EditorViewport::bump_generation() noexcept {
    ++generation_;

    if (generation_ == 0) {
        generation_ = kFirstGeneration;
    }
}

}
