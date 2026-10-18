#include "veyra/data/list_pipeline.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

constexpr veyra::ListLoadDirection kRefresh = veyra::ListLoadDirection::refresh;
constexpr veyra::ListLoadDirection kPrepend = veyra::ListLoadDirection::prepend;
constexpr veyra::ListLoadDirection kAppend = veyra::ListLoadDirection::append;
constexpr veyra::ListLoadOrigin kSource = veyra::ListLoadOrigin::source;
constexpr veyra::ListLoadOrigin kMediator = veyra::ListLoadOrigin::mediator;

void check_initial_state() {
    auto pipeline = veyra::ListPipeline{};

    require(!pipeline.is_loading_anywhere(), "a new pipeline loads nothing");
    require(!pipeline.is_error(kRefresh), "a new pipeline reports no refresh error");
    require(!pipeline.is_error(kAppend), "a new pipeline reports no append error");
    require(!pipeline.should_show_placeholders(), "a new pipeline shows no placeholders");
    require(pipeline.placeholders_before() == 0, "a new pipeline has no head placeholders");
    require(pipeline.placeholders_after() == 0, "a new pipeline has no tail placeholders");
    require(pipeline.search_generation() == 0, "a new pipeline has no search generation");
    require(pipeline.search_query().empty(), "a new pipeline holds no search text");
    require(!pipeline.has_mediator(), "a new pipeline has no mediator layer by default");

    auto request = veyra::ListLoadRequest{};
    require(!pipeline.pending_request(kRefresh, request), "a new pipeline has no pending refresh");

    const auto states = pipeline.combined_states();
    require(!states.has_mediator, "combined states report no mediator by default");
    require(
        states.source.loads[0].state == veyra::ListLoadState::not_loading,
        "a new source refresh idles"
    );
    require(!states.source.loads[0].complete, "a new source refresh is not complete");

    const auto reach = pipeline.end_reached(0, 9, 100, 20);
    require(!reach.prepend && !reach.append, "no unloaded ranges means no edge is reached");
}

void check_request_and_resolve() {
    auto pipeline = veyra::ListPipeline{};

    require(
        pipeline.request(kAppend, 100, 25) == veyra::ListPipelineResult::changed,
        "requesting an append starts a load"
    );
    require(pipeline.is_loading_anywhere(), "an append request loads somewhere");

    auto pending = veyra::ListLoadRequest{};
    require(pipeline.pending_request(kAppend, pending), "the append request is pending");
    require(
        pending.first_index == 100 && pending.count == 25 && pending.generation == 1,
        "the pending append carries its range and first generation"
    );
    require(
        pipeline.load_state(kSource, kAppend).state == veyra::ListLoadState::loading,
        "the source append layer reports loading"
    );

    require(
        pipeline.resolve(kAppend, pending.generation, true) == veyra::ListPipelineResult::changed,
        "resolving the current generation applies"
    );
    require(!pipeline.is_loading_anywhere(), "a resolved append loads nothing");
    require(
        pipeline.load_state(kSource, kAppend).state == veyra::ListLoadState::not_loading,
        "a resolved append idles"
    );
    require(!pipeline.pending_request(kAppend, pending), "a resolved append clears its request");
}

void check_stale_responses_are_dropped() {
    auto pipeline = veyra::ListPipeline{};
    pipeline.request(kAppend, 100, 25);

    auto first = veyra::ListLoadRequest{};
    require(pipeline.pending_request(kAppend, first), "the first append request is pending");

    require(
        pipeline.request(kAppend, 125, 25) == veyra::ListPipelineResult::changed,
        "a moved window replaces the in-flight append"
    );

    auto second = veyra::ListLoadRequest{};
    require(pipeline.pending_request(kAppend, second), "the second append request is pending");
    require(second.generation == first.generation + 1, "replacing a request bumps its generation");

    require(
        pipeline.resolve(kAppend, first.generation, true) == veyra::ListPipelineResult::stale,
        "the superseded response is dropped"
    );
    require(
        pipeline.load_state(kSource, kAppend).state == veyra::ListLoadState::loading,
        "a stale response never clobbers the live load"
    );
    require(
        pipeline.fail(kAppend, first.generation) == veyra::ListPipelineResult::stale,
        "a superseded failure is dropped as well"
    );

    require(
        pipeline.resolve(kAppend, second.generation, true) == veyra::ListPipelineResult::changed,
        "the current generation still applies"
    );
    require(
        pipeline.resolve(kAppend, second.generation, true) == veyra::ListPipelineResult::stale,
        "replaying a consumed response is dropped"
    );
}

