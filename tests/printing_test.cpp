#include "veyra/print/page_setup.hpp"

#include "veyra/print/pagination.hpp"
#include "veyra/render/software_renderer.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_content_rect_from_setup() {
    const veyra::PrintSetup setup{
        .paper = veyra::kPrintPaperA4,
        .orientation = veyra::PrintOrientation::portrait,
        .margins = {.top_mm = 10.0F, .right_mm = 10.0F, .bottom_mm = 10.0F, .left_mm = 10.0F},
        .dpi = 300.0F,
    };
    veyra::LayoutRect content{};
    require(
        veyra::resolve_print_content_rect(setup, content) == veyra::PrintSetupResult::complete,
        "a4 setup with even margins resolves"
    );
    require(
        content.x == 10.0F && content.y == 10.0F && content.width == 190.0F &&
            content.height == 277.0F,
        "portrait content rect subtracts margins from a4"
    );

    const veyra::PrintSetup landscape{
        .paper = veyra::kPrintPaperLetter,
        .orientation = veyra::PrintOrientation::landscape,
        .dpi = 300.0F,
    };
    veyra::LayoutRect swapped{};
    require(
        veyra::resolve_print_content_rect(landscape, swapped) == veyra::PrintSetupResult::complete,
        "margin-free letter setup resolves"
    );
    require(
        swapped.x == 0.0F && swapped.y == 0.0F && swapped.width == 279.4F &&
            swapped.height == 215.9F,
        "landscape swaps letter dimensions"
    );

    const veyra::PrintPagePixels pixels = veyra::print_page_pixels(setup);
    require(pixels.width == 2480 && pixels.height == 3508, "a4 at 300dpi sizes the pixel page");
}

void check_setup_rejection_leaves_output_alone() {
    constexpr veyra::LayoutRect sentinel{
        .x = -1.0F,
        .y = -2.0F,
        .width = -3.0F,
        .height = -4.0F,
    };

    veyra::PrintSetup bad_dpi{
        .paper = veyra::kPrintPaperA4,
        .dpi = std::numeric_limits<float>::quiet_NaN(),
    };
    veyra::LayoutRect content = sentinel;
    require(
        veyra::resolve_print_content_rect(bad_dpi, content) ==
            veyra::PrintSetupResult::invalid_input,
        "nan dpi is rejected"
    );
    require(
        content.x == sentinel.x && content.y == sentinel.y && content.width == sentinel.width &&
            content.height == sentinel.height,
        "rejected dpi leaves the content rect untouched"
    );

    veyra::PrintSetup bad_margins{
        .paper = veyra::kPrintPaperA4,
        .margins = {.right_mm = 200.0F, .left_mm = 200.0F},
        .dpi = 300.0F,
    };
    content = sentinel;
    require(
        veyra::resolve_print_content_rect(bad_margins, content) ==
            veyra::PrintSetupResult::margins_exceed_paper,
        "margins wider than the paper are rejected"
    );
    require(
        content.x == sentinel.x && content.y == sentinel.y && content.width == sentinel.width &&
            content.height == sentinel.height,
        "rejected margins leave the content rect untouched"
    );

    veyra::PrintSetup negative_margin{
        .paper = veyra::kPrintPaperA4,
        .margins = {.top_mm = -1.0F},
        .dpi = 300.0F,
    };
    content = sentinel;
    require(
        veyra::resolve_print_content_rect(negative_margin, content) ==
            veyra::PrintSetupResult::invalid_input,
        "negative margins are rejected"
    );
    require(
        content.x == sentinel.x && content.y == sentinel.y && content.width == sentinel.width &&
            content.height == sentinel.height,
        "negative margins leave the content rect untouched"
    );
}

void check_pagination_slices_content_height() {
    constexpr std::array<float, 3> blocks{3.0F, 3.0F, 4.0F};
    constexpr veyra::LayoutRect content{.x = 1.0F, .y = 1.0F, .width = 8.0F, .height = 6.0F};

    std::array<veyra::LayoutRect, 2> pages{};
    std::size_t page_count = 99;
    require(
        veyra::paginate_print_blocks(blocks, content, pages, page_count) ==
            veyra::PaginationResult::complete,
        "ten units of blocks fit on two six-unit pages"
    );
    require(page_count == 2, "pagination reports two pages");
    require(
        pages[0].x == 1.0F && pages[0].y == 1.0F && pages[0].width == 8.0F &&
            pages[0].height == 6.0F,
        "first page covers the full content rect"
    );
    require(
        pages[1].x == 1.0F && pages[1].y == 7.0F && pages[1].width == 8.0F &&
            pages[1].height == 4.0F,
        "last page keeps full width with the leftover height"
    );

    std::array<veyra::LayoutRect, 1> short_storage{
        veyra::LayoutRect{.x = -1.0F, .y = -1.0F, .width = -1.0F, .height = -1.0F},
    };
    page_count = 99;
    require(
        veyra::paginate_print_blocks(blocks, content, short_storage, page_count) ==
            veyra::PaginationResult::insufficient_output,
        "one slot for two pages reports insufficient output"
    );
    require(page_count == 2, "insufficient output still reports the needed count");
    require(
        short_storage[0].x == -1.0F && short_storage[0].y == -1.0F &&
            short_storage[0].width == -1.0F && short_storage[0].height == -1.0F,
        "insufficient output leaves storage untouched"
    );

    constexpr std::array<float, 2> bad_blocks{2.0F, -1.0F};
    std::array<veyra::LayoutRect, 2> kept{};
    page_count = 99;
    require(
        veyra::paginate_print_blocks(bad_blocks, content, kept, page_count) ==
            veyra::PaginationResult::invalid_input,
        "negative block heights are rejected"
    );
    require(page_count == 99, "rejected blocks leave the page count untouched");
}

