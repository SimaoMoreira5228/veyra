#ifndef VEYRA_BEHAVIORS_MENU_HPP
#define VEYRA_BEHAVIORS_MENU_HPP

#include "veyra/core/semantics/semantics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class MenuItemKind : std::uint8_t {
    command,
    checkable,
    radio,
    separator,
    submenu,
};

enum class MenuMove : std::uint8_t {
    next,
    prev,
    first,
    last,
};

enum class MenuResult : std::uint8_t {
    ignored,
    handled,
    changed,
    opened,
    overflow,
    closed,
    dismissed,
    command,
    toggle,
    radio,
};

struct MenuItem {
    std::uint64_t id{};
    MenuItemKind kind{MenuItemKind::command};
    bool disabled{};
    bool has_children{};
};

struct MenuAction {
    MenuResult result{MenuResult::ignored};
    std::uint64_t id{};
};

struct MenuClose {
    MenuResult result{MenuResult::ignored};
    std::uint64_t closed{};
};

struct MenuReset {
    std::size_t closed{};
};

struct MenuActive {
    bool has{};
    std::uint64_t id{};
};

class MenuController {
  public:
    static constexpr std::size_t kCapacity = 8;

    MenuController() noexcept;

    bool empty() const noexcept;
    std::size_t depth() const noexcept;
    std::uint64_t menu_at(std::size_t index) const noexcept;
    MenuActive active() const noexcept;
    MenuActive active_at(std::size_t index) const noexcept;

    MenuResult open_root(std::uint64_t menu, std::span<const MenuItem> items) noexcept;
    MenuResult open_root_at(
        std::uint64_t menu,
        std::span<const MenuItem> items,
        std::uint64_t anchor
    ) noexcept;
    MenuResult open_submenu(
        std::uint64_t id,
        std::span<const MenuItem> current,
        std::span<const MenuItem> children
    ) noexcept;

    MenuClose close_level() noexcept;
    MenuReset close_all() noexcept;

    MenuResult move(MenuMove direction, std::span<const MenuItem> current) noexcept;
    MenuResult hover(std::uint64_t id, std::span<const MenuItem> current) noexcept;

    MenuAction
    press_right(std::span<const MenuItem> current, std::span<const MenuItem> children) noexcept;
    MenuAction
    activate(std::span<const MenuItem> current, std::span<const MenuItem> children) noexcept;
    MenuAction outside_press() const noexcept;

    bool
    write_menu_semantics(std::string_view accessible_name, SemanticNodeData& output) const noexcept;
    bool write_item_semantics(
        const MenuItem& item,
        std::string_view accessible_name,
        bool checked,
        bool expanded,
        SemanticNodeData& output
    ) const noexcept;

  private:
    struct MenuLevel {
        std::uint64_t menu{};
        bool has_active{};
        std::uint64_t active{};
    };

    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    static bool is_selectable(const MenuItem& item) noexcept;
    static std::size_t index_of(std::span<const MenuItem> items, std::uint64_t id) noexcept;
    static const MenuItem* find(std::span<const MenuItem> items, std::uint64_t id) noexcept;

    static std::size_t first_selectable(std::span<const MenuItem> items) noexcept;
    static std::size_t last_selectable(std::span<const MenuItem> items) noexcept;
    static std::size_t
    step_selectable(std::span<const MenuItem> items, std::size_t from, bool forward) noexcept;

    MenuResult drill_locked(const MenuItem& item, std::span<const MenuItem> children) noexcept;

    std::array<MenuLevel, kCapacity> levels_{};
    std::size_t depth_{};
};

}

namespace veyra::behaviors {
using ::veyra::MenuAction;
using ::veyra::MenuActive;
using ::veyra::MenuClose;
using ::veyra::MenuController;
using ::veyra::MenuItem;
using ::veyra::MenuItemKind;
using ::veyra::MenuMove;
using ::veyra::MenuReset;
using ::veyra::MenuResult;
}

namespace veyra::behaviors::navigation {
using ::veyra::MenuAction;
using ::veyra::MenuActive;
using ::veyra::MenuClose;
using ::veyra::MenuController;
using ::veyra::MenuItem;
using ::veyra::MenuItemKind;
using ::veyra::MenuMove;
using ::veyra::MenuReset;
using ::veyra::MenuResult;
}

#endif
