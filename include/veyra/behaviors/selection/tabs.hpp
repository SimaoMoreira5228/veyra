#ifndef VEYRA_BEHAVIORS_TABS_HPP
#define VEYRA_BEHAVIORS_TABS_HPP

#include "veyra/core/semantics/semantics.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class TabsActivation : std::uint8_t {
    automatic,
    manual,
};

enum class TabsMove : std::uint8_t {
    next,
    prev,
    first,
    last,
};

enum class TabsResult : std::uint8_t {
    ignored,
    handled,
    changed,
};

struct TabsState {
    TabsActivation activation{TabsActivation::automatic};
    bool has_selected{};
    std::uint64_t selected{};
    bool has_cursor{};
    std::uint64_t cursor{};
};

struct TabsClose {
    TabsResult result{TabsResult::ignored};
    std::uint64_t closing{};
    bool has_fallback{};
    std::uint64_t fallback{};
};

class TabsController {
  public:
    TabsController() noexcept;

    TabsState state() const noexcept;
    TabsActivation activation() const noexcept;
    bool has_selected() const noexcept;
    std::uint64_t selected() const noexcept;
    bool has_cursor() const noexcept;
    std::uint64_t cursor() const noexcept;

    bool configure(TabsActivation activation) noexcept;

    TabsResult select(
        std::uint64_t id,
        std::span<const std::uint64_t> members,
        std::span<const std::uint64_t> disabled
    ) noexcept;

    TabsResult move_cursor(
        TabsMove direction,
        std::span<const std::uint64_t> members,
        std::span<const std::uint64_t> disabled
    ) noexcept;

    TabsResult activate(
        std::span<const std::uint64_t> members,
        std::span<const std::uint64_t> disabled
    ) noexcept;

    TabsClose close_request(
        std::uint64_t id,
        std::span<const std::uint64_t> members,
        std::span<const std::uint64_t> disabled
    ) noexcept;

    bool write_tab_semantics(
        std::uint64_t id,
        std::string_view accessible_name,
        bool tab_disabled,
        SemanticNodeData& output
    ) const noexcept;

    bool
    write_list_semantics(std::string_view accessible_name, SemanticNodeData& output) const noexcept;

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    static bool is_disabled(std::uint64_t id, std::span<const std::uint64_t> disabled) noexcept;

    static std::size_t index_of(std::span<const std::uint64_t> ids, std::uint64_t id) noexcept;

    static std::size_t first_enabled(
        std::span<const std::uint64_t> members,
        std::span<const std::uint64_t> disabled
    ) noexcept;

    static std::size_t last_enabled(
        std::span<const std::uint64_t> members,
        std::span<const std::uint64_t> disabled
    ) noexcept;

    static std::size_t step_enabled(
        std::span<const std::uint64_t> members,
        std::span<const std::uint64_t> disabled,
        std::size_t from,
        bool forward
    ) noexcept;

    TabsActivation activation_{TabsActivation::automatic};
    bool has_selected_{};
    std::uint64_t selected_{};
    bool has_cursor_{};
    std::uint64_t cursor_{};
};

}

namespace veyra::behaviors {
using ::veyra::TabsActivation;
using ::veyra::TabsClose;
using ::veyra::TabsController;
using ::veyra::TabsMove;
using ::veyra::TabsResult;
using ::veyra::TabsState;
}

namespace veyra::behaviors::selection {
using ::veyra::TabsActivation;
using ::veyra::TabsClose;
using ::veyra::TabsController;
using ::veyra::TabsMove;
using ::veyra::TabsResult;
using ::veyra::TabsState;
}

#endif
