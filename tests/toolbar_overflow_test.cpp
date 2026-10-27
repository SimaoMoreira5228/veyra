#include "veyra/layout/panels/toolbar.hpp"

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
    std::array<std::uint64_t, 3> folded{};

    veyra::ToolbarLayout layout{};

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::complete, "exact fit completes");
    require(layout.visible_count == 3, "exact fit keeps every item");
    require(layout.overflow_count == 0, "exact fit spills nothing");
    require(layout.folded_count == 0, "exact fit folds nothing");
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
    std::array<std::uint64_t, 3> folded{};

    veyra::ToolbarLayout layout{};

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::overflow, "one pixel over spills");
    require(layout.visible_count == 2, "one pixel over keeps two items");
    require(layout.overflow_count == 1, "one pixel over spills one item");
    require(layout.folded_count == 0, "one pixel over folds nothing");
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
    std::array<std::uint64_t, 3> folded{};

    veyra::ToolbarLayout layout{};

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::overflow, "narrow bar with pinned spills");
    require(layout.visible_count == 1, "only the pinned item stays");
    require(visible[0] == 2, "the pinned item stays visible mid-order");
    require(layout.overflow_count == 2, "both unpinned items spill");
    require(overflow[0] == 1 && overflow[1] == 3, "overflow ids stay in order");
    require(layout.folded_count == 0, "unfoldable pins fold nothing");
    require(layout.show_overflow, "chevron fits next to the pinned item");
}

void check_chevron_reservation() {
    const std::array<veyra::ToolbarItem, 2> items{{
        {.id = 1, .width = 60.0F},
        {.id = 2, .width = 60.0F},
    }};

    std::array<std::uint64_t, 2> visible{};
    std::array<std::uint64_t, 2> overflow{};
    std::array<std::uint64_t, 2> folded{};
    veyra::ToolbarLayout layout{};

    const veyra::ToolbarRequest reserved{
        .items = items,
        .available_width = 100.0F,
        .overflow_button_width = 20.0F,
        .gap = 0.0F,
    };

    auto result = veyra::resolve_toolbar_overflow(reserved, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::overflow, "reserved chevron spills the second item");
    require(layout.visible_count == 1, "reserved chevron keeps one item");
    require(overflow[0] == 2, "reserved chevron spills the second item");
    require(layout.folded_count == 0, "reserved chevron folds nothing");
    require(layout.show_overflow, "reserved chevron is shown");

    const veyra::ToolbarRequest no_button{
        .items = items,
        .available_width = 100.0F,
        .overflow_button_width = 0.0F,
        .gap = 0.0F,
    };

    result = veyra::resolve_toolbar_overflow(no_button, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::overflow, "zero button width still spills");
    require(layout.visible_count == 1, "zero button width keeps one item");
    require(!layout.show_overflow, "zero button width never shows a chevron");

    const veyra::ToolbarRequest roomy{
        .items = items,
        .available_width = 120.0F,
        .overflow_button_width = 0.0F,
        .gap = 0.0F,
    };

    result = veyra::resolve_toolbar_overflow(roomy, visible, overflow, folded, layout);

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
    std::array<std::uint64_t, 2> folded{};

    veyra::ToolbarLayout layout{};

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::unfulfillable, "oversized pinned set is unfulfillable");
    require(layout.visible_count == 1, "unfulfillable still names the pinned item");
    require(visible[0] == 1, "unfulfillable keeps the pinned id");
    require(layout.overflow_count == 1, "unfulfillable spills the rest");
    require(overflow[0] == 2, "unfulfillable names the spilled id");
    require(layout.folded_count == 0, "unfoldable pins fold nothing");
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
    std::array<std::uint64_t, 2> folded{999, 999};

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

    auto result = veyra::resolve_toolbar_overflow(nan_available, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::invalid_input, "NaN available width is rejected");

    const veyra::ToolbarRequest nan_width{
        .items = nan_item,
        .available_width = 100.0F,
        .overflow_button_width = 20.0F,
        .gap = 4.0F,
    };

    std::array<std::uint64_t, 1> narrow_visible{999};
    std::array<std::uint64_t, 1> narrow_overflow{999};
    std::array<std::uint64_t, 1> narrow_folded{999};

    result = veyra::resolve_toolbar_overflow(
        nan_width,
        narrow_visible,
        narrow_overflow,
        narrow_folded,
        layout
    );

    require(result == veyra::ToolbarResult::invalid_input, "NaN item width is rejected");

    const veyra::ToolbarRequest nan_gap{
        .items = items,
        .available_width = 100.0F,
        .overflow_button_width = 20.0F,
        .gap = quiet_nan,
    };

    result = veyra::resolve_toolbar_overflow(nan_gap, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::invalid_input, "NaN gap is rejected");

    const veyra::ToolbarRequest infinite_button{
        .items = items,
        .available_width = 100.0F,
        .overflow_button_width = infinity,
        .gap = 4.0F,
    };

    result = veyra::resolve_toolbar_overflow(infinite_button, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::invalid_input, "infinite button width is rejected");
    require(layout.visible_count == 17, "rejected calls leave the visible count untouched");
    require(layout.overflow_count == 19, "rejected calls leave the overflow count untouched");
    require(layout.folded_count == 0, "rejected calls leave the folded count untouched");
    require(layout.show_overflow, "rejected calls leave the chevron flag untouched");
    require(visible[0] == 999 && visible[1] == 999, "rejected calls leave visible ids untouched");
    require(
        overflow[0] == 999 && overflow[1] == 999,
        "rejected calls leave overflow ids untouched"
    );
    require(folded[0] == 999 && folded[1] == 999, "rejected calls leave folded ids untouched");
    require(narrow_visible[0] == 999, "rejected calls leave narrow visible ids untouched");
    require(narrow_overflow[0] == 999, "rejected calls leave narrow overflow ids untouched");
    require(narrow_folded[0] == 999, "rejected calls leave narrow folded ids untouched");
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
    const std::span<std::uint64_t> folded{};

    veyra::ToolbarLayout layout{
        .visible_count = 5,
        .overflow_count = 5,
        .show_overflow = true,
    };

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::complete, "empty items complete");
    require(layout.visible_count == 0, "empty items show nothing");
    require(layout.overflow_count == 0, "empty items spill nothing");
    require(layout.folded_count == 0, "empty items fold nothing");
    require(!layout.show_overflow, "empty items show no chevron");
}

