#ifndef VEYRA_BEHAVIORS_FOCUS_CHAIN_HPP
#define VEYRA_BEHAVIORS_FOCUS_CHAIN_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace veyra {

class FocusChain {
  public:
    static constexpr std::size_t kCapacity = 64;

    FocusChain() noexcept;

    std::size_t count() const noexcept;
    bool empty() const noexcept;
    bool contains(std::uint64_t id) const noexcept;
    bool is_enabled(std::uint64_t id) const noexcept;
    std::uint64_t focused() const noexcept;
    bool has_focus() const noexcept;
    std::uint64_t generation() const noexcept;

    bool register_item(std::uint64_t id, bool item_enabled = true) noexcept;
    bool unregister(std::uint64_t id) noexcept;
    bool set_enabled(std::uint64_t id, bool on) noexcept;

    bool focus(std::uint64_t id) noexcept;
    bool focus_next() noexcept;
    bool focus_prev() noexcept;
    bool focus_first() noexcept;
    bool focus_last() noexcept;

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    struct FocusEntry {
        std::uint64_t id{};
        bool item_enabled{true};
    };

    std::size_t index_of(std::uint64_t id) const noexcept;
    std::size_t first_enabled() const noexcept;
    std::size_t last_enabled() const noexcept;
    std::size_t step_enabled(std::size_t from, bool forward) const noexcept;
    void move_focus_to_fallback(std::size_t forward_start, std::size_t backward_start) noexcept;

    std::array<FocusEntry, kCapacity> entries_{};
    std::size_t count_{};
    bool has_focus_{};
    std::uint64_t focused_{};
    std::uint64_t generation_{};
};

}

namespace veyra::behaviors {
using ::veyra::FocusChain;
}

namespace veyra::behaviors::navigation {
using ::veyra::FocusChain;
}

#endif
