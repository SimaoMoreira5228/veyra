#include "veyra/layout/grid/grid.hpp"
#include "grid_placement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace veyra {
namespace {

using grid_detail::has_automatic_placement;
using grid_detail::item_placement;
using grid_detail::resolve_placements;

bool is_finite_nonnegative(float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

bool valid_container(const GridContainer& container) noexcept {
    return is_finite_nonnegative(container.width) && is_finite_nonnegative(container.height) &&
        is_finite_nonnegative(container.padding.top) &&
        is_finite_nonnegative(container.padding.right) &&
        is_finite_nonnegative(container.padding.bottom) &&
        is_finite_nonnegative(container.padding.left) && is_finite_nonnegative(container.gap);
}

bool valid_tracks(std::span<const GridTrack> tracks) noexcept {
    for (const auto& track : tracks) {
        if (!is_finite_nonnegative(track.minimum) || !is_finite_nonnegative(track.maximum) ||
            !is_finite_nonnegative(track.fraction) || track.minimum > track.maximum) {
            return false;
        }
    }
    return true;
}

bool valid_items(
    std::span<const GridItem> items,
    std::size_t row_count,
    std::size_t column_count
) noexcept {
    for (const auto& item : items) {
        if (item.row_span == 0 || item.column_span == 0) {
            return false;
        }
        if (item.row && (*item.row >= row_count || item.row_span > row_count - *item.row)) {
            return false;
        }
        if (item.column &&
            (*item.column >= column_count || item.column_span > column_count - *item.column)) {
            return false;
        }
    }
    return true;
}

bool valid_measurements(std::span<const IntrinsicSize> measurements) noexcept {
    const LayoutConstraints unconstrained{};
    for (const auto& intrinsic : measurements) {
        MeasuredSize measured;
        if (constrain_measurement(unconstrained, intrinsic, measured) != MeasureResult::complete) {
            return false;
        }
    }
    return true;
}

double track_extent(
    std::span<const GridTrackWorkspace> workspace,
    std::size_t begin,
    std::size_t count,
    double gap
) noexcept {
    double extent = gap * static_cast<double>(count - 1);
    for (std::size_t index = begin; index < begin + count; ++index) {
        extent += workspace[index].size;
    }
    return extent;
}

void grow_tracks(
    std::span<const GridTrack> tracks,
    std::span<GridTrackWorkspace> workspace,
    std::size_t begin,
    std::size_t count,
    double amount
) noexcept {
    double total_capacity = 0.0;
    for (std::size_t index = begin; index < begin + count; ++index) {
        total_capacity +=
            std::max(0.0, static_cast<double>(tracks[index].maximum) - workspace[index].size);
    }
    if (total_capacity <= 0.0) {
        return;
    }

    const auto target = std::min(amount, total_capacity);
    double applied = 0.0;
    for (std::size_t index = begin; index < begin + count; ++index) {
        const auto capacity =
            std::max(0.0, static_cast<double>(tracks[index].maximum) - workspace[index].size);
        if (capacity <= 0.0) {
            continue;
        }

        const auto growth = std::min(capacity, target * capacity / total_capacity);
        const auto previous_size = workspace[index].size;
        workspace[index].size = static_cast<float>(std::min(
            static_cast<double>(tracks[index].maximum),
            static_cast<double>(previous_size) + growth
        ));
        applied += static_cast<double>(workspace[index].size) - previous_size;
    }

    auto remainder = target - applied;
    for (std::size_t index = begin; index < begin + count && remainder > 0.0; ++index) {
        const auto capacity =
            std::max(0.0, static_cast<double>(tracks[index].maximum) - workspace[index].size);
        const auto growth = std::min(capacity, remainder);
        const auto previous_size = workspace[index].size;
        workspace[index].size = static_cast<float>(std::min(
            static_cast<double>(tracks[index].maximum),
            static_cast<double>(previous_size) + growth
        ));
        remainder -= static_cast<double>(workspace[index].size) - previous_size;
    }
}

double grow_fraction_tracks(
    std::span<const GridTrack> tracks,
    std::span<GridTrackWorkspace> workspace,
    double amount
) noexcept {
    auto minimum_fraction = std::numeric_limits<double>::max();
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        const auto capacity = static_cast<double>(tracks[index].maximum) - workspace[index].size;
        if (tracks[index].fraction > 0.0F && capacity > 0.0) {
            minimum_fraction =
                std::min(minimum_fraction, static_cast<double>(tracks[index].fraction));
        }
    }
    if (minimum_fraction == std::numeric_limits<double>::max()) {
        return amount;
    }

    double lower = 0.0;
    double upper = amount / minimum_fraction;
    for (int iteration = 0; iteration < 24; ++iteration) {
        const auto level = (lower + upper) / 2.0;
        double distributed = 0.0;
        for (std::size_t index = 0; index < tracks.size(); ++index) {
            if (tracks[index].fraction == 0.0F) {
                continue;
            }
            const auto capacity =
                std::max(0.0, static_cast<double>(tracks[index].maximum) - workspace[index].size);
            distributed += std::min(capacity, static_cast<double>(tracks[index].fraction) * level);
        }
        if (distributed < amount) {
            lower = level;
        } else {
            upper = level;
        }
    }

    double applied = 0.0;
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        if (tracks[index].fraction == 0.0F) {
            continue;
        }
        const auto capacity =
            std::max(0.0, static_cast<double>(tracks[index].maximum) - workspace[index].size);
        const auto growth = std::min(capacity, static_cast<double>(tracks[index].fraction) * lower);
        const auto previous_size = workspace[index].size;
        workspace[index].size = static_cast<float>(std::min(
            static_cast<double>(tracks[index].maximum),
            static_cast<double>(previous_size) + growth
        ));
        applied += static_cast<double>(workspace[index].size) - previous_size;
    }

