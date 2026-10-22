#ifndef VEYRA_LAYOUT_DOCK_HPP
#define VEYRA_LAYOUT_DOCK_HPP

#include "veyra/behaviors/tabs.hpp"
#include "veyra/layout/geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

inline constexpr std::size_t kDockCapacity = 64;
inline constexpr std::size_t kDockMaxTabs = 16;
inline constexpr float kDockStripHeight = 24.0F;

enum class DockResult : std::uint8_t {
    ignored,
    handled,
    changed,
    overflow,
};

enum class DockSplit : std::uint8_t {
    horizontal,
    vertical,
};

enum class DockLayoutResult : std::uint8_t {
    complete,
    overflow,
    invalid_input,
    insufficient_output,
};

struct DockLeafRect {
    std::uint64_t leaf{};
    LayoutRect strip{};
    LayoutRect content{};
    bool has_strip{};
    std::uint64_t active_panel{};
};

struct DockRecord {
    std::uint64_t id{};
    std::uint64_t parent{};
    bool is_split{};
    DockSplit axis{DockSplit::horizontal};
    float ratio{0.5F};
    float min_first{};
    float min_second{};
    std::uint64_t first{};
    std::uint64_t second{};
    std::array<std::uint64_t, kDockMaxTabs> tabs{};
    std::uint32_t tab_count{};
    std::uint64_t active{};
};

class DockController {
  public:
    static constexpr std::size_t kCapacity = kDockCapacity;
    static constexpr std::size_t kMaxTabs = kDockMaxTabs;

    DockController() noexcept;

    std::uint64_t root() const noexcept;
    std::size_t node_count() const noexcept;
    bool contains(std::uint64_t id) const noexcept;
    bool is_split(std::uint64_t id) const noexcept;
    std::size_t tab_count(std::uint64_t leaf) const noexcept;
    std::uint64_t active_panel(std::uint64_t leaf) const noexcept;
    std::size_t copy_leaves(std::span<std::uint64_t> output) const noexcept;

    DockResult
    split(std::uint64_t leaf, DockSplit direction, float ratio, std::uint64_t& new_leaf) noexcept;
    DockResult tab_add(std::uint64_t leaf, std::uint64_t panel) noexcept;
    DockResult tab_activate(std::uint64_t leaf, std::uint64_t panel) noexcept;
    DockResult tab_close(std::uint64_t leaf, std::uint64_t panel) noexcept;
    DockResult set_ratio(std::uint64_t node, float ratio) noexcept;
    DockResult set_minimums(std::uint64_t node, float min_first, float min_second) noexcept;
    DockResult set_gap(float gap) noexcept;
    DockResult resize(float width, float height) noexcept;

    DockLayoutResult layout(LayoutRect bounds, std::span<DockLeafRect> output) const noexcept;

    std::size_t export_layout(std::span<DockRecord> output) const noexcept;
    DockResult import_layout(std::span<const DockRecord> records) noexcept;

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    struct Node {
        bool used{};
        std::uint64_t id{};
        std::uint64_t parent{};
        bool is_split{};
        DockSplit axis{DockSplit::horizontal};
        float ratio{0.5F};
        float min_first{};
        float min_second{};
        std::uint64_t first{};
        std::uint64_t second{};
        std::array<std::uint64_t, kDockMaxTabs> tabs{};
        std::size_t tab_count{};
    };

    std::size_t index_of(std::uint64_t id) const noexcept;
    std::size_t record_at(
        std::span<const DockRecord> records,
        std::span<const std::size_t> order,
        std::uint64_t id
    ) const noexcept;
    std::uint64_t build_record(
        std::span<const DockRecord> records,
        std::span<const std::size_t> order,
        std::uint64_t id,
        std::uint64_t parent,
        std::span<std::uint64_t> path,
        std::size_t depth,
        std::span<Node> nodes,
        std::span<TabsController> strips,
        std::size_t& placed,
        std::uint64_t& next_id
    ) noexcept;
    void collect_leaves(
        std::uint64_t id,
        std::span<std::uint64_t> output,
        std::size_t& next
    ) const noexcept;
    DockLayoutResult place(
        std::uint64_t id,
        const LayoutRect& rect,
        std::span<DockLeafRect> output,
        std::size_t& next,
        bool& overflow
    ) const noexcept;
    static float clamp_unit(float value) noexcept;

    std::array<Node, kCapacity> nodes_{};
    std::array<TabsController, kCapacity> strips_{};
    std::uint64_t root_{};
    std::size_t used_{};
    std::uint64_t next_id_{1};
    float gap_{};
    float bound_width_{};
    float bound_height_{};
};

}

#endif