void check_gap_accounting() {
    const std::array<veyra::ToolbarItem, 2> items{{
        {.id = 1, .width = 50.0F},
        {.id = 2, .width = 50.0F},
    }};

    std::array<std::uint64_t, 2> visible{};
    std::array<std::uint64_t, 2> overflow{};
    std::array<std::uint64_t, 2> folded{};
    veyra::ToolbarLayout layout{};

    const veyra::ToolbarRequest exact{
        .items = items,
        .available_width = 110.0F,
        .overflow_button_width = 0.0F,
        .gap = 10.0F,
    };

    auto result = veyra::resolve_toolbar_overflow(exact, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::complete, "widths plus one gap fit exactly");

    const veyra::ToolbarRequest one_short{
        .items = items,
        .available_width = 109.0F,
        .overflow_button_width = 0.0F,
        .gap = 10.0F,
    };

    result = veyra::resolve_toolbar_overflow(one_short, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::overflow, "one pixel under the gap spills");
    require(layout.visible_count == 1, "gap pressure keeps one item");
    require(overflow[0] == 2, "gap pressure spills the second item");

    const veyra::ToolbarRequest no_gaps{
        .items = items,
        .available_width = 109.0F,
        .overflow_button_width = 0.0F,
        .gap = 0.0F,
    };

    result = veyra::resolve_toolbar_overflow(no_gaps, visible, overflow, folded, layout);

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
    std::array<std::uint64_t, 2> folded{};

    veyra::ToolbarLayout layout{};

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::complete, "zero-width items fit in zero space");
    require(layout.visible_count == 2, "zero-width items stay visible");
    require(layout.overflow_count == 0, "zero-width items spill nothing");
    require(layout.folded_count == 0, "zero-width items fold nothing");
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
    std::array<std::uint64_t, 2> folded{};
    std::array<std::uint64_t, 1> narrow{};

    veyra::ToolbarLayout layout{};

    veyra::ToolbarRequest request{
        .items = negative_width,
        .available_width = 100.0F,
        .overflow_button_width = 20.0F,
        .gap = 4.0F,
    };

    require(
        veyra::resolve_toolbar_overflow(request, visible, overflow, folded, layout) ==
            veyra::ToolbarResult::invalid_input,
        "negative item width is rejected"
    );

    request.items = zero_id;

    require(
        veyra::resolve_toolbar_overflow(request, visible, overflow, folded, layout) ==
            veyra::ToolbarResult::invalid_input,
        "zero item id is rejected"
    );

    request.items = pair;
    request.available_width = -1.0F;

    require(
        veyra::resolve_toolbar_overflow(request, visible, overflow, folded, layout) ==
            veyra::ToolbarResult::invalid_input,
        "negative available width is rejected"
    );

    request.available_width = 100.0F;
    request.gap = -2.0F;

    require(
        veyra::resolve_toolbar_overflow(request, visible, overflow, folded, layout) ==
            veyra::ToolbarResult::invalid_input,
        "negative gap is rejected"
    );

    request.gap = 4.0F;
    request.overflow_button_width = -3.0F;

    require(
        veyra::resolve_toolbar_overflow(request, visible, overflow, folded, layout) ==
            veyra::ToolbarResult::invalid_input,
        "negative button width is rejected"
    );

    request.overflow_button_width = 20.0F;

    require(
        veyra::resolve_toolbar_overflow(request, narrow, overflow, folded, layout) ==
            veyra::ToolbarResult::invalid_input,
        "undersized visible storage is rejected"
    );

    require(
        veyra::resolve_toolbar_overflow(request, visible, narrow, folded, layout) ==
            veyra::ToolbarResult::invalid_input,
        "undersized overflow storage is rejected"
    );

    require(
        veyra::resolve_toolbar_overflow(request, visible, overflow, narrow, layout) ==
            veyra::ToolbarResult::invalid_input,
        "undersized folded storage is rejected"
    );
}