void check_duplicate_request_is_deduped() {
    auto pipeline = veyra::ListPipeline{};
    pipeline.request(kPrepend, 0, 50);

    auto pending = veyra::ListLoadRequest{};
    require(pipeline.pending_request(kPrepend, pending), "the prepend request is pending");

    require(
        pipeline.request(kPrepend, 0, 50) == veyra::ListPipelineResult::handled,
        "repeating the in-flight range reports no change"
    );

    auto repeated = veyra::ListLoadRequest{};
    require(pipeline.pending_request(kPrepend, repeated), "the request survives dedup");
    require(repeated.generation == pending.generation, "dedup keeps the live generation stable");
}

void check_complete_flags_gate_requests() {
    auto pipeline = veyra::ListPipeline{};
    pipeline.request(kAppend, 100, 25);

    require(!pipeline.set_complete(kAppend, true), "an in-flight direction cannot complete yet");

    auto pending = veyra::ListLoadRequest{};
    require(pipeline.pending_request(kAppend, pending), "the append request is pending");
    pipeline.resolve(kAppend, pending.generation, true);

    require(pipeline.set_complete(kAppend, true), "a settled append can report end of data");
    require(pipeline.load_state(kSource, kAppend).complete, "the append layer reports completion");
    require(
        pipeline.request(kAppend, 125, 25) == veyra::ListPipelineResult::ignored,
        "no fetch fires past a completed end"
    );
    require(!pipeline.is_loading_anywhere(), "a gated request loads nothing");

    require(
        pipeline.refresh() == veyra::ListPipelineResult::changed,
        "refresh starts a new snapshot"
    );
    require(
        !pipeline.load_state(kSource, kAppend).complete,
        "refresh clears completion for the new snapshot"
    );
    require(
        pipeline.request(kAppend, 0, 25) == veyra::ListPipelineResult::changed,
        "appending works again after refresh"
    );
}

void check_fail_and_retry() {
    auto pipeline = veyra::ListPipeline{};
    pipeline.request(kRefresh, 0, 50);

    auto pending = veyra::ListLoadRequest{};
    require(pipeline.pending_request(kRefresh, pending), "the refresh request is pending");

    require(
        pipeline.fail(kRefresh, pending.generation) == veyra::ListPipelineResult::changed,
        "failing the live request reports a change"
    );
    require(pipeline.is_error(kRefresh), "a failed refresh reports its error");
    require(!pipeline.is_loading_anywhere(), "a failed refresh loads nothing");

    require(
        pipeline.retry(kAppend) == veyra::ListPipelineResult::ignored,
        "retrying a healthy direction is ignored"
    );
    require(
        pipeline.retry(kRefresh) == veyra::ListPipelineResult::changed,
        "retrying the failed refresh reloads"
    );
    require(pipeline.is_loading_anywhere(), "a retried refresh loads again");
    require(!pipeline.is_error(kRefresh), "retry clears the visible error");

    auto retried = veyra::ListLoadRequest{};
    require(pipeline.pending_request(kRefresh, retried), "the retry rearms the request");
    require(
        retried.first_index == 0 && retried.count == 50 &&
            retried.generation == pending.generation + 1,
        "retry keeps the failed range with a fresh generation"
    );
    require(
        pipeline.resolve(kRefresh, pending.generation, true) == veyra::ListPipelineResult::stale,
        "the failed attempt stays stale after retry"
    );
}

void check_resolve_failure_marks_error() {
    auto pipeline = veyra::ListPipeline{};
    pipeline.request(kPrepend, 0, 20);

    auto pending = veyra::ListLoadRequest{};
    require(pipeline.pending_request(kPrepend, pending), "the prepend request is pending");

    require(
        pipeline.resolve(kPrepend, pending.generation, false) == veyra::ListPipelineResult::changed,
        "an unsuccessful response reports a change"
    );
    require(pipeline.is_error(kPrepend), "an unsuccessful response marks the direction");
}