    auto remainder = std::max(0.0, amount - applied);
    for (std::size_t index = 0; index < tracks.size() && remainder > 0.0; ++index) {
        if (tracks[index].fraction == 0.0F) {
            continue;
        }
        const auto capacity =
            std::max(0.0, static_cast<double>(tracks[index].maximum) - workspace[index].size);
        const auto previous_size = workspace[index].size;
        const auto growth = std::min(capacity, remainder);
        workspace[index].size = static_cast<float>(std::min(
            static_cast<double>(tracks[index].maximum),
            static_cast<double>(previous_size) + growth
        ));
        remainder -= static_cast<double>(workspace[index].size) - previous_size;
    }

    return remainder;
}

void shrink_tracks(
    std::span<GridTrackWorkspace> workspace,
    double amount,
    bool preserve_preferred
) noexcept {
    const auto minimum_size = [preserve_preferred](const GridTrackWorkspace& track) {
        return preserve_preferred ? track.preferred : track.min_content;
    };
    double total_capacity = 0.0;
    for (const auto& track : workspace) {
        total_capacity += static_cast<double>(track.size) - minimum_size(track);
    }
    if (total_capacity <= 0.0) {
        return;
    }

    const auto target = std::min(amount, total_capacity);
    double applied = 0.0;
    for (auto& track : workspace) {
        const auto minimum = minimum_size(track);
        const auto capacity = static_cast<double>(track.size) - minimum;
        if (capacity <= 0.0) {
            continue;
        }

        const auto reduction = std::min(capacity, target * capacity / total_capacity);
        const auto previous_size = track.size;
        track.size = static_cast<float>(
            std::max(static_cast<double>(minimum), static_cast<double>(previous_size) - reduction)
        );
        applied += static_cast<double>(previous_size) - track.size;
    }

    auto remainder = target - applied;
    for (auto& track : workspace) {
        if (remainder <= 0.0) {
            break;
        }

        const auto minimum = minimum_size(track);
        const auto capacity = static_cast<double>(track.size) - minimum;
        const auto reduction = std::min(capacity, remainder);
        const auto previous_size = track.size;
        track.size = static_cast<float>(
            std::max(static_cast<double>(minimum), static_cast<double>(previous_size) - reduction)
        );
        remainder -= static_cast<double>(previous_size) - track.size;
    }
}

void size_item_span(
    std::span<const GridTrack> tracks,
    std::span<GridTrackWorkspace> workspace,
    std::size_t begin,
    std::size_t count,
    float contribution,
    double gap
) noexcept {
    const auto current = track_extent(workspace, begin, count, gap);
    if (current < contribution) {
        grow_tracks(tracks, workspace, begin, count, contribution - current);
    }
}

void size_tracks_from_minimum_content(
    std::span<const GridTrack> columns,
    std::span<const GridTrack> rows,
    std::span<const GridItem> items,
    std::span<const GridItemPlacement> placements,
    std::span<const IntrinsicSize> measurements,
    std::span<GridTrackWorkspace> column_workspace,
    std::span<GridTrackWorkspace> row_workspace,
    double gap
) noexcept {
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        const auto placement = item_placement(item, index, placements);
        size_item_span(
            columns,
            column_workspace,
            placement.column,
            item.column_span,
            measurements[index].width.min_content,
            gap
        );
        size_item_span(
            rows,
            row_workspace,
            placement.row,
            item.row_span,
            measurements[index].height.min_content,
            gap
        );
    }
}