void check_fold_one_then_fit() {
    const std::array<veyra::ToolbarItem, 3> items{{
        {.id = 1, .width = 100.0F},
        {.id = 2, .width = 80.0F, .collapsed_width = 40.0F},
        {.id = 3, .width = 60.0F, .collapsed_width = 30.0F},
    }};

    const veyra::ToolbarRequest request{
        .items = items,
        .available_width = 226.0F,
        .overflow_button_width = 24.0F,
        .gap = 8.0F,
    };

    std::array<std::uint64_t, 3> visible{};
    std::array<std::uint64_t, 3> overflow{};
    std::array<std::uint64_t, 3> folded{};

    veyra::ToolbarLayout layout{};

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::complete, "one fold absorbs the shortfall");
    require(layout.visible_count == 3, "one fold keeps every item");
    require(layout.overflow_count == 0, "one fold spills nothing");
    require(!layout.show_overflow, "one fold shows no chevron");
    require(visible[0] == 1 && visible[1] == 2 && visible[2] == 3, "one fold preserves order");
    require(layout.folded_count == 1, "one fold folds exactly one item");
    require(folded[0] == 3, "folding starts at the trailing end");
}

void check_fold_all_then_spill() {
    const std::array<veyra::ToolbarItem, 3> items{{
        {.id = 1, .width = 100.0F, .collapsed_width = 60.0F},
        {.id = 2, .width = 80.0F, .collapsed_width = 40.0F},
        {.id = 3, .width = 60.0F, .collapsed_width = 30.0F},
    }};

    const veyra::ToolbarRequest request{
        .items = items,
        .available_width = 120.0F,
        .overflow_button_width = 24.0F,
        .gap = 8.0F,
    };

    std::array<std::uint64_t, 3> visible{};
    std::array<std::uint64_t, 3> overflow{};
    std::array<std::uint64_t, 3> folded{};

    veyra::ToolbarLayout layout{};

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::overflow, "exhausted folds spill");
    require(layout.visible_count == 1, "exhausted folds keep the folded lead item");
    require(visible[0] == 1, "the lead item survives on its folded width");
    require(layout.overflow_count == 2, "exhausted folds spill two items");
    require(overflow[0] == 2 && overflow[1] == 3, "spilled ids stay in order");
    require(layout.folded_count == 1, "the surviving item reports folded");
    require(folded[0] == 1, "the surviving folded id is reported");
    require(layout.show_overflow, "the chevron fits next to the folded item");
}

