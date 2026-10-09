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

struct DataGridColumn {
    std::uint64_t id{};
    float width{};
    float min_width{};
    float max_width{};
    bool sortable{};
    bool hidden{};
};

struct SortSpec {
    bool has_sort{};
    std::uint64_t column{};
    SortDirection direction{SortDirection::ascending};
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

    std::size_t column_count() const noexcept;
    std::size_t visible_count() const noexcept;
    bool contains_column(std::uint64_t id) const noexcept;
    bool column_hidden(std::uint64_t id) const noexcept;
    float column_width(std::uint64_t id) const noexcept;
    std::size_t copy_columns(std::span<DataGridColumn> output) const noexcept;
    std::size_t copy_visible_columns(std::span<std::uint64_t> output) const noexcept;

    DataGridResult set_columns(std::span<const DataGridColumn> columns) noexcept;
    DataGridResult resize_column(std::uint64_t id, float width) noexcept;
    DataGridResult move_column(std::uint64_t id, std::uint64_t target, ColumnEdge edge) noexcept;
    DataGridResult set_hidden(std::uint64_t id, bool hidden) noexcept;

    SortSpec sort_spec() const noexcept;
    bool has_sort() const noexcept;
    DataGridResult sort_by(std::uint64_t column, SortDirection direction) noexcept;
    DataGridResult clear_sort() noexcept;

    bool write_header_semantics(
        std::uint64_t column,
        std::string_view accessible_name,
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

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    std::size_t find(std::uint64_t id) const noexcept;
    std::size_t collect_visible(std::span<std::uint64_t> output) const noexcept;
    static bool valid_spec(const DataGridColumn& column) noexcept;
    static float clamp_width(float width, float low, float high) noexcept;
    void snap_cursor_to_visible() noexcept;

    std::array<DataGridColumn, kCapacity> columns_{};
    std::size_t count_{};
    bool has_sort_{};
    std::uint64_t sort_column_{};
    SortDirection sort_direction_{SortDirection::ascending};
    bool has_cursor_{};
    std::uint64_t cursor_row_{};
    std::uint64_t cursor_column_{};
};

}

#endif
