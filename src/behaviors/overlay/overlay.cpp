#include "veyra/behaviors/overlay/overlay.hpp"

namespace veyra {

bool OverlayController::empty() const noexcept {
    return depth_ == 0;
}

std::size_t OverlayController::depth() const noexcept {
    return depth_;
}

bool OverlayController::contains(NodeId dialog) const noexcept {
    for (std::size_t i = 0; i < depth_; ++i) {
        if (layers_[i].dialog == dialog) {
            return true;
        }
    }

    return false;
}

NodeId OverlayController::trap_root() const noexcept {
    if (depth_ == 0) {
        return {};
    }

    return layers_[depth_ - 1].dialog;
}

OverlayResult OverlayController::open(const OverlayLayer& layer) noexcept {
    if (layer.dialog.is_null()) {
        return OverlayResult::ignored;
    }

    if (depth_ == kCapacity) {
        return OverlayResult::overflow;
    }

    layers_[depth_] = layer;
    ++depth_;

    return OverlayResult::opened;
}

OverlayClose OverlayController::close_top() noexcept {
    if (depth_ == 0) {
        return {};
    }

    --depth_;
    const auto& layer = layers_[depth_];

    return {
        .result = OverlayResult::closed,
        .dialog = layer.dialog,
        .restore_focus = layer.restore_focus,
        .kind = layer.kind,
        .restore_control = layer.restore_control,
    };
}

OverlayClose OverlayController::close_top(FocusChain& focus) noexcept {
    const auto restore = depth_ == 0 ? std::uint64_t{} : layers_[depth_ - 1].restore_control;
    const auto close = close_top();

    if (close.result == OverlayResult::closed && restore != 0) {
        focus.restore(restore);
    }

    return close;
}

OverlayReset OverlayController::close_all() noexcept {
    if (depth_ == 0) {
        return {};
    }

    const auto restore = layers_[0].restore_focus;
    const auto closed = depth_;
    depth_ = 0;

    return {
        .closed = closed,
        .restore_focus = restore,
    };
}

OverlayReset OverlayController::close_all(FocusChain& focus) noexcept {
    const auto restore = depth_ == 0 ? std::uint64_t{} : layers_[0].restore_control;
    const auto reset = close_all();

    if (reset.closed != 0 && restore != 0) {
        focus.restore(restore);
    }

    return reset;
}

OverlayIntent OverlayController::escape() const noexcept {
    if (depth_ == 0) {
        return {};
    }

    const auto& top = layers_[depth_ - 1];
    if (!top.escape_cancels) {
        return {
            .result = OverlayResult::handled,
            .dialog = top.dialog,
        };
    }

    return {
        .result = OverlayResult::cancel,
        .dialog = top.dialog,
    };
}

OverlayIntent OverlayController::request_default() const noexcept {
    if (depth_ == 0) {
        return {};
    }

    return {
        .result = OverlayResult::activate_default,
        .dialog = layers_[depth_ - 1].dialog,
    };
}

OverlayIntent OverlayController::outside_press() const noexcept {
    if (depth_ == 0) {
        return {};
    }

    const auto& top = layers_[depth_ - 1];
    if (top.kind != OverlayKind::popover) {
        return {
            .result = OverlayResult::handled,
            .dialog = top.dialog,
        };
    }

    return {
        .result = OverlayResult::dismiss,
        .dialog = top.dialog,
    };
}

bool OverlayController::write_dialog_semantics(
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (depth_ == 0) {
        return false;
    }

    if (accessible_name.empty()) {
        return false;
    }

    const auto& top = layers_[depth_ - 1];

    std::uint16_t states{};
    if (top.kind == OverlayKind::modal) {
        states = states | SemanticState::modal;
    }

    std::uint16_t actions{};
    if (top.escape_cancels || top.kind == OverlayKind::popover) {
        actions = action_flag(SemanticAction::dismiss);
    }

    output = {
        .role = SemanticRole::dialog,
        .label = accessible_name,
        .states = states,
        .actions = actions,
    };

    return true;
}

}