void check_pin_fold_interplay() {
    const std::array<veyra::ToolbarItem, 3> items{{
        {.id = 1, .width = 150.0F, .collapsed_width = 60.0F, .pinned = true},
        {.id = 2, .width = 150.0F},
        {.id = 3, .width = 40.0F},
    }};

    const veyra::ToolbarRequest request{
        .items = items,
        .available_width = 100.0F,
        .overflow_button_width = 20.0F,
        .gap = 0.0F,
    };

    std::array<std::uint64_t, 3> visible{};
    std::array<std::uint64_t, 3> overflow{};
    std::array<std::uint64_t, 3> folded{};

    veyra::ToolbarLayout layout{};

    const auto result = veyra::resolve_toolbar_overflow(request, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::overflow, "folded pins still spill the rest");
    require(layout.visible_count == 1, "only the folded pin stays");
    require(visible[0] == 1, "the pin stays visible mid-order while folded");
    require(layout.overflow_count == 2, "both unpinned items spill");
    require(overflow[0] == 2 && overflow[1] == 3, "overflow ids stay in order");
    require(layout.folded_count == 1, "the pin reports folded");
    require(folded[0] == 1, "the folded pin id is reported");
    require(layout.show_overflow, "chevron fits next to the folded pin");
}

void check_invalid_collapsed_widths() {
    const std::array<veyra::ToolbarItem, 1> too_wide{{
        {.id = 1, .width = 40.0F, .collapsed_width = 41.0F},
    }};

    const float quiet_nan = std::numeric_limits<float>::quiet_NaN();

    const std::array<veyra::ToolbarItem, 1> nan_collapsed{{
        {.id = 1, .width = 40.0F, .collapsed_width = quiet_nan},
    }};

    const std::array<veyra::ToolbarItem, 1> negative_collapsed{{
        {.id = 1, .width = 40.0F, .collapsed_width = -1.0F},
    }};

    const std::array<veyra::ToolbarItem, 2> pair{{
        {.id = 1, .width = 40.0F},
        {.id = 2, .width = 40.0F},
    }};

    std::array<std::uint64_t, 2> visible{999, 999};
    std::array<std::uint64_t, 2> overflow{999, 999};
    std::array<std::uint64_t, 2> folded{999, 999};

    veyra::ToolbarLayout layout{
        .visible_count = 17,
        .overflow_count = 19,
        .show_overflow = true,
    };

    veyra::ToolbarRequest request{
        .items = too_wide,
        .available_width = 100.0F,
        .overflow_button_width = 20.0F,
        .gap = 4.0F,
    };

    std::array<std::uint64_t, 1> narrow_visible{999};
    std::array<std::uint64_t, 1> narrow_overflow{999};
    std::array<std::uint64_t, 1> narrow_folded{999};

    require(
        veyra::resolve_toolbar_overflow(
            request,
            narrow_visible,
            narrow_overflow,
            narrow_folded,
            layout
        ) == veyra::ToolbarResult::invalid_input,
        "collapsed wider than full width is rejected"
    );

    request.items = nan_collapsed;

    require(
        veyra::resolve_toolbar_overflow(
            request,
            narrow_visible,
            narrow_overflow,
            narrow_folded,
            layout
        ) == veyra::ToolbarResult::invalid_input,
        "NaN collapsed width is rejected"
    );

    request.items = negative_collapsed;

    require(
        veyra::resolve_toolbar_overflow(
            request,
            narrow_visible,
            narrow_overflow,
            narrow_folded,
            layout
        ) == veyra::ToolbarResult::invalid_input,
        "negative collapsed width is rejected"
    );

    require(layout.visible_count == 17, "rejected folds leave the visible count untouched");
    require(layout.overflow_count == 19, "rejected folds leave the overflow count untouched");
    require(layout.folded_count == 0, "rejected folds leave the folded count untouched");
    require(layout.show_overflow, "rejected folds leave the chevron flag untouched");
    require(narrow_folded[0] == 999, "rejected folds leave narrow folded ids untouched");

    const std::array<veyra::ToolbarItem, 2> equal_widths{{
        {.id = 1, .width = 60.0F, .collapsed_width = 60.0F},
        {.id = 2, .width = 60.0F, .collapsed_width = 60.0F},
    }};

    const veyra::ToolbarRequest equal_request{
        .items = equal_widths,
        .available_width = 100.0F,
        .overflow_button_width = 20.0F,
        .gap = 0.0F,
    };

    const auto equal_result =
        veyra::resolve_toolbar_overflow(equal_request, visible, overflow, folded, layout);

    require(equal_result == veyra::ToolbarResult::overflow, "equal widths never fold");
    require(layout.visible_count == 1, "equal widths keep one item");
    require(layout.folded_count == 0, "equal widths report nothing folded");
    require(overflow[0] == 2, "equal widths spill the second item");

    const veyra::ToolbarRequest zero_collapsed{
        .items = pair,
        .available_width = 70.0F,
        .overflow_button_width = 20.0F,
        .gap = 0.0F,
    };

    const auto zero_result =
        veyra::resolve_toolbar_overflow(zero_collapsed, visible, overflow, folded, layout);

    require(zero_result == veyra::ToolbarResult::overflow, "zero collapsed width never folds");
    require(layout.folded_count == 0, "zero collapsed width reports nothing folded");
}

