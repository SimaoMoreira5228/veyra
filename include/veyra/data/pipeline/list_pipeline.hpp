#ifndef VEYRA_DATA_LIST_PIPELINE_HPP
#define VEYRA_DATA_LIST_PIPELINE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

constexpr std::size_t kListLoadDirections = 3;

enum class ListLoadOrigin : std::uint8_t {
    source,
    mediator,
};

enum class ListLoadDirection : std::uint8_t {
    refresh,
    prepend,
    append,
};

enum class ListLoadState : std::uint8_t {
    loading,
    not_loading,
    error,
};

enum class ListPipelineResult : std::uint8_t {
    ignored,
    handled,
    changed,
    stale,
};

struct ListDirectionLoad {
    ListLoadState state{ListLoadState::not_loading};
    bool complete{};
};

struct ListDirectionStates {
    std::array<ListDirectionLoad, kListLoadDirections> loads{};
};

struct CombinedListLoadStates {
    ListDirectionStates source{};
    ListDirectionStates mediator{};
    bool has_mediator{};
};

struct ListLoadRequest {
    std::size_t first_index{};
    std::size_t count{};
    std::uint64_t generation{};
};

struct ListEdgeReach {
    bool prepend{};
    bool append{};
};

struct ListPipelineConfig {
    std::uint64_t search_debounce_ms{150};
    bool has_mediator{};
};

class ListPipeline {
  public:
    static constexpr std::size_t kDirections = kListLoadDirections;

    explicit ListPipeline(const ListPipelineConfig& config = {}) noexcept;

    ListPipelineResult request(
        ListLoadDirection direction,
        std::size_t first_index,
        std::size_t count,
        ListLoadOrigin origin = ListLoadOrigin::source
    ) noexcept;
    ListPipelineResult resolve(
        ListLoadDirection direction,
        std::uint64_t generation,
        bool success,
        ListLoadOrigin origin = ListLoadOrigin::source
    ) noexcept;
    ListPipelineResult fail(
        ListLoadDirection direction,
        std::uint64_t generation,
        ListLoadOrigin origin = ListLoadOrigin::source
    ) noexcept;
    ListPipelineResult
    retry(ListLoadDirection direction, ListLoadOrigin origin = ListLoadOrigin::source) noexcept;
    ListPipelineResult refresh() noexcept;

    bool set_complete(
        ListLoadDirection direction,
        bool complete,
        ListLoadOrigin origin = ListLoadOrigin::source
    ) noexcept;
    bool set_placeholders(std::size_t before, std::size_t after) noexcept;
    ListPipelineResult update_search(std::span<const char> query, std::uint64_t now_ms) noexcept;
    std::uint64_t invalidate_search() noexcept;
    bool set_search_debounce_ms(std::uint64_t debounce_ms) noexcept;
    bool set_mediator_present(bool present) noexcept;

    ListDirectionLoad load_state(ListLoadOrigin origin, ListLoadDirection direction) const noexcept;
    ListDirectionStates direction_states(ListLoadOrigin origin) const noexcept;
    CombinedListLoadStates combined_states() const noexcept;
    bool is_loading_anywhere() const noexcept;
    bool is_error(ListLoadDirection direction) const noexcept;
    bool should_show_placeholders() const noexcept;
    bool pending_request(
        ListLoadDirection direction,
        ListLoadRequest& output,
        ListLoadOrigin origin = ListLoadOrigin::source
    ) const noexcept;
    ListEdgeReach end_reached(
        std::size_t first_visible,
        std::size_t last_visible,
        std::size_t total_known,
        std::size_t threshold
    ) const noexcept;
    bool search_ready(std::uint64_t now_ms) const noexcept;
    std::uint64_t search_generation() const noexcept;
    std::span<const char> search_query() const noexcept;
    std::size_t placeholders_before() const noexcept;
    std::size_t placeholders_after() const noexcept;
    std::uint64_t search_debounce_ms() const noexcept;
    bool has_mediator() const noexcept;

  private:
    static constexpr std::size_t kMissing = static_cast<std::size_t>(-1);

    struct RequestSlot {
        std::size_t first_index{};
        std::size_t count{};
        std::uint64_t generation{};
        bool pending{};
    };

    struct Layer {
        std::array<ListDirectionLoad, kDirections> loads{};
        std::array<RequestSlot, kDirections> slots{};
    };

    static std::size_t origin_index(ListLoadOrigin origin) noexcept;
    static std::size_t direction_index(ListLoadDirection direction) noexcept;
    static bool same_query(std::span<const char> left, std::span<const char> right) noexcept;
    bool layer_complete(std::size_t slot) const noexcept;

    std::array<Layer, 2> layers_{};
    std::size_t placeholders_before_{};
    std::size_t placeholders_after_{};
    std::span<const char> search_query_{};
    std::uint64_t search_generation_{};
    std::uint64_t search_last_change_ms_{};
    std::uint64_t search_debounce_ms_{150};
    bool has_mediator_{};
};

}

namespace veyra::data {
using ::veyra::CombinedListLoadStates;
using ::veyra::ListDirectionLoad;
using ::veyra::ListDirectionStates;
using ::veyra::ListEdgeReach;
using ::veyra::ListLoadDirection;
using ::veyra::ListLoadOrigin;
using ::veyra::ListLoadRequest;
using ::veyra::ListLoadState;
using ::veyra::ListPipeline;
using ::veyra::ListPipelineConfig;
using ::veyra::ListPipelineResult;
using ::veyra::kListLoadDirections;
}

namespace veyra::data::pipeline {
using ::veyra::CombinedListLoadStates;
using ::veyra::ListDirectionLoad;
using ::veyra::ListDirectionStates;
using ::veyra::ListEdgeReach;
using ::veyra::ListLoadDirection;
using ::veyra::ListLoadOrigin;
using ::veyra::ListLoadRequest;
using ::veyra::ListLoadState;
using ::veyra::ListPipeline;
using ::veyra::ListPipelineConfig;
using ::veyra::ListPipelineResult;
using ::veyra::kListLoadDirections;
}

#endif