void check_printed_page_renders_exact_pixels() {
    const veyra::PrintSetup setup{
        .paper = {.width_mm = 10.0F, .height_mm = 8.0F},
        .margins = {.top_mm = 1.0F, .right_mm = 1.0F, .bottom_mm = 1.0F, .left_mm = 1.0F},
        .dpi = 25.4F,
    };
    const veyra::PrintPagePixels page_pixels = veyra::print_page_pixels(setup);
    require(page_pixels.width == 10 && page_pixels.height == 8, "test page is ten by eight pixels");

    veyra::LayoutRect content_mm{};
    require(
        veyra::resolve_print_content_rect(setup, content_mm) == veyra::PrintSetupResult::complete,
        "test page content rect resolves"
    );
    const veyra::LayoutRect content = veyra::print_content_pixels(content_mm, setup.dpi);
    require(
        content.x == 1.0F && content.y == 1.0F && content.width == 8.0F && content.height == 6.0F,
        "unit scale maps content millimeters to pixels one to one"
    );

    constexpr std::array<float, 3> blocks{3.0F, 3.0F, 4.0F};
    std::array<veyra::LayoutRect, 2> pages{};
    std::size_t page_count = 0;
    require(
        veyra::paginate_print_blocks(blocks, content, pages, page_count) ==
            veyra::PaginationResult::complete,
        "test document paginates before rendering"
    );
    require(page_count == 2, "test document needs two pages");

    constexpr auto page_index = std::size_t{1};
    const float slice_offset = pages[page_index].y - content.y;

    const std::array<veyra::DisplayCommand, 8> commands{
        veyra::FillRect{
            .bounds =
                {
                    .width = static_cast<float>(page_pixels.width),
                    .height = static_cast<float>(page_pixels.height),
                },
            .color = {.red = 255, .green = 255, .blue = 255},
        },
        veyra::PushClip{.bounds = content},
        veyra::PushTranslation{
            .offset = {.x = content.x, .y = content.y - slice_offset},
        },
        veyra::FillRect{
            .bounds = {.width = 8.0F, .height = 3.0F},
            .color = {.red = 255},
        },
        veyra::FillRect{
            .bounds = {.y = 3.0F, .width = 8.0F, .height = 3.0F},
            .color = {.green = 255},
        },
        veyra::FillRect{
            .bounds = {.y = 6.0F, .width = 8.0F, .height = 4.0F},
            .color = {.blue = 255},
        },
        veyra::PopTranslation{},
        veyra::PopClip{},
    };

    std::array<veyra::SoftwarePixel, 80> pixels{};
    std::array<veyra::SoftwareClipRect, 1> clips{};
    std::array<veyra::LayoutPoint, 1> translations{};
    const auto result = veyra::render_software(
        commands,
        {
            .width = page_pixels.width,
            .height = page_pixels.height,
            .stride = page_pixels.width,
            .pixels = pixels,
        },
        {.clips = clips, .translations = translations}
    );
    require(result == veyra::SoftwareRenderResult::complete, "second page renders");

    for (std::size_t y = 0; y < page_pixels.height; ++y) {
        for (std::size_t x = 0; x < page_pixels.width; ++x) {
            const bool in_block = y >= 1 && y <= 4 && x >= 1 && x <= 8;
            const auto& pixel = pixels[y * page_pixels.width + x];

            if (in_block) {
                require(
                    pixel.red == 0 && pixel.green == 0 && pixel.blue == 255 && pixel.alpha == 255,
                    "second page shows the sliced block in blue"
                );
            } else {
                require(
                    pixel.red == 255 && pixel.green == 255 && pixel.blue == 255 &&
                        pixel.alpha == 255,
                    "margins and empty page area stay paper white"
                );
            }
        }
    }
}

}

int main() {
    try {
        check_content_rect_from_setup();
        check_setup_rejection_leaves_output_alone();
        check_pagination_slices_content_height();
        check_printed_page_renders_exact_pixels();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
