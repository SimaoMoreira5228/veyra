#include "veyra/layout/image_fit.hpp"
#include "veyra/render/software_renderer.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool near(float left, float right) noexcept {
    return std::abs(left - right) < 0.0001F;
}

bool same_rect(const veyra::LayoutRect& left, const veyra::LayoutRect& right) noexcept {
    return left.x == right.x && left.y == right.y && left.width == right.width &&
        left.height == right.height;
}

void check_fit_modes() {
    const auto bounds = veyra::LayoutRect{
        .x = 3.0F,
        .y = 7.0F,
        .width = 8.0F,
        .height = 8.0F,
    };
    const auto intrinsic = veyra::ImageSize{.width = 4.0F, .height = 2.0F};
    auto placement = veyra::ImagePlacement{};

    require(
        veyra::fit_image(bounds, intrinsic, veyra::ImageFitMode::fill, placement) ==
            veyra::ImageFitResult::complete,
        "fill produces an image placement"
    );
    require(same_rect(placement.destination, bounds), "fill uses the full destination bounds");
    require(
        same_rect(placement.source, {.width = 4.0F, .height = 2.0F}),
        "fill keeps the full source image"
    );

    require(
        veyra::fit_image(bounds, intrinsic, veyra::ImageFitMode::contain, placement) ==
            veyra::ImageFitResult::complete,
        "contain produces an image placement"
    );
    require(
        near(placement.destination.x, 3.0F) && near(placement.destination.y, 9.0F) &&
            near(placement.destination.width, 8.0F) && near(placement.destination.height, 4.0F),
        "contain centers the full image without changing its aspect ratio"
    );

    require(
        veyra::fit_image(bounds, intrinsic, veyra::ImageFitMode::cover, placement) ==
            veyra::ImageFitResult::complete,
        "cover produces an image placement"
    );
    require(same_rect(placement.destination, bounds), "cover fills the destination bounds");
    require(
        near(placement.source.x, 1.0F) && near(placement.source.y, 0.0F) &&
            near(placement.source.width, 2.0F) && near(placement.source.height, 2.0F),
        "cover crops the source while preserving its aspect ratio"
    );

    require(
        veyra::fit_image(bounds, intrinsic, veyra::ImageFitMode::none, placement) ==
            veyra::ImageFitResult::complete,
        "none produces an image placement"
    );
    require(
        same_rect(
            placement.destination,
            {
                .x = 5.0F,
                .y = 10.0F,
                .width = 4.0F,
                .height = 2.0F,
            }
        ),
        "none centers the image at its intrinsic size"
    );

    require(
        veyra::fit_image(
            {.x = 3.0F, .y = 7.0F, .width = 2.0F, .height = 1.0F},
            intrinsic,
            veyra::ImageFitMode::none,
            placement
        ) == veyra::ImageFitResult::complete,
        "none crops an image that exceeds its box"
    );
    require(
        near(placement.source.x, 1.0F) && near(placement.source.y, 0.5F) &&
            near(placement.source.width, 2.0F) && near(placement.source.height, 1.0F),
        "none returns the visible source crop"
    );
    require(
        near(placement.destination.x, 3.0F) && near(placement.destination.y, 7.0F) &&
            near(placement.destination.width, 2.0F) && near(placement.destination.height, 1.0F),
        "none returns the visible destination rectangle"
    );

    require(
        veyra::fit_image(bounds, intrinsic, veyra::ImageFitMode::scale_down, placement) ==
            veyra::ImageFitResult::complete,
        "scale-down produces an image placement"
    );
    require(
        placement.destination.width == intrinsic.width &&
            placement.destination.height == intrinsic.height,
        "scale-down does not enlarge the source"
    );

    require(
        veyra::fit_image(
            {.width = 2.0F, .height = 2.0F},
            intrinsic,
            veyra::ImageFitMode::scale_down,
            placement
        ) == veyra::ImageFitResult::complete,
        "scale-down fits inside smaller bounds"
    );
    require(
        near(placement.destination.width, 2.0F) && near(placement.destination.height, 1.0F) &&
            near(placement.destination.y, 0.5F),
        "scale-down preserves aspect ratio when it must shrink"
    );
}