void check_refresh_invalidates_everything() {
    auto pipeline = veyra::ListPipeline{};
    pipeline.request(kRefresh, 0, 50);
    pipeline.request(kAppend, 100, 25);

    auto refresh = veyra::ListLoadRequest{};
    auto append = veyra::ListLoadRequest{};
    require(pipeline.pending_request(kRefresh, refresh), "refresh is pending before refresh");
    require(pipeline.pending_request(kAppend, append), "append is pending before refresh");
    pipeline.set_placeholders(10, 40);

    require(
        pipeline.refresh() == veyra::ListPipelineResult::changed,
        "refresh resets the pipeline"
    );

    require(
        pipeline.load_state(kSource, kRefresh).state == veyra::ListLoadState::loading,
        "refresh reloads the refresh direction"
    );
    require(
        pipeline.load_state(kSource, kAppend).state == veyra::ListLoadState::not_loading,
        "refresh parks the append direction"
    );

    auto dropped = veyra::ListLoadRequest{};
    require(!pipeline.pending_request(kRefresh, dropped), "refresh drops the old refresh fetch");
    require(!pipeline.pending_request(kAppend, dropped), "refresh drops the old append fetch");
    require(
        pipeline.resolve(kRefresh, refresh.generation, true) == veyra::ListPipelineResult::stale,
        "the old refresh response is stale after refresh"
    );
    require(
        pipeline.resolve(kAppend, append.generation, true) == veyra::ListPipelineResult::stale,
        "the old append response is stale after refresh"
    );
    require(!pipeline.should_show_placeholders(), "refresh clears stale placeholder counts");

    auto pending = veyra::ListLoadRequest{};
    pipeline.request(kRefresh, 0, 50);
    require(pipeline.pending_request(kRefresh, pending), "a new refresh request is pending");
    require(pending.generation > refresh.generation, "post-refresh requests use fresh generations");
}

void check_placeholders_and_end_reached() {
    auto pipeline = veyra::ListPipeline{};

    require(pipeline.set_placeholders(50, 200), "recording unloaded ranges reports a change");
    require(!pipeline.set_placeholders(50, 200), "repeating placeholder counts reports no change");
    require(pipeline.should_show_placeholders(), "unloaded ranges show placeholders");
    require(pipeline.placeholders_before() == 50, "the head placeholder count is kept");
    require(pipeline.placeholders_after() == 200, "the tail placeholder count is kept");

    const auto far = pipeline.end_reached(40, 59, 100, 20);
    require(!far.prepend && !far.append, "a centered window reaches neither edge");

    const auto head = pipeline.end_reached(5, 24, 100, 20);
    require(head.prepend && !head.append, "a window near the head reaches the prepend edge");

    const auto tail = pipeline.end_reached(60, 85, 100, 20);
    require(!tail.prepend && tail.append, "a window near the tail reaches the append edge");

    const auto exact = pipeline.end_reached(0, 99, 100, 0);
    require(exact.prepend && exact.append, "a zero threshold still fires exactly at the edges");

    require(!pipeline.end_reached(0, 100, 100, 20).append, "a window past the end reaches nothing");
    require(!pipeline.end_reached(10, 5, 100, 20).prepend, "an inverted window reaches nothing");
    require(!pipeline.end_reached(0, 0, 0, 20).prepend, "an empty list reaches neither edge");

    require(pipeline.set_placeholders(0, 0), "clearing placeholders reports a change");
    require(!pipeline.should_show_placeholders(), "a fully loaded list shows no placeholders");
    require(!pipeline.end_reached(0, 19, 100, 20).prepend, "no head range means no prepend fetch");
    require(!pipeline.end_reached(80, 99, 100, 20).append, "no tail range means no append fetch");
}

void check_completed_edges_suppress_end_reached() {
    auto pipeline = veyra::ListPipeline{};
    pipeline.set_placeholders(50, 200);

    require(pipeline.set_complete(kAppend, true), "the tail can report end of data");
    require(
        !pipeline.end_reached(80, 99, 100, 20).append,
        "a completed tail never asks for another append"
    );
    require(
        pipeline.end_reached(0, 19, 100, 20).prepend,
        "completing the tail leaves prepend fetching alone"
    );
}

