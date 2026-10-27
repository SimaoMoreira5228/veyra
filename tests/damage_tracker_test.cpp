#include "veyra/render/damage/damage_tracker.hpp"

#include "veyra/render/cpu/software_renderer.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_pixel_safe_merging_and_clipping() {
    std::array<veyra::LayoutRect, 3> storage{};
    veyra::DamageTracker tracker(8, 8, storage);
    require(tracker.valid(), "caller storage configures damage tracking");

    require(
        tracker.add({.x = 0.1F, .y = 0.1F, .width = 0.1F, .height = 0.1F}),
        "first partial-pixel region is accepted"
    );
    require(
        tracker.add({.x = 1.1F, .y = 0.1F, .width = 0.1F, .height = 0.1F}),
        "separate rounded pixel regions remain separate"
    );
    require(tracker.regions().size() == 2, "non-overlapping pixel regions stay separate");
    require(
        tracker.add({.x = 0.6F, .y = 0.2F, .width = 0.6F, .height = 0.1F}),
        "a bridge region merges transitively overlapping pixels"
    );
    require(tracker.regions().size() == 1, "transitive pixel overlap becomes one region");

    require(
        tracker.add({.x = 4.0F, .y = 4.0F, .width = 1.0F, .height = 1.0F}),
        "disjoint region remains separate"
    );
    require(
        tracker.add({.x = -2.0F, .y = 7.0F, .width = 4.0F, .height = 2.0F}),
        "partly offscreen region is clipped"
    );
    require(tracker.regions().size() == 3, "clipped region uses one workspace slot");

    std::array<veyra::SoftwarePixel, 64> pixels{};
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::FillRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .color = {.red = 80, .green = 90, .blue = 100},
        },
    };
    const auto surface = veyra::SoftwareSurface{
        .width = 8,
        .height = 8,
        .stride = 8,
        .pixels = pixels,
    };
    require(
        veyra::render_software(commands, surface, {}, tracker.regions()) ==
            veyra::SoftwareRenderResult::complete,
        "tracked regions satisfy renderer damage constraints"
    );
    require(pixels[0].red == 80 && pixels[4 * 8 + 4].red == 80, "tracked pixels are rendered");
    constexpr auto clipped_row = std::size_t{7} * 8;
    require(
        pixels[clipped_row].red == 80 && pixels[clipped_row + 1].red == 80,
        "clipped damage reaches the visible surface edge"
    );
}

void check_overflow_and_reset() {
    std::array<veyra::LayoutRect, 2> storage{};
    veyra::DamageTracker tracker(8, 6, storage);
    require(tracker.valid(), "small damage workspace is valid");
    require(
        tracker.add({.x = 1.0F, .y = 1.0F, .width = 1.0F, .height = 1.0F}),
        "first region adds"
    );
    require(
        tracker.add({.x = 4.0F, .y = 1.0F, .width = 1.0F, .height = 1.0F}),
        "second region adds"
    );
    require(
        tracker.add({.x = 1.0F, .y = 4.0F, .width = 1.0F, .height = 1.0F}),
        "overflow adds safely"
    );
    require(tracker.full_surface(), "full workspace falls back to the whole surface");
    require(tracker.regions().size() == 1, "full damage needs only one region");
    require(
        tracker.regions()[0].width >= 8.0F && tracker.regions()[0].height >= 6.0F,
        "fallback region covers the full surface"
    );
    require(
        tracker.add({.x = 0.0F, .y = 0.0F, .width = 1.0F, .height = 1.0F}),
        "additional changes coalesce into full damage"
    );
    require(tracker.regions().size() == 1, "full damage stays coalesced");

    tracker.clear();
    require(!tracker.full_surface() && tracker.regions().empty(), "clear resets the tracker");
    require(
        tracker.add({.x = 50.0F, .y = 50.0F, .width = 1.0F, .height = 1.0F}),
        "offscreen damage is ignored"
    );
    require(tracker.regions().empty(), "offscreen changes do not consume workspace");
    require(tracker.mark_full(), "caller can request a full repaint");
    require(
        tracker.full_surface() && tracker.regions().size() == 1,
        "explicit full damage is recorded"
    );
}

void check_invalid_inputs_and_workspace() {
    std::array<veyra::LayoutRect, 1> storage{};
    veyra::DamageTracker tracker(8, 8, storage);
    require(tracker.valid(), "single-region workspace is valid");
    require(
        tracker.add({.x = 2.0F, .y = 2.0F, .width = 1.0F, .height = 1.0F}),
        "valid damage adds"
    );

    const auto previous = tracker.regions()[0];
    require(
        !tracker.add({.x = 0.0F, .y = 0.0F, .width = std::numeric_limits<float>::quiet_NaN()}),
        "non-finite damage is rejected"
    );
    require(
        tracker.regions().size() == 1 && tracker.regions()[0].x == previous.x &&
            tracker.regions()[0].y == previous.y,
        "invalid damage leaves current regions unchanged"
    );

    const std::span<veyra::LayoutRect> no_storage;
    veyra::DamageTracker invalid_tracker(8, 8, no_storage);
    require(!invalid_tracker.valid(), "empty workspace is rejected");
    require(
        !invalid_tracker.add({.width = 1.0F, .height = 1.0F}),
        "invalid tracker rejects damage"
    );
}

}

int main() {
    try {
        check_pixel_safe_merging_and_clipping();
        check_overflow_and_reset();
        check_invalid_inputs_and_workspace();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
