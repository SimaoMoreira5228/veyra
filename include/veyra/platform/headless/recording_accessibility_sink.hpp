#ifndef VEYRA_PLATFORM_RECORDING_ACCESSIBILITY_SINK_HPP
#define VEYRA_PLATFORM_RECORDING_ACCESSIBILITY_SINK_HPP

#include "veyra/platform/core/accessibility.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace veyra {

class RecordingAccessibilitySink : public AccessibilitySink {
  public:
    static constexpr std::size_t kNodeCapacity = 64;
    static constexpr std::size_t kTextCapacity = 64;

    void reset() noexcept;

    void begin_window(WindowId window) noexcept override;
    void node(const AccessibilityNode& record) noexcept override;
    void end_window(WindowId window) noexcept override;

    WindowId window() const noexcept;
    std::size_t begin_count() const noexcept;
    std::size_t end_count() const noexcept;
    std::size_t node_count() const noexcept;
    std::size_t dropped() const noexcept;
    const AccessibilityNode* at(std::size_t index) const noexcept;

  private:
    struct Record {
        AccessibilityNode node;
        std::array<char, kTextCapacity> name{};
        std::array<char, kTextCapacity> value{};
    };

    static std::string_view
    copy_text(std::array<char, kTextCapacity>& storage, std::string_view text) noexcept;

    std::array<Record, kNodeCapacity> records_{};
    WindowId window_{kInvalidWindowId};
    std::size_t begin_count_{};
    std::size_t end_count_{};
    std::size_t node_count_{};
    std::size_t dropped_{};
};

}

namespace veyra::platform {
using ::veyra::RecordingAccessibilitySink;
}

namespace veyra::platform::headless {
using ::veyra::RecordingAccessibilitySink;
}

#endif
