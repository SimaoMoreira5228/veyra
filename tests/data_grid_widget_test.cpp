#include "veyra/widgets/data/data_grid.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

const veyra::FillRect& as_fill(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* fill = std::get_if<veyra::FillRect>(&command);
    require(fill != nullptr, message);
    return *fill;
}

const veyra::StrokeRect& as_stroke(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* stroke = std::get_if<veyra::StrokeRect>(&command);
    require(stroke != nullptr, message);
    return *stroke;
}

void check_header_appearance_tracks_state() {
    const auto theme = veyra::Theme{};

    const auto plain = veyra::data_grid_header_appearance(theme, {});
    require(
        plain.background == theme.color(veyra::tokens::color_surface) &&
            plain.text == theme.color(veyra::tokens::color_text_primary),
        "a plain header rests on the surface with primary text"
    );

    const auto sorted = veyra::data_grid_header_appearance(theme, {.sorted_asc = true});
    require(
        sorted.background == theme.color(veyra::tokens::color_action_hovered) &&
            sorted.sort_arrow == theme.color(veyra::tokens::color_action_primary),
        "a sorted header lifts to the hover tint with a primary arrow"
    );

    const auto pinned = veyra::data_grid_header_appearance(theme, {.pinned = true});
    require(
        pinned.background == theme.color(veyra::tokens::color_action_hovered) &&
            pinned.pinned_separator == theme.color(veyra::tokens::color_action_primary),
        "a pinned header shares the hover tint with a primary divider"
    );
}

void check_cell_appearance_tracks_state() {
    const auto theme = veyra::Theme{};

    const auto idle = veyra::data_grid_cell_appearance(theme, {});
    require(
        idle.background == theme.color(veyra::tokens::color_surface),
        "an idle cell rests on the surface"
    );

    const auto picked = veyra::data_grid_cell_appearance(theme, {.selected = true});
    require(
        picked.background == theme.color(veyra::tokens::color_action_hovered),
        "a selected cell lifts to the hover tint"
    );

    const auto ranged = veyra::data_grid_cell_appearance(theme, {.in_range = true});
    require(
        ranged.background == theme.color(veyra::tokens::color_action_hovered) &&
            ranged.range_outline == theme.color(veyra::tokens::color_action_primary),
        "a ranged cell tints and outlines in the primary color"
    );

    const auto edited = veyra::data_grid_cell_appearance(theme, {.edit_pending = true});
    const auto cursor = veyra::data_grid_cell_appearance(theme, {.current = true});
    require(
        edited.range_outline == cursor.range_outline &&
            edited.range_outline == theme.color(veyra::tokens::color_action_primary),
        "a pending edit reads exactly like the current cell"
    );
}

