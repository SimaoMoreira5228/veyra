#include "veyra/layout/toolbar.hpp"

#include <array>
#include <cstdint>
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

void check_exact_fit() {
    const std::array<veyra::ToolbarItem, 3> items{{
        {.id = 1, .width = 100.0F},
        {.id = 2, .width = 80.0F},
        {.id = 3, .width = 60.0F},
    }};

    const veyra::ToolbarRequest request{
        .items = items,
        .available_width = 256.0F,
        .overflow_button_width = 24.0F,
        .gap = 8.0F,
    };

    std::array<std::uint64_t, 3> visible{};
    std::array<std::uint64_t, 3> overflow{};

    veyra::ToolbarLayout layout{};

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, layout);

    require(result == veyra::ToolbarResult::complete, "exact fit completes");
    require(layout.visible_count == 3, "exact fit keeps every item");
    require(layout.overflow_count == 0, "exact fit spills nothing");
    require(!layout.show_overflow, "exact fit shows no chevron");
    require(visible[0] == 1 && visible[1] == 2 && visible[2] == 3, "exact fit preserves order");
}

void check_one_pixel_over() {
    const std::array<veyra::ToolbarItem, 3> items{{
        {.id = 1, .width = 100.0F},
        {.id = 2, .width = 80.0F},
        {.id = 3, .width = 60.0F},
    }};

    const veyra::ToolbarRequest request{
        .items = items,
        .available_width = 255.0F,
        .overflow_button_width = 24.0F,
        .gap = 8.0F,
    };

    std::array<std::uint64_t, 3> visible{};
    std::array<std::uint64_t, 3> overflow{};

    veyra::ToolbarLayout layout{};

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, layout);

    require(result == veyra::ToolbarResult::overflow, "one pixel over spills");
    require(layout.visible_count == 2, "one pixel over keeps two items");
    require(layout.overflow_count == 1, "one pixel over spills one item");
    require(layout.show_overflow, "one pixel over shows the chevron");
    require(visible[0] == 1 && visible[1] == 2, "visible ids stay in order");
    require(overflow[0] == 3, "the last item spills first");
}

void check_pinned_retention() {
    const std::array<veyra::ToolbarItem, 3> items{{
        {.id = 1, .width = 150.0F},
        {.id = 2, .width = 40.0F, .pinned = true},
        {.id = 3, .width = 150.0F},
    }};

    const veyra::ToolbarRequest request{
        .items = items,
        .available_width = 100.0F,
        .overflow_button_width = 20.0F,
        .gap = 0.0F,
    };

    std::array<std::uint64_t, 3> visible{};
    std::array<std::uint64_t, 3> overflow{};

    veyra::ToolbarLayout layout{};

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, layout);

    require(result == veyra::ToolbarResult::overflow, "narrow bar with pinned spills");
    require(layout.visible_count == 1, "only the pinned item stays");
    require(visible[0] == 2, "the pinned item stays visible mid-order");
    require(layout.overflow_count == 2, "both unpinned items spill");
    require(overflow[0] == 1 && overflow[1] == 3, "overflow ids stay in order");
    require(layout.show_overflow, "chevron fits next to the pinned item");
}

void check_chevron_reservation() {
    const std::array<veyra::ToolbarItem, 2> items{{
        {.id = 1, .width = 60.0F},
        {.id = 2, .width = 60.0F},
    }};

    std::array<std::uint64_t, 2> visible{};
    std::array<std::uint64_t, 2> overflow{};
    veyra::ToolbarLayout layout{};

    const veyra::ToolbarRequest reserved{
        .items = items,
        .available_width = 100.0F,
        .overflow_button_width = 20.0F,
        .gap = 0.0F,
    };

    auto result = veyra::resolve_toolbar_overflow(reserved, visible, overflow, layout);

    require(result == veyra::ToolbarResult::overflow, "reserved chevron spills the second item");
    require(layout.visible_count == 1, "reserved chevron keeps one item");
    require(overflow[0] == 2, "reserved chevron spills the second item");
    require(layout.show_overflow, "reserved chevron is shown");

    const veyra::ToolbarRequest no_button{
        .items = items,
        .available_width = 100.0F,
        .overflow_button_width = 0.0F,
        .gap = 0.0F,
    };

    result = veyra::resolve_toolbar_overflow(no_button, visible, overflow, layout);

    require(result == veyra::ToolbarResult::overflow, "zero button width still spills");
    require(layout.visible_count == 1, "zero button width keeps one item");
    require(!layout.show_overflow, "zero button width never shows a chevron");

    const veyra::ToolbarRequest roomy{
        .items = items,
        .available_width = 120.0F,
        .overflow_button_width = 0.0F,
        .gap = 0.0F,
    };

    result = veyra::resolve_toolbar_overflow(roomy, visible, overflow, layout);

    require(result == veyra::ToolbarResult::complete, "fitting items complete without a button");
    require(layout.visible_count == 2, "fitting items all stay");
    require(!layout.show_overflow, "no spill means no chevron");
}

