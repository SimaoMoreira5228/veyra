#ifndef VEYRA_BEHAVIORS_OVERLAY_HPP
#define VEYRA_BEHAVIORS_OVERLAY_HPP

#include "veyra/core/tree/node_tree.hpp"
#include "veyra/core/semantics/semantics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace veyra {

enum class OverlayKind : std::uint8_t {
    modal,
    popover,
};

enum class OverlayResult : std::uint8_t {
    ignored,
    handled,
    opened,
    closed,
    overflow,
    dismiss,
    cancel,
    activate_default,
};

struct OverlayLayer {
    NodeId dialog;
    NodeId restore_focus;
    bool escape_cancels{true};
    OverlayKind kind{OverlayKind::modal};
};

struct OverlayClose {
    OverlayResult result{OverlayResult::ignored};
    NodeId dialog;
    NodeId restore_focus;
    OverlayKind kind{OverlayKind::modal};
};

struct OverlayIntent {
    OverlayResult result{OverlayResult::ignored};
    NodeId dialog;
};

struct OverlayReset {
    std::size_t closed{};
    NodeId restore_focus;
};

class OverlayController {
  public:
    static constexpr std::size_t kCapacity = 8;

    bool empty() const noexcept;
    std::size_t depth() const noexcept;
    bool contains(NodeId dialog) const noexcept;
    NodeId trap_root() const noexcept;

    OverlayResult open(const OverlayLayer& layer) noexcept;
    OverlayClose close_top() noexcept;
    OverlayReset close_all() noexcept;

    OverlayIntent escape() const noexcept;
    OverlayIntent request_default() const noexcept;
    OverlayIntent outside_press() const noexcept;

    bool write_dialog_semantics(
        std::string_view accessible_name,
        SemanticNodeData& output
    ) const noexcept;

  private:
    std::array<OverlayLayer, kCapacity> layers_{};
    std::size_t depth_{};
};

}

#endif
