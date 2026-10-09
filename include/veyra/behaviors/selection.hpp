#ifndef VEYRA_BEHAVIORS_SELECTION_HPP
#define VEYRA_BEHAVIORS_SELECTION_HPP

#include "veyra/core/semantics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class SelectionMode : std::uint8_t {
    single,
    multiple,
};

enum class SelectionResult : std::uint8_t {
    ignored,
    handled,
    changed,
    overflow,
};

struct SelectionConfig {
    SelectionMode mode{SelectionMode::single};
    bool allow_empty{true};
};

struct SelectionState {
    SelectionMode mode{SelectionMode::single};
    bool allow_empty{true};
    std::size_t count{};
    bool disabled{};
};

class SelectionController {
  public:
    static constexpr std::size_t kCapacity = 256;

    SelectionState state() const noexcept;
    SelectionMode mode() const noexcept;
    bool allows_empty() const noexcept;
    bool disabled() const noexcept;

    bool configure(const SelectionConfig& config) noexcept;
    bool set_disabled(bool disabled) noexcept;

    std::size_t count() const noexcept;
    bool empty() const noexcept;
    bool contains(std::uint64_t id) const noexcept;
    bool has_anchor() const noexcept;
    std::uint64_t anchor() const noexcept;
    bool has_active() const noexcept;
    std::uint64_t active() const noexcept;
    std::size_t copy_selected(std::span<std::uint64_t> output) const noexcept;

    bool write_item_semantics(
        std::uint64_t id,
        std::string_view accessible_name,
        SemanticNodeData& output
    ) const noexcept;

    SelectionResult select(std::uint64_t id) noexcept;
    SelectionResult deselect(std::uint64_t id) noexcept;
    SelectionResult toggle_item(std::uint64_t id) noexcept;
    SelectionResult clear() noexcept;
    SelectionResult extend_range(std::span<const std::uint64_t> order, std::uint64_t to) noexcept;
    SelectionResult select_all(std::span<const std::uint64_t> ids) noexcept;

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    std::size_t find(std::uint64_t id) const noexcept;
    bool same_set(const std::uint64_t* ids, std::size_t size) const noexcept;
    void remove_at(std::size_t index) noexcept;

    std::array<std::uint64_t, kCapacity> ids_{};
    std::size_t count_{};
    SelectionMode mode_{SelectionMode::single};
    bool allow_empty_{true};
    bool disabled_{};
    bool has_anchor_{};
    std::uint64_t anchor_{};
    bool has_active_{};
    std::uint64_t active_{};
};

enum class RadioMove : std::uint8_t {
    next,
    prev,
    first,
    last,
};

class RadioGroup {
  public:
    RadioGroup() noexcept;

    bool allows_empty() const noexcept;
    bool disabled() const noexcept;
    bool has_selection() const noexcept;
    std::uint64_t selected() const noexcept;

    bool configure(bool allow_empty) noexcept;
    bool set_disabled(bool disabled) noexcept;

    bool contains(std::uint64_t id) const noexcept;
    SelectionResult select(std::uint64_t id, std::span<const std::uint64_t> members) noexcept;
    SelectionResult clear() noexcept;
    SelectionResult move(RadioMove direction, std::span<const std::uint64_t> members) noexcept;

    bool write_semantics(
        std::uint64_t id,
        std::string_view accessible_name,
        SemanticNodeData& output
    ) const noexcept;

  private:
    SelectionController controller_{};
};

}

#endif