void check_unfulfillable_pinned_set() {
    const std::array<veyra::ToolbarItem, 2> items{{
        {.id = 1, .width = 90.0F, .pinned = true},
        {.id = 2, .width = 10.0F},
    }};

    const veyra::ToolbarRequest request{
        .items = items,
        .available_width = 50.0F,
        .overflow_button_width = 10.0F,
        .gap = 0.0F,
    };

    std::array<std::uint64_t, 2> visible{};
    std::array<std::uint64_t, 2> overflow{};

    veyra::ToolbarLayout layout{};

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, layout);

    require(result == veyra::ToolbarResult::unfulfillable, "oversized pinned set is unfulfillable");
    require(layout.visible_count == 1, "unfulfillable still names the pinned item");
    require(visible[0] == 1, "unfulfillable keeps the pinned id");
    require(layout.overflow_count == 1, "unfulfillable spills the rest");
    require(overflow[0] == 2, "unfulfillable names the spilled id");
    require(!layout.show_overflow, "unfulfillable hides the chevron it cannot fit");
}

void check_nan_rejection_leaves_outputs_untouched() {
    const std::array<veyra::ToolbarItem, 2> items{{
        {.id = 1, .width = 40.0F},
        {.id = 2, .width = 40.0F},
    }};

    const float quiet_nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    const std::array<veyra::ToolbarItem, 1> nan_item{{
        {.id = 7, .width = quiet_nan},
    }};

    std::array<std::uint64_t, 2> visible{999, 999};
    std::array<std::uint64_t, 2> overflow{999, 999};

    veyra::ToolbarLayout layout{
        .visible_count = 17,
        .overflow_count = 19,
        .show_overflow = true,
    };

    const veyra::ToolbarRequest nan_available{
        .items = items,
        .available_width = quiet_nan,
        .overflow_button_width = 20.0F,
        .gap = 4.0F,
    };

    auto result = veyra::resolve_toolbar_overflow(nan_available, visible, overflow, layout);

    require(result == veyra::ToolbarResult::invalid_input, "NaN available width is rejected");

    const veyra::ToolbarRequest nan_width{
        .items = nan_item,
        .available_width = 100.0F,
        .overflow_button_width = 20.0F,
        .gap = 4.0F,
    };

    std::array<std::uint64_t, 1> narrow_visible{999};
    std::array<std::uint64_t, 1> narrow_overflow{999};

    result = veyra::resolve_toolbar_overflow(nan_width, narrow_visible, narrow_overflow, layout);

    require(result == veyra::ToolbarResult::invalid_input, "NaN item width is rejected");

    const veyra::ToolbarRequest nan_gap{
        .items = items,
        .available_width = 100.0F,
        .overflow_button_width = 20.0F,
        .gap = quiet_nan,
    };

    result = veyra::resolve_toolbar_overflow(nan_gap, visible, overflow, layout);

    require(result == veyra::ToolbarResult::invalid_input, "NaN gap is rejected");

    const veyra::ToolbarRequest infinite_button{
        .items = items,
        .available_width = 100.0F,
        .overflow_button_width = infinity,
        .gap = 4.0F,
    };

    result = veyra::resolve_toolbar_overflow(infinite_button, visible, overflow, layout);

    require(result == veyra::ToolbarResult::invalid_input, "infinite button width is rejected");
    require(layout.visible_count == 17, "rejected calls leave the visible count untouched");
    require(layout.overflow_count == 19, "rejected calls leave the overflow count untouched");
    require(layout.show_overflow, "rejected calls leave the chevron flag untouched");
    require(visible[0] == 999 && visible[1] == 999, "rejected calls leave visible ids untouched");
    require(
        overflow[0] == 999 && overflow[1] == 999,
        "rejected calls leave overflow ids untouched"
    );
    require(narrow_visible[0] == 999, "rejected calls leave narrow visible ids untouched");
    require(narrow_overflow[0] == 999, "rejected calls leave narrow overflow ids untouched");
}

void check_empty_items() {
    const std::span<const veyra::ToolbarItem> items{};

    const veyra::ToolbarRequest request{
        .items = items,
        .available_width = 200.0F,
        .overflow_button_width = 24.0F,
        .gap = 8.0F,
    };

    const std::span<std::uint64_t> visible{};
    const std::span<std::uint64_t> overflow{};

    veyra::ToolbarLayout layout{
        .visible_count = 5,
        .overflow_count = 5,
        .show_overflow = true,
    };

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, layout);

    require(result == veyra::ToolbarResult::complete, "empty items complete");
    require(layout.visible_count == 0, "empty items show nothing");
    require(layout.overflow_count == 0, "empty items spill nothing");
    require(!layout.show_overflow, "empty items show no chevron");
}