void check_header_emit_geometry() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::DataGridColumn, 3> columns{{
        {.id = 1,
         .width = 100.0F,
         .min_width = 40.0F,
         .max_width = 200.0F,
         .pinned = veyra::DataGridPin::left},
        {.id = 2, .width = 80.0F, .min_width = 40.0F, .max_width = 200.0F},
        {.id = 3,
         .width = 120.0F,
         .min_width = 40.0F,
         .max_width = 200.0F,
         .pinned = veyra::DataGridPin::right},
    }};
    constexpr std::array<veyra::SortSpec, 2> sorts{{
        {.column = 3, .direction = veyra::SortDirection::descending},
        {.column = 1, .direction = veyra::SortDirection::ascending},
    }};

    std::array<veyra::DisplayCommand, 16> storage{};
    const auto emitted = veyra::emit_data_grid_header(
        theme,
        columns,
        sorts,
        {.x = 8.0F, .y = 4.0F, .row_height = 32.0F},
        storage
    );
    require(
        !emitted.overflow && emitted.count == 7,
        "three pinned and sorted headers take seven paints"
    );

    const auto band = as_fill(storage[0], "the band leads the header");
    require(
        band.bounds.x == 8.0F && band.bounds.y == 4.0F && band.bounds.width == 300.0F &&
            band.bounds.height == 32.0F && band.color == theme.color(veyra::tokens::color_surface),
        "the band spans every column at the density height"
    );

    const auto arrow_asc = as_fill(storage[2], "the ascending arrow follows its tint block");
    const auto arrow_desc = as_fill(storage[6], "the descending arrow follows its tint block");
    require(
        arrow_asc.bounds.y < 4.0F + 16.0F && arrow_desc.bounds.y > 4.0F + 16.0F,
        "ascending sits above center and descending below it"
    );
    require(
        arrow_asc.bounds.width == 8.0F && arrow_asc.bounds.height == 3.0F &&
            arrow_desc.bounds.width == 8.0F && arrow_desc.bounds.height == 3.0F &&
            arrow_asc.color == theme.color(veyra::tokens::color_action_primary),
        "both arrows are primary bars resolved from the sort list position"
    );

    const auto divider = as_fill(storage[3], "the pin boundary follows the arrow");
    require(
        divider.bounds.width == 2.0F &&
            divider.color == theme.color(veyra::tokens::color_action_primary),
        "the pinned divider is twice as wide in the primary color"
    );

    const auto middle = as_fill(storage[4], "the middle edge follows the first divider");
    require(
        middle.bounds.width == 2.0F &&
            middle.color == theme.color(veyra::tokens::color_action_primary),
        "the unpinned to right-pinned edge is a divider too"
    );

    constexpr std::array<veyra::DataGridColumn, 2> loose{{
        {.id = 5, .width = 60.0F, .min_width = 16.0F, .max_width = 128.0F},
        {.id = 6, .width = 60.0F, .min_width = 16.0F, .max_width = 128.0F},
    }};
    std::array<veyra::DisplayCommand, 8> plain_storage{};
    const auto plain_emit = veyra::emit_data_grid_header(
        theme,
        loose,
        {},
        {.x = 0.0F, .y = 0.0F, .row_height = 32.0F},
        plain_storage
    );
    require(
        !plain_emit.overflow && plain_emit.count == 2 &&
            as_fill(plain_storage[1], "the free edge trails the band").bounds.width == 1.0F &&
            as_fill(plain_storage[1], "the free edge trails the band").color ==
                theme.color(veyra::tokens::color_text_disabled),
        "an unpinned edge stays a single muted pixel"
    );
}

void check_header_emit_density_and_overflow() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::DataGridColumn, 1> columns{{
        {.id = 9, .width = 64.0F, .min_width = 16.0F, .max_width = 128.0F},
    }};

    auto grid = veyra::DataGridController{};
    grid.set_columns(columns);
    for (const auto density :
         {veyra::DataGridDensity::compact,
          veyra::DataGridDensity::standard,
          veyra::DataGridDensity::comfortable}) {
        grid.set_density(density);
        std::array<veyra::DisplayCommand, 4> storage{};
        const auto emitted = veyra::emit_data_grid_header(
            theme,
            columns,
            {},
            {.x = 0.0F, .y = 0.0F, .row_height = grid.density_row_height()},
            storage
        );
        require(!emitted.overflow && emitted.count == 1, "a lone plain header is just its band");
        require(
            as_fill(storage[0], "the band is the only paint").bounds.height ==
                grid.density_row_height(),
            "the band height follows the controller density"
        );
    }

    std::array<veyra::DisplayCommand, 0> tight{};
    const auto starved = veyra::emit_data_grid_header(
        theme,
        columns,
        {},
        {.x = 0.0F, .y = 0.0F, .row_height = 32.0F},
        tight
    );
    require(starved.overflow && starved.count == 0, "a short span overflows without partial paint");
}

