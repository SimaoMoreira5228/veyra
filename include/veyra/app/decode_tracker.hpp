#ifndef VEYRA_APP_DECODE_TRACKER_HPP
#define VEYRA_APP_DECODE_TRACKER_HPP

#include "veyra/platform/core/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace veyra::app {

enum class DecodeState : std::uint8_t {
    pending,
    ready,
    discarded,
};

struct DecodeEntry {
    WindowId window{};
    std::uint64_t request{};
    DecodeState state{DecodeState::pending};
    bool active{};
};

class DecodeTracker {
  public:
    static constexpr std::size_t kCapacity = 32;

    bool begin(WindowId window, std::uint64_t request) noexcept;
    bool complete(std::uint64_t request) noexcept;
    bool take(std::uint64_t request) noexcept;
    bool discarded(std::uint64_t request) const noexcept;
    void discard_window(WindowId window) noexcept;
    std::size_t pending() const noexcept;

  private:
    DecodeEntry* find(std::uint64_t request) noexcept;
    const DecodeEntry* find(std::uint64_t request) const noexcept;

    std::array<DecodeEntry, kCapacity> entries_{};
};

}

namespace veyra::app {
using ::veyra::app::DecodeEntry;
using ::veyra::app::DecodeState;
using ::veyra::app::DecodeTracker;
}

#endif
