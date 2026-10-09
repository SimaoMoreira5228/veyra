#include "veyra/layout/container_queries.hpp"

#include <array>
#include <cmath>
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

veyra::ContainerQuerySet make_set() {
    veyra::ContainerQuerySet set;

    const std::array<float, 2> thresholds{400.0F, 800.0F};
    const bool configured = set.configure(thresholds);
    require(configured, "two-threshold setup is accepted");

    return set;
}

void drive_hold(veyra::ContainerQuerySet& set, std::uint64_t id, float low, float high) {
    auto update = set.update(id, low);
    require(update.accepted, "hold setup parks the container");

    update = set.update(id, high);
    require(update.changed, "hold setup crosses cleanly");

    update = set.update(id, low);
    require(update.changed, "hold setup returns cleanly");

    update = set.update(id, high);
    require(update.changed, "hold setup crosses a second time");

    update = set.update(id, low);
    require(update.oscillating, "repeated alternation trips the guard");
}

void check_attach_detach() {
    auto set = make_set();

    require(set.attach(1), "first attach is accepted");
    require(set.attach(2), "second attach is accepted");
    require(set.container_count() == 2, "count tracks live containers");

    require(!set.attach(1), "duplicate attach is rejected");
    require(set.container_count() == 2, "duplicate attach leaves the count untouched");

    require(!set.attach(0), "zero id is rejected");
    require(set.container_count() == 2, "zero id leaves the count untouched");

    require(set.detach(1), "detach of a live container is accepted");
    require(set.container_count() == 1, "detach drops the count");

    require(!set.detach(1), "second detach of the same id is rejected");
    require(!set.detach(99), "detach of an unknown id is rejected");

    auto variant = set.variant_of(2);
    require(variant.known, "the surviving container stays known after a detach");
    require(set.attach(1), "a detached id can be re-attached");

    variant = set.variant_of(1);
    require(variant.known, "re-attached container is known");
    require(variant.variant == 0, "re-attached container restarts in band zero");
    require(!variant.holding, "re-attached container restarts with the guard disengaged");
}

void check_overflow() {
    veyra::ContainerQuerySet set;

    for (std::uint64_t id = 1; id <= veyra::ContainerQuerySet::kMaxContainers; ++id) {
        const bool attached = set.attach(id);
        require(attached, "attach within capacity is accepted");
    }

    require(
        set.container_count() == veyra::ContainerQuerySet::kMaxContainers,
        "a full set reports its bound"
    );

    require(!set.attach(1000), "attach past capacity is rejected");

    require(
        set.container_count() == veyra::ContainerQuerySet::kMaxContainers,
        "rejected attach leaves the count untouched"
    );

    auto update = set.update(1, 500.0F);
    require(update.accepted, "live containers still resolve after a rejected attach");

    auto variant = set.variant_of(1000);
    require(!variant.known, "the rejected id never became known");

    require(set.detach(1), "detach frees a slot in a full set");
    require(set.attach(1000), "attach succeeds once a slot is free");

    variant = set.variant_of(1000);
    require(variant.known, "the retried id is known after a slot frees up");
}

void check_shared_thresholds() {
    auto set = make_set();

    require(set.attach(11), "first container attaches");
    require(set.attach(22), "second container attaches");

    auto first = set.update(11, 500.0F);
    auto second = set.update(22, 500.0F);
    require(first.variant == 1, "first container resolves against the shared table");
    require(second.variant == 1, "second container resolves against the same table");

    first = set.update(11, 900.0F);
    require(first.variant == 2, "first container crosses the shared top threshold");
    require(first.changed, "crossing reports a change");

    second = set.update(22, 100.0F);
    require(second.variant == 0, "second container resolves its own band independently");

    const std::array<float, 1> narrower{600.0F};
    require(set.configure(narrower), "reconfigure with a narrower table is accepted");
    require(set.band_count() == 2, "band count follows the shared table");
    require(set.threshold_count() == 1, "threshold count follows the shared table");

    auto variant = set.variant_of(11);
    require(variant.variant == 0, "reconfigure resets every container to band zero");
    require(!variant.holding, "reconfigure disengages every container guard");

    variant = set.variant_of(22);
    require(variant.variant == 0, "the second container resets too");

    const std::array<float, 2> unsorted{600.0F, 300.0F};
    require(!set.configure(unsorted), "out-of-order reconfigure is rejected");
    require(set.threshold_count() == 1, "rejected reconfigure keeps the shared table");
    require(set.threshold_at(0) == 600.0F, "rejected reconfigure keeps the first threshold");

    require(std::isnan(set.threshold_at(1)), "threshold query past the end returns NaN");

    auto late = set.variant_of(22);
    require(late.variant == 0, "rejected reconfigure leaves container state untouched");
}

