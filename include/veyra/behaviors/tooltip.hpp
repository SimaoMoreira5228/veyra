#ifndef VEYRA_BEHAVIORS_TOOLTIP_HPP
#define VEYRA_BEHAVIORS_TOOLTIP_HPP

#include "veyra/core/semantics.hpp"

#include <cstdint>
#include <string_view>

namespace veyra {

enum class TooltipVisibility : std::uint8_t {
    hidden,
    pending,
    shown,
};

enum class TooltipResult : std::uint8_t {
    ignored,
    handled,
    changed,
    shown,
    rejected,
};

class TooltipController {
  public:
    TooltipController() noexcept;

    TooltipVisibility visibility() const noexcept;
    std::uint64_t anchor() const noexcept;
    std::uint64_t now_ms() const noexcept;

    TooltipResult
    hover_start(std::uint64_t anchor, std::uint32_t dwell_ms, std::uint64_t now_ms) noexcept;

    TooltipResult advance(std::uint64_t now_ms) noexcept;
    TooltipResult hide() noexcept;

    bool write_tooltip_semantics(
        std::string_view accessible_name,
        SemanticNodeData& output
    ) const noexcept;

  private:
    TooltipVisibility visibility_{TooltipVisibility::hidden};
    std::uint64_t anchor_{};
    std::uint64_t deadline_ms_{};
    std::uint64_t now_ms_{};
};

}

#endif