void check_gap_accounting() {
    const std::array<veyra::ToolbarItem, 2> items{{
        {.id = 1, .width = 50.0F},
        {.id = 2, .width = 50.0F},
    }};

    std::array<std::uint64_t, 2> visible{};
    std::array<std::uint64_t, 2> overflow{};
    veyra::ToolbarLayout layout{};

    const veyra::ToolbarRequest exact{
        .items = items,
        .available_width = 110.0F,
        .overflow_button_width = 0.0F,
        .gap = 10.0F,
    };

    auto result = veyra::resolve_toolbar_overflow(exact, visible, overflow, layout);

    require(result == veyra::ToolbarResult::complete, "widths plus one gap fit exactly");

    const veyra::ToolbarRequest one_short{
        .items = items,
        .available_width = 109.0F,
        .overflow_button_width = 0.0F,
        .gap = 10.0F,
    };

    result = veyra::resolve_toolbar_overflow(one_short, visible, overflow, layout);

    require(result == veyra::ToolbarResult::overflow, "one pixel under the gap spills");
    require(layout.visible_count == 1, "gap pressure keeps one item");
    require(overflow[0] == 2, "gap pressure spills the second item");

    const veyra::ToolbarRequest no_gaps{
        .items = items,
        .available_width = 109.0F,
        .overflow_button_width = 0.0F,
        .gap = 0.0F,
    };

    result = veyra::resolve_toolbar_overflow(no_gaps, visible, overflow, layout);

    require(result == veyra::ToolbarResult::complete, "zero gap fits the same widths");
}

void check_zero_width_items_in_zero_space() {
    const std::array<veyra::ToolbarItem, 2> items{{
        {.id = 1, .width = 0.0F},
        {.id = 2, .width = 0.0F},
    }};

    const veyra::ToolbarRequest request{
        .items = items,
        .available_width = 0.0F,
        .overflow_button_width = 0.0F,
        .gap = 0.0F,
    };

    std::array<std::uint64_t, 2> visible{};
    std::array<std::uint64_t, 2> overflow{};

    veyra::ToolbarLayout layout{};

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, layout);

    require(result == veyra::ToolbarResult::complete, "zero-width items fit in zero space");
    require(layout.visible_count == 2, "zero-width items stay visible");
    require(layout.overflow_count == 0, "zero-width items spill nothing");
}

void check_rejections() {
    const std::array<veyra::ToolbarItem, 1> negative_width{{
        {.id = 1, .width = -5.0F},
    }};

    const std::array<veyra::ToolbarItem, 1> zero_id{{
        {.id = 0, .width = 10.0F},
    }};

    const std::array<veyra::ToolbarItem, 2> pair{{
        {.id = 1, .width = 40.0F},
        {.id = 2, .width = 40.0F},
    }};

    std::array<std::uint64_t, 2> visible{};
    std::array<std::uint64_t, 2> overflow{};
    std::array<std::uint64_t, 1> narrow{};

    veyra::ToolbarLayout layout{};

    veyra::ToolbarRequest request{
        .items = negative_width,
        .available_width = 100.0F,
        .overflow_button_width = 20.0F,
        .gap = 4.0F,
    };

    require(
        veyra::resolve_toolbar_overflow(request, visible, overflow, layout) ==
            veyra::ToolbarResult::invalid_input,
        "negative item width is rejected"
    );

    request.items = zero_id;

    require(
        veyra::resolve_toolbar_overflow(request, visible, overflow, layout) ==
            veyra::ToolbarResult::invalid_input,
        "zero item id is rejected"
    );

    request.items = pair;
    request.available_width = -1.0F;

    require(
        veyra::resolve_toolbar_overflow(request, visible, overflow, layout) ==
            veyra::ToolbarResult::invalid_input,
        "negative available width is rejected"
    );

    request.available_width = 100.0F;
    request.gap = -2.0F;

    require(
        veyra::resolve_toolbar_overflow(request, visible, overflow, layout) ==
            veyra::ToolbarResult::invalid_input,
        "negative gap is rejected"
    );

    request.gap = 4.0F;
    request.overflow_button_width = -3.0F;

    require(
        veyra::resolve_toolbar_overflow(request, visible, overflow, layout) ==
            veyra::ToolbarResult::invalid_input,
        "negative button width is rejected"
    );

    request.overflow_button_width = 20.0F;

    require(
        veyra::resolve_toolbar_overflow(request, narrow, overflow, layout) ==
            veyra::ToolbarResult::invalid_input,
        "undersized visible storage is rejected"
    );

    require(
        veyra::resolve_toolbar_overflow(request, visible, narrow, layout) ==
            veyra::ToolbarResult::invalid_input,
        "undersized overflow storage is rejected"
    );
}

}

int main() {
    try {
        check_exact_fit();
        check_one_pixel_over();
        check_pinned_retention();
        check_chevron_reservation();
        check_unfulfillable_pinned_set();
        check_nan_rejection_leaves_outputs_untouched();
        check_empty_items();
        check_gap_accounting();
        check_zero_width_items_in_zero_space();
        check_rejections();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
