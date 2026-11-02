#ifndef VEYRA_PLATFORM_ATSPI_SINK_HPP
#define VEYRA_PLATFORM_ATSPI_SINK_HPP

#include "veyra/platform/core/accessibility.hpp"

#include <cstddef>
#include <memory>

namespace veyra {

class AtspiSink final : public AccessibilitySink {
  public:
    static constexpr std::size_t kNodeCapacity = 64;

    using ActionHandler = void (*)(void* user, NodeId node) noexcept;

    AtspiSink() noexcept;
    ~AtspiSink() override;

    AtspiSink(const AtspiSink&) = delete;
    AtspiSink& operator=(const AtspiSink&) = delete;

    bool connect() noexcept;
    bool connected() const noexcept;
    void close() noexcept;

    void set_action_handler(ActionHandler handler, void* user) noexcept;

    void begin_window(WindowId window) noexcept override;
    void node(const AccessibilityNode& record) noexcept override;
    void end_window(WindowId window) noexcept override;

    std::size_t node_count() const noexcept;
    const char* bus_name() const noexcept;
    const char* bus_address() const noexcept;

    struct Impl;

  private:
    std::unique_ptr<Impl> impl_;
};

}

namespace veyra::platform {
using ::veyra::AtspiSink;
}

namespace veyra::platform::linux {
using ::veyra::AtspiSink;
}

#endif
