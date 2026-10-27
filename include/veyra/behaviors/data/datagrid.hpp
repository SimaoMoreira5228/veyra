#ifndef VEYRA_BEHAVIORS_DATAGRID_HPP
#define VEYRA_BEHAVIORS_DATAGRID_HPP

#include "veyra/core/semantics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class DataGridResult : std::uint8_t {
    ignored,
    handled,
    changed,
    moved,
    edit,
    overflow,
};

enum class SortDirection : std::uint8_t {
    ascending,
    descending,
};

enum class DataGridMove : std::uint8_t {
    up,
    down,
    left,
    right,
    home,
    end,
    page_up,
    page_down,
    next,
    prev,
};

enum class ColumnEdge : std::uint8_t {
    before,
    after,
};

enum class DataGridPin : std::uint8_t {
    none,
    left,
    right,
};

enum class DataGridValueKind : std::uint8_t {
    text,
    number,
    date,
    custom,
};

enum class DataGridDensity : std::uint8_t {
    compact,
    standard,
    comfortable,
};

struct DataGridColumn {
    std::uint64_t id{};
    float width{};
    float min_width{};
    float max_width{};
    bool sortable{};
    bool hidden{};
    DataGridPin pinned{DataGridPin::none};
    bool locked_position{};
    DataGridValueKind value_kind{DataGridValueKind::text};
};

struct SortSpec {
    std::uint64_t column{};
    SortDirection direction{SortDirection::ascending};
};

struct ColumnCustomization {
    std::uint64_t id{};
    float width{};
    bool hidden{};
    DataGridPin pinned{DataGridPin::none};
    std::uint32_t position{};
};

struct CellCursor {
    bool has_cursor{};
    std::uint64_t row{};
    std::uint64_t column{};
};

struct CellMove {
    DataGridResult result{DataGridResult::ignored};
    std::uint64_t row{};
    std::uint64_t column{};
};

struct CellEdit {
    DataGridResult result{DataGridResult::ignored};
    std::uint64_t row{};
    std::uint64_t column{};
};

class DataGridController {
  public:
    static constexpr std::size_t kCapacity = 64;
    static constexpr std::size_t kSortCapacity = 8;

    std::size_t column_count() const noexcept;
    std::size_t visible_count() const noexcept;
    bool contains_column(std::uint64_t id) const noexcept;
    bool column_hidden(std::uint64_t id) const noexcept;
    float column_width(std::uint64_t id) const noexcept;
    DataGridPin column_pin(std::uint64_t id) const noexcept;
    std::size_t copy_columns(std::span<DataGridColumn> output) const noexcept;
    std::size_t copy_visible_columns(std::span<std::uint64_t> output) const noexcept;

    DataGridResult set_columns(std::span<const DataGridColumn> columns) noexcept;
    DataGridResult resize_column(std::uint64_t id, float width) noexcept;
    DataGridResult move_column(std::uint64_t id, std::uint64_t target, ColumnEdge edge) noexcept;
    DataGridResult set_hidden(std::uint64_t id, bool hidden) noexcept;
    DataGridResult set_pinned(std::uint64_t id, DataGridPin pin) noexcept;

    std::size_t copy_customization(std::span<ColumnCustomization> output) const noexcept;
    DataGridResult apply_customization(std::span<const ColumnCustomization> custom) noexcept;

    bool has_sort() const noexcept;
    std::size_t sort_count() const noexcept;
    std::size_t sort_specs(std::span<SortSpec> output) const noexcept;
    DataGridResult
    sort_by(std::uint64_t column, SortDirection direction, bool accumulate = false) noexcept;
    DataGridResult clear_sort() noexcept;

    DataGridDensity density() const noexcept;
    DataGridResult set_density(DataGridDensity density) noexcept;
    float density_row_height() const noexcept;

    bool write_header_semantics(
        std::uint64_t column,
        std::string_view accessible_name,
        SemanticNodeData& output
    ) const noexcept;

    bool write_grid_semantics(
        std::string_view accessible_name,
        bool focused,
        SemanticNodeData& output
    ) const noexcept;

    bool write_row_semantics(
        std::uint64_t row,
        std::size_t row_index,
        bool selected,
        SemanticNodeData& output
    ) const noexcept;

    bool write_cell_semantics(
        std::uint64_t row,
        std::uint64_t column,
        std::size_t row_index,
        std::size_t column_index,
        std::string_view accessible_name,
        bool selected,
        bool focused,
        SemanticNodeData& output
    ) const noexcept;

    CellCursor cursor() const noexcept;
    bool has_cursor() const noexcept;
    DataGridResult set_cursor(
        std::uint64_t row,
        std::uint64_t column,
        std::span<const std::uint64_t> rows
    ) noexcept;
    DataGridResult clear_cursor() noexcept;
    CellMove move_cursor(
        DataGridMove move,
        std::span<const std::uint64_t> rows,
        std::size_t page_step
    ) noexcept;
    CellEdit begin_edit(std::span<const std::uint64_t> rows) noexcept;
    CellEdit commit_edit() noexcept;
    DataGridResult cancel_edit() noexcept;
    bool edit_pending() const noexcept;
    CellEdit pending_edit() const noexcept;
    bool edit_row_mode() const noexcept;
    DataGridResult edit_row_mode(bool row_mode) noexcept;

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    std::size_t find(std::uint64_t id) const noexcept;
    std::size_t collect_visible(std::span<std::uint64_t> output) const noexcept;
    static bool valid_spec(const DataGridColumn& column) noexcept;
    static float clamp_width(float width, float low, float high) noexcept;
    void snap_cursor_to_visible() noexcept;
    void drop_edit_for_column(std::uint64_t id) noexcept;
    void cancel_pending() noexcept;
    bool same_spec(const DataGridColumn& before, const DataGridColumn& after) const noexcept;
    std::size_t prune_sorts() noexcept;

    std::array<DataGridColumn, kCapacity> columns_{};
    std::size_t count_{};
    std::array<SortSpec, kSortCapacity> sorts_{};
    std::size_t sort_count_{};
    DataGridDensity density_{DataGridDensity::standard};
    bool has_cursor_{};
    std::uint64_t cursor_row_{};
    std::uint64_t cursor_column_{};
    bool edit_pending_{};
    std::uint64_t edit_row_{};
    std::uint64_t edit_column_{};
    bool edit_row_mode_{};
};

}
#endif
