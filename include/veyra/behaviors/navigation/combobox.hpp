#ifndef VEYRA_BEHAVIORS_COMBOBOX_HPP
#define VEYRA_BEHAVIORS_COMBOBOX_HPP

#include "veyra/core/semantics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class ComboMode : std::uint8_t {
    strict,
    free,
};

enum class ComboMove : std::uint8_t {
    next,
    prev,
    first,
    last,
};

enum class ComboResult : std::uint8_t {
    ignored,
    handled,
    changed,
    opened,
    overflow,
    closed,
    dismissed,
    commit,
    commit_text,
};

struct ComboOption {
    std::uint64_t id{};
    std::string_view label{};
    bool disabled{};
};

struct ComboCommit {
    ComboResult result{ComboResult::ignored};
    std::uint64_t id{};
    bool free_text{};
};

struct ComboActive {
    bool has{};
    std::uint64_t id{};
};

struct ComboState {
    ComboMode mode{ComboMode::strict};
    bool open{};
    bool disabled{};
    std::size_t matches{};
    bool has_active{};
    std::uint64_t active{};
    bool has_value{};
    std::uint64_t value{};
};

class ComboBoxController {
  public:
    static constexpr std::size_t kCapacity = 256;

    ComboBoxController() noexcept;

    ComboState state() const noexcept;
    ComboMode mode() const noexcept;
    bool is_open() const noexcept;
    bool disabled() const noexcept;
    bool has_value() const noexcept;
    std::uint64_t value() const noexcept;
    ComboActive active() const noexcept;
    std::size_t match_count() const noexcept;
    bool contains(std::uint64_t id) const noexcept;
    bool is_option_enabled(std::uint64_t id) const noexcept;
    std::size_t copy_matches(std::span<std::uint64_t> output) const noexcept;

    bool configure(ComboMode mode) noexcept;
    bool set_disabled(bool disabled) noexcept;
    bool set_value(std::uint64_t id) noexcept;
    bool clear_value() noexcept;

    ComboResult open(std::span<const ComboOption> options, std::string_view query = {}) noexcept;
    ComboResult close() noexcept;
    ComboResult apply_filter(std::string_view query, std::span<const ComboOption> options) noexcept;
    ComboResult clear_filter() noexcept;
    ComboResult move_active(ComboMove direction) noexcept;
    ComboCommit commit_active() noexcept;
    ComboCommit commit_id(std::uint64_t id, std::span<const ComboOption> options) noexcept;
    ComboCommit commit_text(std::string_view query) noexcept;
    ComboResult dismiss() noexcept;

    bool
    write_box_semantics(std::string_view accessible_name, SemanticNodeData& output) const noexcept;
    bool
    write_list_semantics(std::string_view accessible_name, SemanticNodeData& output) const noexcept;
    bool write_item_semantics(
        std::uint64_t id,
        std::string_view accessible_name,
        SemanticNodeData& output
    ) const noexcept;

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    static bool matches_query(std::string_view label, std::string_view query) noexcept;

    std::size_t find_match(std::uint64_t id) const noexcept;
    std::size_t scan_enabled(std::size_t from, bool forward) const noexcept;
    std::size_t enabled_total() const noexcept;
    bool same_matches(const std::uint64_t* ids, std::size_t size) const noexcept;
    ComboResult collect(std::string_view query, std::span<const ComboOption> options) noexcept;
    ComboCommit finish_commit(std::uint64_t id, bool free_text) noexcept;

    std::array<std::uint64_t, kCapacity> matches_{};
    std::array<bool, kCapacity> match_enabled_{};
    std::size_t match_count_{};
    ComboMode mode_{ComboMode::strict};
    bool open_{};
    bool disabled_{};
    bool has_active_{};
    std::uint64_t active_{};
    bool has_value_{};
    std::uint64_t value_{};
};

}
#endif