void check_unfulfillable_with_folding() {
    const std::array<veyra::ToolbarItem, 2> items{{
        {.id = 1, .width = 90.0F, .collapsed_width = 40.0F, .pinned = true},
        {.id = 2, .width = 10.0F},
    }};

    std::array<std::uint64_t, 2> visible{};
    std::array<std::uint64_t, 2> overflow{};
    std::array<std::uint64_t, 2> folded{};

    veyra::ToolbarLayout layout{};

    const veyra::ToolbarRequest rescued{
        .items = items,
        .available_width = 50.0F,
        .overflow_button_width = 10.0F,
        .gap = 0.0F,
    };

    auto result = veyra::resolve_toolbar_overflow(rescued, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::complete, "folding rescues the pinned set");
    require(layout.visible_count == 2, "rescued pins keep every item");
    require(layout.overflow_count == 0, "rescued pins spill nothing");
    require(layout.folded_count == 1, "rescued pins report the fold");
    require(folded[0] == 1, "rescued pins name the folded pin");
    require(!layout.show_overflow, "rescued pins show no chevron");

    const veyra::ToolbarRequest doomed{
        .items = items,
        .available_width = 30.0F,
        .overflow_button_width = 10.0F,
        .gap = 0.0F,
    };

    result = veyra::resolve_toolbar_overflow(doomed, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::unfulfillable, "folded pins can still not fit");
    require(layout.visible_count == 1, "doomed pins still name the pinned item");
    require(visible[0] == 1, "doomed pins keep the pinned id");
    require(layout.overflow_count == 1, "doomed pins spill the rest");
    require(overflow[0] == 2, "doomed pins name the spilled id");
    require(layout.folded_count == 1, "doomed pins report the folded pin");
    require(folded[0] == 1, "doomed pins name the folded pin id");
    require(!layout.show_overflow, "doomed pins hide the chevron they cannot fit");
}

void check_folded_output_order() {
    const std::array<veyra::ToolbarItem, 3> items{{
        {.id = 1, .width = 100.0F, .collapsed_width = 50.0F},
        {.id = 2, .width = 100.0F},
        {.id = 3, .width = 100.0F, .collapsed_width = 50.0F},
    }};

    std::array<std::uint64_t, 3> visible{};
    std::array<std::uint64_t, 3> overflow{};
    std::array<std::uint64_t, 3> folded{};

    veyra::ToolbarLayout layout{};

    const veyra::ToolbarRequest one_fold{
        .items = items,
        .available_width = 250.0F,
        .overflow_button_width = 0.0F,
        .gap = 0.0F,
    };

    auto result = veyra::resolve_toolbar_overflow(one_fold, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::complete, "one trailing fold fits");
    require(layout.visible_count == 3, "one trailing fold keeps every item");
    require(layout.folded_count == 1, "one trailing fold folds exactly one item");
    require(folded[0] == 3, "the trailing foldable folds while the lead foldable holds");

    const veyra::ToolbarRequest two_folds{
        .items = items,
        .available_width = 200.0F,
        .overflow_button_width = 0.0F,
        .gap = 0.0F,
    };

    result = veyra::resolve_toolbar_overflow(two_folds, visible, overflow, folded, layout);

    require(result == veyra::ToolbarResult::complete, "two folds fit the narrow bar");
    require(layout.visible_count == 3, "two folds keep every item");
    require(layout.folded_count == 2, "two folds fold both foldables");
    require(folded[0] == 1 && folded[1] == 3, "folded ids come back in input order");
    require(visible[0] == 1 && visible[1] == 2 && visible[2] == 3, "visible ids stay in order");
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
        check_fold_one_then_fit();
        check_fold_all_then_spill();
        check_pin_fold_interplay();
        check_invalid_collapsed_widths();
        check_unfulfillable_with_folding();
        check_folded_output_order();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