void check_range_emit_outlines() {
    const auto theme = veyra::Theme{};
    constexpr veyra::DataGridRangeGeometry both{
        .bounds = {.x = 10.0F, .y = 20.0F, .width = 100.0F, .height = 60.0F},
        .current = {.x = 30.0F, .y = 40.0F, .width = 50.0F, .height = 32.0F},
        .has_range = true,
        .has_current = true,
    };

    std::array<veyra::DisplayCommand, veyra::data_grid_range_capacity()> storage{};
    const auto emitted = veyra::emit_data_grid_range(theme, both, storage);
    require(!emitted.overflow && emitted.count == 2, "range plus cursor takes two outlines");

    const auto range = as_stroke(storage[0], "the range outline leads");
    require(
        range.bounds.x == 10.0F && range.bounds.width == 100.0F && range.thickness == 1.0F &&
            range.color == theme.color(veyra::tokens::color_action_primary),
        "the range reads as a single-pixel primary outline"
    );

    const auto cursor = as_stroke(storage[1], "the cursor outline trails");
    require(
        cursor.bounds.x == 30.0F && cursor.bounds.y == 40.0F && cursor.thickness == 2.0F &&
            cursor.color == theme.color(veyra::tokens::color_action_primary),
        "the current cell reads as a double-weight primary outline"
    );

    constexpr veyra::DataGridRangeGeometry editing{
        .current = {.x = 30.0F, .y = 40.0F, .width = 50.0F, .height = 32.0F},
        .edit_pending = true,
    };
    const auto pending = veyra::emit_data_grid_range(theme, editing, storage);
    require(
        !pending.overflow && pending.count == 1 &&
            as_stroke(storage[0], "the edit outline stands alone").thickness == 2.0F,
        "a pending edit keeps the current outline with no cursor flag"
    );

    constexpr veyra::DataGridRangeGeometry empty{};
    const auto none = veyra::emit_data_grid_range(theme, empty, storage);
    require(!none.overflow && none.count == 0, "no range and no cursor paints nothing");

    std::array<veyra::DisplayCommand, 1> tight{};
    const auto starved = veyra::emit_data_grid_range(theme, both, tight);
    require(starved.overflow && starved.count == 0, "a short span overflows without partial paint");
}

void check_cell_background_rule() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect bounds{.x = 2.0F, .y = 2.0F, .width = 40.0F, .height = 24.0F};

    std::array<veyra::DisplayCommand, 1> storage{};
    const auto idle = veyra::emit_data_grid_cell_background(
        theme,
        veyra::data_grid_cell_appearance(theme, {}),
        bounds,
        storage
    );
    require(!idle.overflow && idle.count == 0, "surface cells emit no paint at all");

    const auto picked = veyra::emit_data_grid_cell_background(
        theme,
        veyra::data_grid_cell_appearance(theme, {.selected = true}),
        bounds,
        storage
    );
    require(
        !picked.overflow && picked.count == 1 &&
            as_fill(storage[0], "the selection paints").color ==
                theme.color(veyra::tokens::color_action_hovered),
        "only a tinted cell earns its fill"
    );

    std::array<veyra::DisplayCommand, 0> tight{};
    const auto starved = veyra::emit_data_grid_cell_background(
        theme,
        veyra::data_grid_cell_appearance(theme, {.selected = true}),
        bounds,
        tight
    );
    require(starved.overflow, "a tinted cell with no room reports overflow");
}

void check_theme_override_repaints_chrome() {
    auto theme = veyra::Theme{};
    theme.set(veyra::tokens::color_surface, veyra::Srgba8{16, 18, 24});
    theme.set(veyra::tokens::color_text_primary, veyra::Srgba8{232, 234, 240});
    theme.set(veyra::tokens::color_action_primary, veyra::Srgba8{90, 160, 255});
    theme.set(veyra::tokens::color_action_hovered, veyra::Srgba8{48, 80, 140});

    const auto header = veyra::data_grid_header_appearance(theme, {.sorted_desc = true});
    require(
        header.background == veyra::Srgba8{48, 80, 140} &&
            header.sort_arrow == veyra::Srgba8{90, 160, 255},
        "the sorted header follows the dark overlay and primary"
    );

    const auto cell = veyra::data_grid_cell_appearance(theme, {.selected = true, .current = true});
    require(
        cell.background == veyra::Srgba8{48, 80, 140} &&
            cell.range_outline == veyra::Srgba8{90, 160, 255},
        "the selected cell follows the same dark overlay"
    );

    constexpr std::array<veyra::DataGridColumn, 1> columns{{
        {.id = 4, .width = 72.0F, .min_width = 16.0F, .max_width = 128.0F},
    }};
    std::array<veyra::DisplayCommand, 4> storage{};
    const auto emitted = veyra::emit_data_grid_header(
        theme,
        columns,
        {},
        {.x = 0.0F, .y = 0.0F, .row_height = 32.0F},
        storage
    );
    require(
        !emitted.overflow &&
            as_fill(storage[0], "the dark band leads").color == veyra::Srgba8{16, 18, 24},
        "the emitted band tracks the overridden surface"
    );
}

}

int main() {
    try {
        check_header_appearance_tracks_state();
        check_cell_appearance_tracks_state();
        check_header_emit_geometry();
        check_header_emit_density_and_overflow();
        check_range_emit_outlines();
        check_cell_background_rule();
        check_theme_override_repaints_chrome();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
