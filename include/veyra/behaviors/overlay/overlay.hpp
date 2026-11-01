#ifndef VEYRA_BEHAVIORS_OVERLAY_HPP
#define VEYRA_BEHAVIORS_OVERLAY_HPP

#include "veyra/behaviors/navigation/focus_chain.hpp"
#include "veyra/core/semantics/semantics.hpp"
#include "veyra/core/tree/node_tree.hpp"

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
    std::uint64_t restore_control{};
};

struct OverlayClose {
    OverlayResult result{OverlayResult::ignored};
    NodeId dialog;
    NodeId restore_focus;
    OverlayKind kind{OverlayKind::modal};
    std::uint64_t restore_control{};
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
    OverlayClose close_top(FocusChain& focus) noexcept;
    OverlayReset close_all() noexcept;
    OverlayReset close_all(FocusChain& focus) noexcept;

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

namespace veyra::behaviors {
using ::veyra::OverlayClose;
using ::veyra::OverlayController;
using ::veyra::OverlayIntent;
using ::veyra::OverlayKind;
using ::veyra::OverlayLayer;
using ::veyra::OverlayReset;
using ::veyra::OverlayResult;
}

namespace veyra::behaviors::overlay {
using ::veyra::OverlayClose;
using ::veyra::OverlayController;
using ::veyra::OverlayIntent;
using ::veyra::OverlayKind;
using ::veyra::OverlayLayer;
using ::veyra::OverlayReset;
using ::veyra::OverlayResult;
}

#endif
