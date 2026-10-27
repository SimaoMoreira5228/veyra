#ifndef VEYRA_BEHAVIORS_MENUBAR_HPP
#define VEYRA_BEHAVIORS_MENUBAR_HPP

#include "veyra/core/semantics/semantics.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class MenubarMove : std::uint8_t {
    next,
    prev,
    first,
    last,
};

enum class MenubarResult : std::uint8_t {
    ignored,
    handled,
    changed,
    opened,
    switched,
    drill,
    back_to_strip,
    exited,
};

struct MenubarTitle {
    std::uint64_t id{};
    bool disabled{};
};

struct MenubarAction {
    MenubarResult result{MenubarResult::ignored};
    std::uint64_t id{};
};

struct MenubarSwitch {
    MenubarResult result{MenubarResult::ignored};
    std::uint64_t closed{};
    std::uint64_t opened{};
};

struct MenubarFocus {
    bool in_strip{};
    bool has_focus{};
    std::uint64_t focus{};
    bool has_open{};
    std::uint64_t open{};
};

class MenubarController {
  public:
    MenubarController() noexcept;

    MenubarFocus state() const noexcept;
    bool in_strip() const noexcept;
    bool has_focus() const noexcept;
    std::uint64_t focus() const noexcept;
    bool has_open() const noexcept;
    std::uint64_t open() const noexcept;

    MenubarAction enter_strip(std::span<const MenubarTitle> titles) noexcept;
    MenubarAction drill() const noexcept;
    MenubarAction escape() noexcept;
    MenubarAction dismiss_menu() noexcept;

    MenubarSwitch move(MenubarMove direction, std::span<const MenubarTitle> titles) noexcept;
    MenubarSwitch open_current(std::span<const MenubarTitle> titles) noexcept;
    MenubarSwitch click(std::uint64_t id, std::span<const MenubarTitle> titles) noexcept;
    MenubarSwitch exit_strip() noexcept;

    bool
    write_bar_semantics(std::string_view accessible_name, SemanticNodeData& output) const noexcept;
    bool write_title_semantics(
        const MenubarTitle& title,
        std::string_view accessible_name,
        SemanticNodeData& output
    ) const noexcept;

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    static bool is_selectable(const MenubarTitle& title) noexcept;
    static std::size_t index_of(std::span<const MenubarTitle> titles, std::uint64_t id) noexcept;
    static const MenubarTitle*
    find(std::span<const MenubarTitle> titles, std::uint64_t id) noexcept;

    static std::size_t first_selectable(std::span<const MenubarTitle> titles) noexcept;
    static std::size_t last_selectable(std::span<const MenubarTitle> titles) noexcept;
    static std::size_t
    step_selectable(std::span<const MenubarTitle> titles, std::size_t from, bool forward) noexcept;

    bool in_strip_{};
    bool has_focus_{};
    std::uint64_t focus_{};
    bool has_open_{};
    std::uint64_t open_{};
};

}

#endif
