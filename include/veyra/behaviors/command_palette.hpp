#ifndef VEYRA_BEHAVIORS_COMMAND_PALETTE_HPP
#define VEYRA_BEHAVIORS_COMMAND_PALETTE_HPP

#include "veyra/core/semantics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class PaletteResult : std::uint8_t {
    ignored,
    handled,
    changed,
    overflow,
};

enum class PaletteMove : std::uint8_t {
    next,
    prev,
    first,
    last,
};

struct PaletteItem {
    std::uint64_t id{};
    std::uint64_t group_id{};
    bool disabled{};
};

struct PaletteActive {
    bool has{};
    std::uint64_t id{};
};

struct PaletteState {
    bool expanded{};
    bool disabled{};
    bool loop{};
    std::size_t items{};
    std::size_t matches{};
    std::size_t total{};
    bool overflow{};
    bool has_active{};
    std::uint64_t active{};
    std::uint64_t generation{};
};

class CommandPalette {
  public:
    static constexpr std::size_t kItemCapacity = 1024;
    static constexpr std::size_t kResultCapacity = 256;

    CommandPalette() noexcept;

    PaletteState state() const noexcept;
    bool expanded() const noexcept;
    bool disabled() const noexcept;
    bool loops() const noexcept;
    std::size_t item_count() const noexcept;
    std::size_t match_count() const noexcept;
    std::size_t total_matches() const noexcept;
    bool overflowed() const noexcept;
    bool has_results() const noexcept;
    PaletteActive active() const noexcept;
    std::uint64_t generation() const noexcept;
    std::span<const char> query() const noexcept;
    bool contains(std::uint64_t id) const noexcept;
    bool is_enabled(std::uint64_t id) const noexcept;
    std::size_t copy_matches(std::span<std::uint64_t> output) const noexcept;
    std::size_t copy_groups(std::span<std::uint64_t> output) const noexcept;

    bool set_loop(bool loop) noexcept;
    bool set_enabled(bool enabled) noexcept;
    bool set_expanded(bool expanded) noexcept;
    PaletteResult set_items(std::span<const PaletteItem> items) noexcept;
    PaletteResult set_query(std::span<const char> query) noexcept;
    std::uint64_t invalidate() noexcept;

    static PaletteResult filter(
        std::span<const char> query,
        std::span<const PaletteItem> items,
        std::span<const std::string_view> titles,
        std::span<const std::string_view> keywords,
        std::span<std::uint64_t> output_ids,
        std::size_t& output_count
    ) noexcept;

    PaletteResult apply_filter(
        std::span<const std::string_view> titles,
        std::span<const std::string_view> keywords
    ) noexcept;

    PaletteResult move(PaletteMove direction) noexcept;
    PaletteResult move_next() noexcept;
    PaletteResult move_prev() noexcept;
    PaletteResult move_first() noexcept;
    PaletteResult move_last() noexcept;
    std::uint64_t activate() const noexcept;

    bool
    write_box_semantics(std::string_view accessible_name, SemanticNodeData& output) const noexcept;
    bool write_item_semantics(
        std::uint64_t id,
        std::string_view accessible_name,
        SemanticNodeData& output
    ) const noexcept;

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);
    static constexpr int kNoRank = -1;

    static bool same_query(std::span<const char> left, std::span<const char> right) noexcept;
    static bool starts_with(std::string_view text, std::span<const char> query) noexcept;
    static bool word_prefix_match(std::string_view text, std::span<const char> query) noexcept;
    static bool subsequence_match(std::string_view text, std::span<const char> query) noexcept;
    static int rank_text(std::string_view text, std::span<const char> query) noexcept;
    static int rank_item(
        std::string_view title,
        std::string_view keywords,
        std::span<const char> query
    ) noexcept;
    static std::size_t collect_matches(
        std::span<const char> query,
        std::span<const PaletteItem> items,
        std::span<const std::string_view> titles,
        std::span<const std::string_view> keywords,
        std::span<std::uint64_t> ids,
        std::span<bool> enabled = {},
        std::span<std::uint64_t> groups = {}
    ) noexcept;

    std::size_t find_item(std::uint64_t id) const noexcept;
    std::size_t find_match(std::uint64_t id) const noexcept;
    std::size_t enabled_total() const noexcept;
    std::size_t scan_enabled(std::size_t from, bool forward) const noexcept;
    bool same_window(
        const std::uint64_t* ids,
        const bool* enabled,
        std::size_t size,
        bool has_active,
        std::uint64_t active,
        bool overflow
    ) const noexcept;

    std::array<PaletteItem, kItemCapacity> items_{};
    std::size_t item_count_{};
    std::array<std::uint64_t, kResultCapacity> filtered_{};
    std::array<bool, kResultCapacity> filtered_enabled_{};
    std::array<std::uint64_t, kResultCapacity> filtered_groups_{};
    std::size_t filtered_count_{};
    std::size_t total_matches_{};
    bool overflow_{};
    std::span<const char> query_{};
    std::uint64_t generation_{};
    bool loop_{true};
    bool enabled_{true};
    bool expanded_{};
    bool has_active_{};
    std::uint64_t active_{};
};

}

#endif