void check_search_debounce_and_invalidate() {
    veyra::ListPipelineConfig config{};
    config.search_debounce_ms = 150;
    auto pipeline = veyra::ListPipeline{config};

    require(!pipeline.search_ready(0), "a fresh query is still debouncing");
    require(!pipeline.search_ready(149), "a fresh query waits out the debounce window");

    static constexpr char kQuery[] = "chat";
    require(
        pipeline.update_search(kQuery, 1000) == veyra::ListPipelineResult::changed,
        "typing a query reports a change"
    );
    require(pipeline.search_generation() == 1, "typing a query bumps the search generation");
    require(!pipeline.search_ready(1149), "a new keystroke restarts the debounce window");
    require(pipeline.search_ready(1150), "the debounced query becomes due after its window");

    static constexpr char kSame[] = "chat";
    require(
        pipeline.update_search(kSame, 1200) == veyra::ListPipelineResult::handled,
        "repeating the query reports no change"
    );
    require(pipeline.search_generation() == 1, "repeating the query keeps its generation");
    require(!pipeline.search_ready(1349), "repeating the query restarts its debounce window");
    require(pipeline.search_ready(1350), "the repeated query becomes due after its window");

    static constexpr char kNext[] = "chat!";
    require(
        pipeline.update_search(kNext, 1400) == veyra::ListPipelineResult::changed,
        "extending the query reports a change"
    );
    require(pipeline.search_generation() == 2, "extending the query bumps its generation");

    const std::uint64_t invalidated = pipeline.invalidate_search();
    require(invalidated == 3, "explicit invalidation bumps the search generation");
    require(pipeline.search_generation() == 3, "the bumped generation is observable");

    require(pipeline.set_search_debounce_ms(0), "the debounce window can shrink to zero");
    require(pipeline.search_ready(1400), "a zero window is due immediately");
    require(!pipeline.set_search_debounce_ms(0), "repeating the debounce window reports no change");
}

void check_mediator_layer() {
    veyra::ListPipelineConfig config{};
    config.has_mediator = true;
    auto pipeline = veyra::ListPipeline{config};

    require(pipeline.has_mediator(), "the mediator layer is present when configured");
    require(pipeline.combined_states().has_mediator, "combined states report the mediator layer");

    require(
        pipeline.request(kAppend, 200, 50, kMediator) == veyra::ListPipelineResult::changed,
        "the mediator append loads independently"
    );
    require(pipeline.is_loading_anywhere(), "a mediator load counts as loading");
    require(
        pipeline.load_state(kSource, kAppend).state == veyra::ListLoadState::not_loading,
        "a mediator load leaves the source layer alone"
    );

    auto pending = veyra::ListLoadRequest{};
    require(
        pipeline.pending_request(kAppend, pending, kMediator),
        "the mediator request is pending"
    );
    require(
        !pipeline.pending_request(kAppend, pending, kSource),
        "the source layer has no request of its own"
    );

    require(
        pipeline.fail(kAppend, pending.generation, kMediator) == veyra::ListPipelineResult::changed,
        "failing the mediator request reports a change"
    );
    require(pipeline.is_error(kAppend), "a mediator error surfaces on its direction");

    require(
        pipeline.retry(kAppend, kMediator) == veyra::ListPipelineResult::changed,
        "the mediator error retries on its own layer"
    );
    require(
        pipeline.resolve(kAppend, pending.generation + 1, true, kMediator) ==
            veyra::ListPipelineResult::changed,
        "the retried mediator request resolves on its own layer"
    );
    require(!pipeline.is_error(kAppend), "resolving the mediator clears the direction error");

    require(pipeline.set_mediator_present(false), "the mediator layer can detach");
    require(!pipeline.combined_states().has_mediator, "combined states report the detachment");
}

void check_invalid_arguments() {
    auto pipeline = veyra::ListPipeline{};

    require(
        pipeline.request(kAppend, 10, 0) == veyra::ListPipelineResult::ignored,
        "an empty range never fetches"
    );
    require(
        pipeline.request(kAppend, std::numeric_limits<std::size_t>::max(), 1) ==
            veyra::ListPipelineResult::ignored,
        "an overflowing range never fetches"
    );
    require(
        pipeline.resolve(kAppend, 1, true) == veyra::ListPipelineResult::stale,
        "resolving without a request is stale"
    );
    require(
        pipeline.fail(kAppend, 1) == veyra::ListPipelineResult::stale,
        "failing without a request is stale"
    );
    require(
        pipeline.retry(kAppend) == veyra::ListPipelineResult::ignored,
        "retrying without an error is ignored"
    );
    require(!pipeline.set_complete(kAppend, false), "clearing an unset flag reports no change");

    const auto bad_direction = static_cast<veyra::ListLoadDirection>(9);
    require(
        pipeline.request(bad_direction, 0, 10) == veyra::ListPipelineResult::ignored,
        "an unknown direction never fetches"
    );
    require(!pipeline.is_error(bad_direction), "an unknown direction never errors");
}

}

int main() {
    try {
        check_initial_state();
        check_request_and_resolve();
        check_stale_responses_are_dropped();
        check_duplicate_request_is_deduped();
        check_complete_flags_gate_requests();
        check_fail_and_retry();
        check_resolve_failure_marks_error();
        check_refresh_invalidates_everything();
        check_placeholders_and_end_reached();
        check_completed_edges_suppress_end_reached();
        check_search_debounce_and_invalidate();
        check_mediator_layer();
        check_invalid_arguments();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