void check_empty_and_invalid_inputs_leave_output_untouched() {
    const auto original = veyra::ImagePlacement{
        .source = {.x = 1.0F, .y = 2.0F, .width = 3.0F, .height = 4.0F},
        .destination = {.x = 5.0F, .y = 6.0F, .width = 7.0F, .height = 8.0F},
    };
    auto placement = original;

    require(
        veyra::fit_image(
            {.width = 0.0F, .height = 10.0F},
            {.width = 4.0F, .height = 2.0F},
            veyra::ImageFitMode::contain,
            placement
        ) == veyra::ImageFitResult::empty,
        "zero-sized bounds produce an empty placement"
    );
    require(same_rect(placement.source, original.source), "empty bounds leave output unchanged");

    require(
        veyra::fit_image(
            {.width = 10.0F, .height = 10.0F},
            {.width = 0.0F, .height = 2.0F},
            veyra::ImageFitMode::cover,
            placement
        ) == veyra::ImageFitResult::invalid_input,
        "zero-sized source images are rejected"
    );
    require(
        same_rect(placement.destination, original.destination),
        "invalid input leaves output unchanged"
    );

    require(
        veyra::fit_image(
            {.width = 10.0F, .height = 10.0F},
            {.width = 4.0F, .height = 2.0F},
            static_cast<veyra::ImageFitMode>(99),
            placement
        ) == veyra::ImageFitResult::invalid_input,
        "unknown fit modes are rejected"
    );
}

void check_cover_rendering() {
    const std::array pixels{
        veyra::SoftwarePixel{.red = 10},
        veyra::SoftwarePixel{.red = 30},
        veyra::SoftwarePixel{.red = 50},
        veyra::SoftwarePixel{.red = 70},
        veyra::SoftwarePixel{.red = 100},
        veyra::SoftwarePixel{.red = 120},
        veyra::SoftwarePixel{.red = 140},
        veyra::SoftwarePixel{.red = 160},
    };
    const auto image = veyra::SoftwareImage{
        .width = 4,
        .height = 2,
        .stride = 4,
        .pixels = pixels,
    };
    auto placement = veyra::ImagePlacement{};
    require(
        veyra::fit_image(
            {.width = 4.0F, .height = 4.0F},
            {.width = 4.0F, .height = 2.0F},
            veyra::ImageFitMode::cover,
            placement
        ) == veyra::ImageFitResult::complete,
        "cover placement is ready for rendering"
    );

    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::DrawImage{
            .source = placement.source,
            .destination = placement.destination,
            .image = &image,
        },
    };
    std::array<veyra::SoftwarePixel, 16> destination{};
    std::array<veyra::SoftwareClipRect, 1> clips{};
    const auto result = veyra::render_software(
        commands,
        {.width = 4, .height = 4, .stride = 4, .pixels = destination},
        {.clips = clips}
    );
    require(result == veyra::SoftwareRenderResult::complete, "cover placement renders");
    require(
        destination[0].red == 30 && destination[1].red == 30 && destination[2].red == 50 &&
            destination[3].red == 50 && destination[12].red == 120 && destination[15].red == 140,
        "cover rendering crops and scales the source image"
    );
}

void check_invalid_source_bounds_do_not_render_partial_frames() {
    const std::array image_pixels{veyra::SoftwarePixel{.red = 255}};
    const auto image = veyra::SoftwareImage{
        .width = 1,
        .height = 1,
        .stride = 1,
        .pixels = image_pixels,
    };
    const std::array<veyra::DisplayCommand, 2> commands{
        veyra::FillRect{
            .bounds = {.width = 2.0F, .height = 2.0F},
            .color = {.green = 255},
        },
        veyra::DrawImage{
            .source = {.x = 0.5F, .width = 1.0F, .height = 1.0F},
            .destination = {.width = 2.0F, .height = 2.0F},
            .image = &image,
        },
    };
    auto destination = std::array<veyra::SoftwarePixel, 4>{};
    destination.fill(veyra::SoftwarePixel{.alpha = 0});
    std::array<veyra::SoftwareClipRect, 1> clips{};

    const auto result = veyra::render_software(
        commands,
        {.width = 2, .height = 2, .stride = 2, .pixels = destination},
        {.clips = clips}
    );
    require(
        result == veyra::SoftwareRenderResult::invalid_input,
        "out-of-image crops are rejected"
    );
    for (const auto pixel : destination) {
        require(pixel.alpha == 0, "invalid image placement leaves the surface unchanged");
    }
}

}

int main() {
    try {
        check_fit_modes();
        check_empty_and_invalid_inputs_leave_output_untouched();
        check_cover_rendering();
        check_invalid_source_bounds_do_not_render_partial_frames();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
