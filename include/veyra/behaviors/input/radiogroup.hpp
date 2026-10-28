#ifndef VEYRA_BEHAVIORS_RADIOGROUP_HPP
#define VEYRA_BEHAVIORS_RADIOGROUP_HPP

#include "veyra/core/semantics/semantics.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

struct RadioOption {
    std::uint64_t id{};
    bool disabled{};
};

enum class RadioKey : std::uint8_t {
    left,
    up,
    right,
    down,
    home,
    end,
};

enum class RadioResult : std::uint8_t {
    ignored,
    handled,
    changed,
};

struct RadioGroupConfig {
    std::span<const RadioOption> options{};
    std::uint64_t selected{};
    bool has_selection{true};
    bool allow_none{};
};

struct RadioTransition {
    RadioResult result{RadioResult::ignored};
    std::uint64_t selected{};
    bool has_selection{};
};

struct RadioGroupState {
    std::size_t count{};
    std::uint64_t selected{};
    bool has_selection{};
    bool focused{};
    bool disabled{};
};

class RadioGroupController {
  public:
    RadioGroupState state() const noexcept;
    std::uint64_t selected() const noexcept;
    bool has_selection() const noexcept;
    std::size_t count() const noexcept;
    bool focused() const noexcept;
    bool disabled() const noexcept;

    bool configure(const RadioGroupConfig& config) noexcept;
    bool set_disabled(bool disabled) noexcept;
    RadioResult set_focused(bool focused) noexcept;

    RadioTransition select(std::uint64_t id) noexcept;
    RadioTransition key_down(RadioKey key) noexcept;

    bool write_semantics(
        std::string_view accessible_name,
        std::uint64_t id,
        SemanticNodeData& output
    ) const noexcept;

  private:
    std::size_t enabled_count() const noexcept;
    std::size_t index_of(std::uint64_t id) const noexcept;
    std::size_t first_enabled() const noexcept;
    std::size_t last_enabled() const noexcept;
    std::size_t step_from(std::size_t index, bool forward) const noexcept;
    RadioTransition commit(std::size_t index) noexcept;

    static constexpr std::size_t kMissing = static_cast<std::size_t>(-1);

    const RadioOption* options_{};
    std::size_t count_{};
    std::size_t selected_index_{kMissing};
    bool focused_{};
    bool disabled_{};
};

}

namespace veyra::behaviors {
using ::veyra::RadioGroupConfig;
using ::veyra::RadioGroupController;
using ::veyra::RadioGroupState;
using ::veyra::RadioKey;
using ::veyra::RadioOption;
using ::veyra::RadioResult;
using ::veyra::RadioTransition;
}

namespace veyra::behaviors::input {
using ::veyra::RadioGroupConfig;
using ::veyra::RadioGroupController;
using ::veyra::RadioGroupState;
using ::veyra::RadioKey;
using ::veyra::RadioOption;
using ::veyra::RadioResult;
using ::veyra::RadioTransition;
}

#endif
