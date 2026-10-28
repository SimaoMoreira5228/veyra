#ifndef VEYRA_BEHAVIORS_DISCLOSURE_HPP
#define VEYRA_BEHAVIORS_DISCLOSURE_HPP

#include "veyra/core/semantics/semantics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class DisclosureResult : std::uint8_t {
    ignored,
    handled,
    changed,
    overflow,
};

struct DisclosureState {
    std::size_t count{};
    bool disabled{};
};

class DisclosureController {
  public:
    static constexpr std::size_t kCapacity = 256;

    DisclosureState state() const noexcept;
    bool disabled() const noexcept;

    bool set_disabled(bool disabled) noexcept;

    std::size_t count() const noexcept;
    bool empty() const noexcept;
    bool contains(std::uint64_t id) const noexcept;
    std::size_t copy_expanded(std::span<std::uint64_t> output) const noexcept;

    bool write_disclosure_semantics(
        std::uint64_t id,
        std::string_view accessible_name,
        SemanticNodeData& output
    ) const noexcept;

    DisclosureResult expand(std::uint64_t id) noexcept;
    DisclosureResult collapse(std::uint64_t id) noexcept;
    DisclosureResult toggle_item(std::uint64_t id) noexcept;
    DisclosureResult
    collapse_subtree(std::uint64_t id, std::span<const std::uint64_t> descendants) noexcept;
    DisclosureResult expand_all(std::span<const std::uint64_t> ids) noexcept;
    DisclosureResult clear() noexcept;

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    std::size_t find(std::uint64_t id) const noexcept;
    void remove_at(std::size_t index) noexcept;

    std::array<std::uint64_t, kCapacity> ids_{};
    std::size_t count_{};
    bool disabled_{};
};

}

namespace veyra::behaviors {
using ::veyra::DisclosureController;
using ::veyra::DisclosureResult;
using ::veyra::DisclosureState;
}

namespace veyra::behaviors::navigation {
using ::veyra::DisclosureController;
using ::veyra::DisclosureResult;
using ::veyra::DisclosureState;
}

#endif