void size_tracks_from_preferred_content(
    std::span<const GridTrack> columns,
    std::span<const GridTrack> rows,
    std::span<const GridItem> items,
    std::span<const GridItemPlacement> placements,
    std::span<const IntrinsicSize> measurements,
    std::span<GridTrackWorkspace> column_workspace,
    std::span<GridTrackWorkspace> row_workspace,
    double gap
) noexcept {
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        const auto placement = item_placement(item, index, placements);
        size_item_span(
            columns,
            column_workspace,
            placement.column,
            item.column_span,
            measurements[index].width.preferred,
            gap
        );
        size_item_span(
            rows,
            row_workspace,
            placement.row,
            item.row_span,
            measurements[index].height.preferred,
            gap
        );
    }
}

void size_tracks_from_maximum_content(
    std::span<const GridTrack> columns,
    std::span<const GridTrack> rows,
    std::span<const GridItem> items,
    std::span<const GridItemPlacement> placements,
    std::span<const IntrinsicSize> measurements,
    std::span<GridTrackWorkspace> column_workspace,
    std::span<GridTrackWorkspace> row_workspace,
    double gap
) noexcept {
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        const auto placement = item_placement(item, index, placements);
        size_item_span(
            columns,
            column_workspace,
            placement.column,
            item.column_span,
            measurements[index].width.max_content,
            gap
        );
        size_item_span(
            rows,
            row_workspace,
            placement.row,
            item.row_span,
            measurements[index].height.max_content,
            gap
        );
    }
}

double total_track_extent(std::span<const GridTrackWorkspace> tracks, double gap) noexcept {
    if (tracks.empty()) {
        return 0.0;
    }

    return track_extent(tracks, 0, tracks.size(), gap);
}

double preferred_track_extent(std::span<const GridTrackWorkspace> tracks, double gap) noexcept {
    if (tracks.empty()) {
        return 0.0;
    }

    double extent = gap * static_cast<double>(tracks.size() - 1);
    for (const auto& track : tracks) {
        extent += track.preferred;
    }
    return extent;
}

bool resolve_axis(
    std::span<const GridTrack> tracks,
    std::span<GridTrackWorkspace> workspace,
    double available,
    double gap
) noexcept {
    const auto preferred_extent = preferred_track_extent(workspace, gap);
    const auto maximum_content_extent = total_track_extent(workspace, gap);
    if (preferred_extent > available) {
        for (auto& track : workspace) {
            track.size = track.preferred;
        }
        shrink_tracks(workspace, preferred_extent - available, false);
    } else if (maximum_content_extent > available) {
        shrink_tracks(workspace, maximum_content_extent - available, true);
    } else if (maximum_content_extent < available) {
        const auto free_space = available - maximum_content_extent;
        const bool has_fraction_tracks =
            std::any_of(tracks.begin(), tracks.end(), [](const GridTrack& track) {
                return track.fraction > 0.0F;
            });
        if (has_fraction_tracks) {
            const auto remainder = grow_fraction_tracks(tracks, workspace, free_space);
            if (remainder > 0.0) {
                grow_tracks(tracks, workspace, 0, tracks.size(), remainder);
            }
        } else {
            grow_tracks(tracks, workspace, 0, tracks.size(), free_space);
        }
        const auto grown_extent = total_track_extent(workspace, gap);
        if (grown_extent > available) {
            shrink_tracks(workspace, grown_extent - available, true);
        }
    }

    double position = 0.0;
    const auto maximum_coordinate = static_cast<double>(std::numeric_limits<float>::max());
    for (auto& track : workspace) {
        if (position > maximum_coordinate) {
            return false;
        }
        track.position = static_cast<float>(position);
        position += static_cast<double>(track.size) + gap;
    }
    return true;
}

double cell_extent(
    std::span<const GridTrackWorkspace> tracks,
    std::size_t begin,
    std::size_t count,
    double gap
) noexcept {
    return track_extent(tracks, begin, count, gap);
}

}

