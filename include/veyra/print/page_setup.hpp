#ifndef VEYRA_PRINT_PAGE_SETUP_HPP
#define VEYRA_PRINT_PAGE_SETUP_HPP

#include "veyra/layout/geometry.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace veyra {

constexpr float kPrintMillimetersPerInch = 25.4F;

struct PrintPaperSize {
    float width_mm{};
    float height_mm{};
};

constexpr PrintPaperSize kPrintPaperA4 = {.width_mm = 210.0F, .height_mm = 297.0F};
constexpr PrintPaperSize kPrintPaperLetter = {.width_mm = 215.9F, .height_mm = 279.4F};

enum class PrintOrientation : std::uint8_t {
    portrait,
    landscape,
};

struct PrintMargins {
    float top_mm{};
    float right_mm{};
    float bottom_mm{};
    float left_mm{};
};

struct PrintSetup {
    PrintPaperSize paper{};
    PrintOrientation orientation{PrintOrientation::portrait};
    PrintMargins margins{};
    float dpi{};
};

enum class PrintSetupResult : std::uint8_t {
    complete,
    invalid_input,
    margins_exceed_paper,
};

struct PrintPagePixels {
    std::size_t width{};
    std::size_t height{};
};

inline PrintPaperSize oriented_paper_size(const PrintSetup& setup) noexcept {
    if (setup.orientation == PrintOrientation::landscape) {
        return {.width_mm = setup.paper.height_mm, .height_mm = setup.paper.width_mm};
    }

    return setup.paper;
}

inline PrintSetupResult
resolve_print_content_rect(const PrintSetup& setup, LayoutRect& content_rect_mm) noexcept {
    if (!std::isfinite(setup.paper.width_mm) || !std::isfinite(setup.paper.height_mm)) {
        return PrintSetupResult::invalid_input;
    }

    if (!std::isfinite(setup.margins.top_mm) || !std::isfinite(setup.margins.right_mm)) {
        return PrintSetupResult::invalid_input;
    }

    if (!std::isfinite(setup.margins.bottom_mm) || !std::isfinite(setup.margins.left_mm)) {
        return PrintSetupResult::invalid_input;
    }

    if (!std::isfinite(setup.dpi)) {
        return PrintSetupResult::invalid_input;
    }

    if (setup.paper.width_mm <= 0.0F || setup.paper.height_mm <= 0.0F) {
        return PrintSetupResult::invalid_input;
    }

    if (setup.margins.top_mm < 0.0F || setup.margins.right_mm < 0.0F) {
        return PrintSetupResult::invalid_input;
    }

    if (setup.margins.bottom_mm < 0.0F || setup.margins.left_mm < 0.0F) {
        return PrintSetupResult::invalid_input;
    }

    if (setup.dpi <= 0.0F) {
        return PrintSetupResult::invalid_input;
    }

    const PrintPaperSize paper = oriented_paper_size(setup);

    const float horizontal_margins = setup.margins.left_mm + setup.margins.right_mm;
    const float vertical_margins = setup.margins.top_mm + setup.margins.bottom_mm;

    if (horizontal_margins >= paper.width_mm || vertical_margins >= paper.height_mm) {
        return PrintSetupResult::margins_exceed_paper;
    }

    content_rect_mm.x = setup.margins.left_mm;
    content_rect_mm.y = setup.margins.top_mm;
    content_rect_mm.width = paper.width_mm - horizontal_margins;
    content_rect_mm.height = paper.height_mm - vertical_margins;

    return PrintSetupResult::complete;
}

inline PrintPagePixels print_page_pixels(const PrintSetup& setup) noexcept {
    const PrintPaperSize paper = oriented_paper_size(setup);

    if (!std::isfinite(paper.width_mm) || !std::isfinite(paper.height_mm)) {
        return {};
    }

    if (!std::isfinite(setup.dpi) || setup.dpi <= 0.0F) {
        return {};
    }

    if (paper.width_mm <= 0.0F || paper.height_mm <= 0.0F) {
        return {};
    }

    const double scale =
        static_cast<double>(setup.dpi) / static_cast<double>(kPrintMillimetersPerInch);
    const double width = static_cast<double>(paper.width_mm) * scale;
    const double height = static_cast<double>(paper.height_mm) * scale;

    return {
        .width = static_cast<std::size_t>(width + 0.5),
        .height = static_cast<std::size_t>(height + 0.5),
    };
}

inline LayoutRect print_content_pixels(LayoutRect content_rect_mm, float dpi) noexcept {
    if (!std::isfinite(content_rect_mm.x) || !std::isfinite(content_rect_mm.y)) {
        return {};
    }

    if (!std::isfinite(content_rect_mm.width) || !std::isfinite(content_rect_mm.height)) {
        return {};
    }

    if (!std::isfinite(dpi) || dpi <= 0.0F) {
        return {};
    }

    const double scale = static_cast<double>(dpi) / static_cast<double>(kPrintMillimetersPerInch);

    LayoutRect content_pixels{};
    content_pixels.x = static_cast<float>(static_cast<double>(content_rect_mm.x) * scale);
    content_pixels.y = static_cast<float>(static_cast<double>(content_rect_mm.y) * scale);
    content_pixels.width = static_cast<float>(static_cast<double>(content_rect_mm.width) * scale);
    content_pixels.height = static_cast<float>(static_cast<double>(content_rect_mm.height) * scale);

    return content_pixels;
}

}

#endif
