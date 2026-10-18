#include "veyra/data/list_pipeline.hpp"

#include <limits>

namespace veyra {

ListPipeline::ListPipeline(const ListPipelineConfig& config) noexcept :
    search_debounce_ms_(config.search_debounce_ms), has_mediator_(config.has_mediator) {
}

ListPipelineResult ListPipeline::request(
    ListLoadDirection direction,
    std::size_t first_index,
    std::size_t count,
    ListLoadOrigin origin
) noexcept {
    const std::size_t layer = origin_index(origin);
    const std::size_t slot = direction_index(direction);

    if (layer == kMissing || slot == kMissing) {
        return ListPipelineResult::ignored;
    }

    if (count == 0 || first_index > std::numeric_limits<std::size_t>::max() - count) {
        return ListPipelineResult::ignored;
    }

    Layer& state = layers_[layer];

    if (state.loads[slot].complete) {
        return ListPipelineResult::ignored;
    }

    RequestSlot& pending = state.slots[slot];

    if (pending.pending && pending.first_index == first_index && pending.count == count) {
        return ListPipelineResult::handled;
    }

    pending.first_index = first_index;
    pending.count = count;
    ++pending.generation;
    pending.pending = true;
    state.loads[slot].state = ListLoadState::loading;
    state.loads[slot].complete = false;

    return ListPipelineResult::changed;
}

ListPipelineResult ListPipeline::resolve(
    ListLoadDirection direction,
    std::uint64_t generation,
    bool success,
    ListLoadOrigin origin
) noexcept {
    const std::size_t layer = origin_index(origin);
    const std::size_t slot = direction_index(direction);

    if (layer == kMissing || slot == kMissing) {
        return ListPipelineResult::ignored;
    }

    Layer& state = layers_[layer];
    RequestSlot& pending = state.slots[slot];

    if (!pending.pending || generation != pending.generation) {
        return ListPipelineResult::stale;
    }

    pending.pending = false;
    state.loads[slot].state = success ? ListLoadState::not_loading : ListLoadState::error;

    return ListPipelineResult::changed;
}

ListPipelineResult ListPipeline::fail(
    ListLoadDirection direction,
    std::uint64_t generation,
    ListLoadOrigin origin
) noexcept {
    const std::size_t layer = origin_index(origin);
    const std::size_t slot = direction_index(direction);

    if (layer == kMissing || slot == kMissing) {
        return ListPipelineResult::ignored;
    }

    Layer& state = layers_[layer];
    RequestSlot& pending = state.slots[slot];

    if (!pending.pending || generation != pending.generation) {
        return ListPipelineResult::stale;
    }

    pending.pending = false;
    state.loads[slot].state = ListLoadState::error;

    return ListPipelineResult::changed;
}

ListPipelineResult
ListPipeline::retry(ListLoadDirection direction, ListLoadOrigin origin) noexcept {
    const std::size_t layer = origin_index(origin);
    const std::size_t slot = direction_index(direction);

    if (layer == kMissing || slot == kMissing) {
        return ListPipelineResult::ignored;
    }

    Layer& state = layers_[layer];

    if (state.loads[slot].state != ListLoadState::error) {
        return ListPipelineResult::ignored;
    }

    RequestSlot& pending = state.slots[slot];

    ++pending.generation;
    pending.pending = true;
    state.loads[slot].state = ListLoadState::loading;
    state.loads[slot].complete = false;

    return ListPipelineResult::changed;
}

ListPipelineResult ListPipeline::refresh() noexcept {
    for (std::size_t layer = 0; layer < layers_.size(); ++layer) {
        if (layer == origin_index(ListLoadOrigin::mediator) && !has_mediator_) {
            continue;
        }

        for (std::size_t slot = 0; slot < kDirections; ++slot) {
            RequestSlot& pending = layers_[layer].slots[slot];
            ++pending.generation;
            pending.pending = false;
            pending.first_index = 0;
            pending.count = 0;

            const bool is_refresh = slot == direction_index(ListLoadDirection::refresh);
            layers_[layer].loads[slot].state =
                is_refresh ? ListLoadState::loading : ListLoadState::not_loading;
            layers_[layer].loads[slot].complete = false;
        }
    }

    placeholders_before_ = 0;
    placeholders_after_ = 0;

    return ListPipelineResult::changed;
}

bool ListPipeline::set_complete(
    ListLoadDirection direction,
    bool complete,
    ListLoadOrigin origin
) noexcept {
    const std::size_t layer = origin_index(origin);
    const std::size_t slot = direction_index(direction);

    if (layer == kMissing || slot == kMissing) {
        return false;
    }

    ListDirectionLoad& load = layers_[layer].loads[slot];

    if (load.state == ListLoadState::loading || load.complete == complete) {
        return false;
    }

    load.complete = complete;

    return true;
}

bool ListPipeline::set_placeholders(std::size_t before, std::size_t after) noexcept {
    if (placeholders_before_ == before && placeholders_after_ == after) {
        return false;
    }

    placeholders_before_ = before;
    placeholders_after_ = after;

    return true;
}

ListPipelineResult
ListPipeline::update_search(std::span<const char> query, std::uint64_t now_ms) noexcept {
    if (!same_query(query, search_query_)) {
        search_query_ = query;
        search_last_change_ms_ = now_ms;
        ++search_generation_;

        return ListPipelineResult::changed;
    }

    search_last_change_ms_ = now_ms;

    return ListPipelineResult::handled;
}

std::uint64_t ListPipeline::invalidate_search() noexcept {
    return ++search_generation_;
}

bool ListPipeline::set_search_debounce_ms(std::uint64_t debounce_ms) noexcept {
    if (search_debounce_ms_ == debounce_ms) {
        return false;
    }

    search_debounce_ms_ = debounce_ms;

    return true;
}

bool ListPipeline::set_mediator_present(bool present) noexcept {
    if (has_mediator_ == present) {
        return false;
    }

    has_mediator_ = present;

    return true;
}

ListDirectionLoad
ListPipeline::load_state(ListLoadOrigin origin, ListLoadDirection direction) const noexcept {
    const std::size_t layer = origin_index(origin);
    const std::size_t slot = direction_index(direction);

    if (layer == kMissing || slot == kMissing) {
        return {};
    }

    return layers_[layer].loads[slot];
}

ListDirectionStates ListPipeline::direction_states(ListLoadOrigin origin) const noexcept {
    ListDirectionStates states{};

    const std::size_t layer = origin_index(origin);

    if (layer == kMissing) {
        return states;
    }

    states.loads = layers_[layer].loads;

    return states;
}

CombinedListLoadStates ListPipeline::combined_states() const noexcept {
    CombinedListLoadStates combined{};

    combined.source.loads = layers_[origin_index(ListLoadOrigin::source)].loads;
    combined.mediator.loads = layers_[origin_index(ListLoadOrigin::mediator)].loads;
    combined.has_mediator = has_mediator_;

    return combined;
}

bool ListPipeline::is_loading_anywhere() const noexcept {
    for (std::size_t layer = 0; layer < layers_.size(); ++layer) {
        if (layer == origin_index(ListLoadOrigin::mediator) && !has_mediator_) {
            continue;
        }

        for (const auto& load : layers_[layer].loads) {
            if (load.state == ListLoadState::loading) {
                return true;
            }
        }
    }

    return false;
}

bool ListPipeline::is_error(ListLoadDirection direction) const noexcept {
    const std::size_t slot = direction_index(direction);

    if (slot == kMissing) {
        return false;
    }

    if (layers_[origin_index(ListLoadOrigin::source)].loads[slot].state == ListLoadState::error) {
        return true;
    }

    if (!has_mediator_) {
        return false;
    }

    return layers_[origin_index(ListLoadOrigin::mediator)].loads[slot].state ==
        ListLoadState::error;
}

bool ListPipeline::should_show_placeholders() const noexcept {
    return placeholders_before_ > 0 || placeholders_after_ > 0;
}

bool ListPipeline::pending_request(
    ListLoadDirection direction,
    ListLoadRequest& output,
    ListLoadOrigin origin
) const noexcept {
    const std::size_t layer = origin_index(origin);
    const std::size_t slot = direction_index(direction);

    if (layer == kMissing || slot == kMissing) {
        return false;
    }

    const RequestSlot& pending = layers_[layer].slots[slot];

    if (!pending.pending) {
        return false;
    }

    output = {
        .first_index = pending.first_index,
        .count = pending.count,
        .generation = pending.generation,
    };

    return true;
}

ListEdgeReach ListPipeline::end_reached(
    std::size_t first_visible,
    std::size_t last_visible,
    std::size_t total_known,
    std::size_t threshold
) const noexcept {
    ListEdgeReach reach{};

    if (total_known == 0 || first_visible > last_visible || last_visible >= total_known) {
        return reach;
    }

    const std::size_t prepend = direction_index(ListLoadDirection::prepend);
    const std::size_t append = direction_index(ListLoadDirection::append);

    if (!layer_complete(prepend) && placeholders_before_ > 0 && first_visible <= threshold) {
        reach.prepend = true;
    }

    if (!layer_complete(append) && placeholders_after_ > 0 &&
        threshold >= total_known - last_visible - 1) {
        reach.append = true;
    }

    return reach;
}

bool ListPipeline::search_ready(std::uint64_t now_ms) const noexcept {
    if (now_ms < search_last_change_ms_) {
        return false;
    }

    return now_ms - search_last_change_ms_ >= search_debounce_ms_;
}

std::uint64_t ListPipeline::search_generation() const noexcept {
    return search_generation_;
}

std::span<const char> ListPipeline::search_query() const noexcept {
    return search_query_;
}

std::size_t ListPipeline::placeholders_before() const noexcept {
    return placeholders_before_;
}

std::size_t ListPipeline::placeholders_after() const noexcept {
    return placeholders_after_;
}

std::uint64_t ListPipeline::search_debounce_ms() const noexcept {
    return search_debounce_ms_;
}

bool ListPipeline::has_mediator() const noexcept {
    return has_mediator_;
}

std::size_t ListPipeline::origin_index(ListLoadOrigin origin) noexcept {
    switch (origin) {
    case ListLoadOrigin::source:
        return 0;
    case ListLoadOrigin::mediator:
        return 1;
    }

    return kMissing;
}

std::size_t ListPipeline::direction_index(ListLoadDirection direction) noexcept {
    switch (direction) {
    case ListLoadDirection::refresh:
        return 0;
    case ListLoadDirection::prepend:
        return 1;
    case ListLoadDirection::append:
        return 2;
    }

    return kMissing;
}

bool ListPipeline::same_query(std::span<const char> left, std::span<const char> right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }

    return true;
}

bool ListPipeline::layer_complete(std::size_t slot) const noexcept {
    if (!layers_[origin_index(ListLoadOrigin::source)].loads[slot].complete) {
        return false;
    }

    if (!has_mediator_) {
        return true;
    }

    return layers_[origin_index(ListLoadOrigin::mediator)].loads[slot].complete;
}

}
