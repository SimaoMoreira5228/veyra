#ifndef VEYRA_PLATFORM_ACCESSIBILITY_HPP
#define VEYRA_PLATFORM_ACCESSIBILITY_HPP

#include "veyra/core/semantics/semantics.hpp"
#include "veyra/foundation/geometry.hpp"
#include "veyra/platform/core/window.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace veyra {

// NOTE: name and value are borrowed for the duration of the sink call. A sink
// that keeps them past return must copy into its own storage.
struct AccessibilityNode {
    WindowId window{kInvalidWindowId};
    NodeId id{};
    NodeId parent{};
    SemanticRole role{SemanticRole::none};
    std::string_view name;
    std::string_view value;
    LayoutRect bounds{};
    std::uint16_t states{};
    std::uint16_t actions{};
    std::uint64_t item_key{};
    std::size_t position_in_set{};
    std::size_t set_size{};
    bool virtualized{};
    bool visible{true};
};

class AccessibilitySink {
  public:
    virtual ~AccessibilitySink() = default;

    virtual void begin_window(WindowId window) noexcept = 0;
    virtual void node(const AccessibilityNode& record) noexcept = 0;
    virtual void end_window(WindowId window) noexcept = 0;
};

constexpr bool accessibility_value_protected(std::uint16_t states) noexcept {
    return has_state(states, SemanticState::protected_value);
}

}

namespace veyra::platform {
using ::veyra::accessibility_value_protected;
using ::veyra::AccessibilityNode;
using ::veyra::AccessibilitySink;
}

namespace veyra::platform::core {
using ::veyra::accessibility_value_protected;
using ::veyra::AccessibilityNode;
using ::veyra::AccessibilitySink;
}

#endif