GridLayoutResult layout_grid(
    const GridContainer& container,
    std::span<const GridTrack> columns,
    std::span<const GridTrack> rows,
    std::span<const GridItem> items,
    std::span<const IntrinsicSize> intrinsic_sizes,
    GridLayoutWorkspace workspace,
    std::span<LayoutRect> output
) noexcept {
    if (!valid_container(container)) {
        return GridLayoutResult::invalid_input;
    }
    if (!valid_tracks(columns) || !valid_tracks(rows)) {
        return GridLayoutResult::invalid_input;
    }
    if (!valid_items(items, rows.size(), columns.size())) {
        return GridLayoutResult::invalid_input;
    }
    if (intrinsic_sizes.size() < items.size()) {
        return GridLayoutResult::insufficient_measurements;
    }
    if (!valid_measurements(intrinsic_sizes.first(items.size()))) {
        return GridLayoutResult::invalid_input;
    }
    if (output.size() < items.size()) {
        return GridLayoutResult::insufficient_output;
    }
    auto column_workspace = workspace.columns;
    auto row_workspace = workspace.rows;
    if (column_workspace.size() < columns.size() || row_workspace.size() < rows.size()) {
        return GridLayoutResult::insufficient_workspace;
    }

    const auto placement_result = resolve_placements(
        items,
        rows.size(),
        columns.size(),
        workspace.placements,
        workspace.occupancy
    );
    if (placement_result != GridLayoutResult::complete) {
        return placement_result;
    }
    const auto placements = has_automatic_placement(items)
        ? std::span<const GridItemPlacement>(workspace.placements.data(), items.size())
        : std::span<const GridItemPlacement>{};

    for (std::size_t index = 0; index < columns.size(); ++index) {
        column_workspace[index] = {
            .min_content = columns[index].minimum,
            .size = columns[index].minimum,
        };
    }
    for (std::size_t index = 0; index < rows.size(); ++index) {
        row_workspace[index] = {
            .min_content = rows[index].minimum,
            .size = rows[index].minimum,
        };
    }

    size_tracks_from_minimum_content(
        columns,
        rows,
        items,
        placements,
        intrinsic_sizes,
        column_workspace,
        row_workspace,
        container.gap
    );
    for (std::size_t index = 0; index < columns.size(); ++index) {
        column_workspace[index].min_content = column_workspace[index].size;
    }
    for (std::size_t index = 0; index < rows.size(); ++index) {
        row_workspace[index].min_content = row_workspace[index].size;
    }

    size_tracks_from_preferred_content(
        columns,
        rows,
        items,
        placements,
        intrinsic_sizes,
        column_workspace,
        row_workspace,
        container.gap
    );
    for (std::size_t index = 0; index < columns.size(); ++index) {
        column_workspace[index].preferred = column_workspace[index].size;
    }
    for (std::size_t index = 0; index < rows.size(); ++index) {
        row_workspace[index].preferred = row_workspace[index].size;
    }

    size_tracks_from_maximum_content(
        columns,
        rows,
        items,
        placements,
        intrinsic_sizes,
        column_workspace,
        row_workspace,
        container.gap
    );

    const auto available_width = std::max(
        0.0,
        static_cast<double>(container.width) - container.padding.left - container.padding.right
    );
    const auto available_height = std::max(
        0.0,
        static_cast<double>(container.height) - container.padding.top - container.padding.bottom
    );
    if (!resolve_axis(columns, column_workspace, available_width, container.gap)) {
        return GridLayoutResult::invalid_input;
    }
    if (!resolve_axis(rows, row_workspace, available_height, container.gap)) {
        return GridLayoutResult::invalid_input;
    }

    bool overflow = total_track_extent(column_workspace, container.gap) > available_width ||
        total_track_extent(row_workspace, container.gap) > available_height;
    const auto maximum_coordinate = static_cast<double>(std::numeric_limits<float>::max());
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        const auto placement = item_placement(item, index, placements);
        const auto width =
            cell_extent(column_workspace, placement.column, item.column_span, container.gap);
        const auto height = cell_extent(row_workspace, placement.row, item.row_span, container.gap);
        const auto x = static_cast<double>(container.padding.left) +
            column_workspace[placement.column].position;
        const auto y =
            static_cast<double>(container.padding.top) + row_workspace[placement.row].position;
        if (width > maximum_coordinate || height > maximum_coordinate || x > maximum_coordinate ||
            y > maximum_coordinate) {
            return GridLayoutResult::invalid_input;
        }

        overflow = overflow || intrinsic_sizes[index].width.min_content > width ||
            intrinsic_sizes[index].height.min_content > height;
    }

    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        const auto placement = item_placement(item, index, placements);
        output[index] = {
            .x = container.padding.left + column_workspace[placement.column].position,
            .y = container.padding.top + row_workspace[placement.row].position,
            .width = static_cast<float>(
                cell_extent(column_workspace, placement.column, item.column_span, container.gap)
            ),
            .height = static_cast<float>(
                cell_extent(row_workspace, placement.row, item.row_span, container.gap)
            ),
        };
    }

    return overflow ? GridLayoutResult::overflow : GridLayoutResult::complete;
}

}
