#ifndef VEYRA_BEHAVIORS_PROGRESS_HPP
#define VEYRA_BEHAVIORS_PROGRESS_HPP

#include <cstdint>

namespace veyra {

enum class ProgressResult : std::uint8_t {
    ignored,
    handled,
    changed,
    invalid_input,
};

struct ProgressState {
    double fraction{};
    bool indeterminate{};
    bool cancellable{};
    bool canceled{};
};

struct ProgressTransition {
    ProgressResult result{ProgressResult::ignored};
    double fraction{};
};

class ProgressController {
  public:
    ProgressController() noexcept = default;

    ProgressState state() const noexcept;
    double fraction() const noexcept;
    bool indeterminate() const noexcept;
    bool cancellable() const noexcept;
    bool canceled() const noexcept;

    ProgressTransition set_fraction(double fraction) noexcept;
    bool set_indeterminate(bool indeterminate) noexcept;
    bool set_cancellable(bool cancellable) noexcept;
    ProgressResult cancel() noexcept;
    bool reset() noexcept;

  private:
    double fraction_{};
    bool indeterminate_{};
    bool cancellable_{};
    bool canceled_{};
};

}

namespace veyra::behaviors {
using ::veyra::ProgressController;
using ::veyra::ProgressResult;
using ::veyra::ProgressState;
using ::veyra::ProgressTransition;
}

namespace veyra::behaviors::control {
using ::veyra::ProgressController;
using ::veyra::ProgressResult;
using ::veyra::ProgressState;
using ::veyra::ProgressTransition;
}

#endif
