#ifndef VEYRA_BEHAVIORS_BREADCRUMB_HPP
#define VEYRA_BEHAVIORS_BREADCRUMB_HPP

#include "veyra/core/semantics/semantics.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class BreadcrumbResult : std::uint8_t {
    ignored,
    handled,
    changed,
};

enum class BreadcrumbMove : std::uint8_t {
    next,
    prev,
    first,
    last,
};

enum class BreadcrumbOverflowResult : std::uint8_t {
    complete,
    collapsed,
    invalid_input,
};

struct BreadcrumbItem {
    std::uint64_t id{};
    bool disabled{};
};

struct BreadcrumbAction {
    BreadcrumbResult result{BreadcrumbResult::ignored};
    std::uint64_t id{};
};

struct BreadcrumbOverflow {
    BreadcrumbOverflowResult result{BreadcrumbOverflowResult::invalid_input};
    std::size_t visible_count{};
    std::size_t collapsed_count{};
};

class BreadcrumbBar {
  public:
    BreadcrumbBar() noexcept;

    bool has_cursor() const noexcept;
    std::uint64_t cursor() const noexcept;

    BreadcrumbAction select(std::uint64_t id, std::span<const BreadcrumbItem> trail) noexcept;

    BreadcrumbResult
    move_cursor(BreadcrumbMove direction, std::span<const BreadcrumbItem> trail) noexcept;

    BreadcrumbAction activate(std::span<const BreadcrumbItem> trail) noexcept;

    bool write_crumb_semantics(
        std::uint64_t id,
        std::string_view accessible_name,
        bool crumb_disabled,
        SemanticNodeData& output
    ) const noexcept;

    bool
    write_list_semantics(std::string_view accessible_name, SemanticNodeData& output) const noexcept;

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    static bool is_selectable(const BreadcrumbItem& item) noexcept;
    static std::size_t index_of(std::span<const BreadcrumbItem> trail, std::uint64_t id) noexcept;

    static std::size_t first_selectable(std::span<const BreadcrumbItem> trail) noexcept;
    static std::size_t last_selectable(std::span<const BreadcrumbItem> trail) noexcept;

    static std::size_t
    step_selectable(std::span<const BreadcrumbItem> trail, std::size_t from, bool forward) noexcept;

    bool has_cursor_{};
    std::uint64_t cursor_{};
};

BreadcrumbOverflow resolve_breadcrumb_overflow(
    std::span<const BreadcrumbItem> trail,
    std::size_t max_visible,
    std::span<std::uint64_t> visible_ids
) noexcept;

}

#endif
