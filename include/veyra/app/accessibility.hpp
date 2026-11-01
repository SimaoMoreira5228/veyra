#ifndef VEYRA_APP_ACCESSIBILITY_HPP
#define VEYRA_APP_ACCESSIBILITY_HPP

#include "veyra/layout/scroll/virtual_semantics.hpp"
#include "veyra/platform/core/accessibility.hpp"

#include <cstdint>
#include <span>

namespace veyra::app {

enum class AccessibilityPublishResult : std::uint8_t {
    complete,
    invalid_window,
    invalid_node,
};

class AccessibilityBridge {
  public:
    explicit AccessibilityBridge(AccessibilitySink& sink) noexcept;

    AccessibilityPublishResult
    publish_window(WindowId window, std::span<const SemanticNode> nodes) noexcept;
    AccessibilityPublishResult publish_virtual_list(
        WindowId window,
        NodeId list,
        std::span<const VirtualSemanticNode> items
    ) noexcept;

  private:
    AccessibilitySink* sink_;
};

}

namespace veyra::app {
using ::veyra::app::AccessibilityBridge;
using ::veyra::app::AccessibilityPublishResult;
}

#endif