void check_one_sided_thrash_isolation() {
    auto set = make_set();

    require(set.attach(1), "thrashing container attaches");
    require(set.attach(2), "steady container attaches");

    auto steady = set.update(2, 500.0F);
    require(steady.variant == 1, "steady container parks in the middle band");

    drive_hold(set, 1, 300.0F, 500.0F);

    auto thrasher = set.variant_of(1);
    require(thrasher.known, "thrashing container is still known");
    require(thrasher.holding, "thrashing container holds its guard");

    auto calm = set.variant_of(2);
    require(calm.known, "steady container is still known");
    require(calm.variant == 1, "steady container keeps its variant while the other holds");
    require(!calm.holding, "steady container never trips its guard");

    steady = set.update(2, 510.0F);
    require(steady.accepted, "steady container still accepts widths during the hold");
    require(!steady.changed, "steady container reports no change in its band");
    require(!steady.oscillating, "steady container reports no oscillation");

    thrasher = set.variant_of(1);
    require(thrasher.holding, "feeding the steady container never clears the thrasher hold");

    auto held = set.update(1, 500.0F);
    require(held.accepted, "held container still accepts widths");
    require(!held.changed, "held container reports no change");

    held = set.update(1, 500.0F);
    require(!held.oscillating, "two steady updates release the thrasher guard");

    calm = set.variant_of(2);
    require(calm.variant == 1, "releasing the thrasher leaves the steady variant alone");
    require(!calm.holding, "releasing the thrasher leaves the steady guard disengaged");
}

void check_unknown_id_policy() {
    auto set = make_set();

    require(set.attach(7), "one container attaches");

    auto update = set.update(404, 500.0F);
    require(!update.accepted, "update on an unknown id is rejected");

    require(set.container_count() == 1, "rejected update never attaches implicitly");

    auto variant = set.variant_of(404);
    require(!variant.known, "unknown id reports not known");

    update = set.update(7, 500.0F);
    require(update.accepted, "the live container is unaffected by unknown-id traffic");
    require(update.variant == 1, "the live container still resolves its band");

    update = set.update(404, 500.0F);
    require(!update.accepted, "unknown id stays rejected on repeat");
    require(set.container_count() == 1, "repeats never grow the set either");
}

void check_invalid_widths() {
    auto set = make_set();

    require(set.attach(1), "first container attaches");
    require(set.attach(2), "second container attaches");

    auto update = set.update(1, 500.0F);
    require(update.variant == 1, "setup parks the first container in the middle band");

    update = set.update(2, 100.0F);
    require(update.variant == 0, "setup parks the second container in band zero");

    const float quiet_nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    update = set.update(1, quiet_nan);
    require(!update.accepted, "NaN width is rejected");

    update = set.update(1, infinity);
    require(!update.accepted, "infinite width is rejected");

    update = set.update(1, -1.0F);
    require(!update.accepted, "negative width is rejected");

    auto variant = set.variant_of(1);
    require(variant.variant == 1, "rejected widths leave the variant untouched");
    require(!variant.holding, "rejected widths leave the guard untouched");

    variant = set.variant_of(2);
    require(variant.variant == 0, "rejected widths on one container never touch another");

    update = set.update(1, 500.0F);
    require(update.accepted, "valid width still resolves after rejections");
    require(!update.changed, "rejections never moved the variant");
    require(!update.oscillating, "rejections never touched the guard");
}

void check_detach_preserves_neighbors() {
    auto set = make_set();

    require(set.attach(1), "first container attaches");
    require(set.attach(2), "second container attaches");
    require(set.attach(3), "third container attaches");

    drive_hold(set, 2, 300.0F, 500.0F);
    set.update(3, 900.0F);

    require(set.detach(1), "detaching one container is accepted");

    auto variant = set.variant_of(2);
    require(variant.known, "the holding container survives a neighbor detach");
    require(variant.holding, "the holding container keeps its guard across a neighbor detach");

    variant = set.variant_of(3);
    require(variant.known, "the moved container survives a neighbor detach");
    require(variant.variant == 2, "the moved container keeps its variant");

    require(set.container_count() == 2, "detach drops the count with neighbors intact");
}

}

int main() {
    try {
        check_attach_detach();
        check_overflow();
        check_shared_thresholds();
        check_one_sided_thrash_isolation();
        check_unknown_id_policy();
        check_invalid_widths();
        check_detach_preserves_neighbors();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
