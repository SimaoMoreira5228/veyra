#include "veyra/layout/virtual_list.hpp"

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

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 0.001;
}

void check_variable_extent_queries() {
    std::array extents{10.0F, 20.0F, 0.0F, 15.0F, 5.0F};
    std::array<double, 6> fenwick{};
    veyra::VirtualListIndex index;
    require(index.reset(extents, fenwick), "valid extents initialize the index");
    require(index.size() == extents.size(), "index reports item count");
    require(near(index.total_extent(), 50.0), "index reports total extent");

    double offset = -1.0;
    require(index.offset_of(3, offset), "item offset is available");
    require(near(offset, 30.0), "offset includes preceding measured extents");
    require(!index.offset_of(extents.size() + 1, offset), "past-end offset is rejected");

    veyra::VirtualListRange range;
    require(index.visible_range(10.0, 20.0, 1, range), "visible range query succeeds");
    require(range.first_item == 0 && range.last_item == 3, "overscan includes surrounding rows");
    require(
        near(range.first_offset, 0.0) && near(range.total_extent, 50.0),
        "range reports geometry"
    );

    require(index.visible_range(30.0, 15.0, 0, range), "boundary range query succeeds");
    require(range.first_item == 3 && range.last_item == 4, "exact boundaries exclude prior rows");
    require(near(range.first_offset, 30.0), "boundary range begins at its first row");

    require(index.visible_range(50.0, 10.0, 2, range), "end-of-list query succeeds");
    require(range.first_item == 5 && range.last_item == 5, "end offset returns an empty range");
}

void check_measurement_updates_and_failures() {
    std::array extents{10.0F, 20.0F, 30.0F};
    std::array<double, 4> fenwick{};
    veyra::VirtualListIndex index;
    require(index.reset(extents, fenwick), "valid extents initialize the index");

    double delta = 0.0;
    require(index.set_extent(0, 14.0F, delta), "measured height updates the index");
    require(
        near(delta, 4.0) && near(index.total_extent(), 64.0),
        "update exposes anchor correction"
    );

    double offset = 0.0;
    require(index.offset_of(2, offset), "updated offset is available");
    require(near(offset, 34.0), "updated height changes later offsets");

    const auto previous_total = index.total_extent();
    require(
        !index.set_extent(1, std::numeric_limits<float>::infinity(), delta),
        "non-finite measurement is rejected"
    );
    require(
        near(delta, 0.0) && near(index.total_extent(), previous_total),
        "failed update is atomic"
    );

    veyra::VirtualListRange range{.first_item = 7};
    require(!index.visible_range(-1.0, 10.0, 0, range), "negative scroll is rejected");
    require(range.first_item == 7, "failed query leaves range unchanged");
}

void check_reset_rejects_invalid_storage_and_extents() {
    veyra::VirtualListIndex index;
    std::array extents{5.0F, 10.0F};
    std::array<double, 2> short_fenwick{17.0, 19.0};
    require(!index.reset(extents, short_fenwick), "short index storage is rejected");
    require(
        near(short_fenwick[0], 17.0) && near(short_fenwick[1], 19.0),
        "short storage is unchanged"
    );

    std::array<double, 3> fenwick{23.0, 29.0, 31.0};
    extents[1] = -1.0F;
    require(!index.reset(extents, fenwick), "negative extents are rejected");
    require(
        near(fenwick[0], 23.0) && near(fenwick[1], 29.0),
        "invalid extents leave storage unchanged"
    );
    require(index.size() == 0, "failed reset leaves index empty");
}

void check_ranges_match_linear_reference() {
    std::array extents{0.0F, 4.0F, 7.0F, 0.0F, 3.0F, 8.0F};
    std::array<double, 7> fenwick{};
    veyra::VirtualListIndex index;
    require(index.reset(extents, fenwick), "reference extents initialize the index");

    for (std::size_t scroll_tick = 0; scroll_tick <= 46; ++scroll_tick) {
        const auto scroll = static_cast<double>(scroll_tick) * 0.5;
        for (std::size_t viewport_tick = 0; viewport_tick <= 20; ++viewport_tick) {
            const auto viewport = static_cast<double>(viewport_tick) * 0.5;
            for (std::size_t overscan = 0; overscan <= 2; ++overscan) {
                veyra::VirtualListRange range;
                require(
                    index.visible_range(scroll, viewport, overscan, range),
                    "range query succeeds"
                );

                auto visible_first = std::size_t{0};
                auto prefix = 0.0;
                while (visible_first < extents.size()) {
                    prefix += extents[visible_first];
                    if (prefix > scroll) {
                        break;
                    }
                    ++visible_first;
                }

                auto visible_last = visible_first;
                const bool has_viewport = viewport > 0.0 && scroll < index.total_extent();
                if (has_viewport) {
                    const auto end = std::min(index.total_extent(), scroll + viewport);
                    auto ended_before = std::size_t{0};
                    prefix = 0.0;
                    for (const auto extent : extents) {
                        prefix += extent;
                        if (prefix >= end) {
                            break;
                        }
                        ++ended_before;
                    }
                    visible_last = std::min(extents.size(), ended_before + 1);
                }

                const auto expected_first = has_viewport
                    ? (visible_first > overscan ? visible_first - overscan : 0)
                    : visible_first;
                const auto expected_last = has_viewport
                    ? std::min(
                          extents.size(),
                          visible_last + std::min(overscan, extents.size() - visible_last)
                      )
                    : visible_last;
                auto expected_offset = 0.0;
                for (std::size_t item = 0; item < expected_first; ++item) {
                    expected_offset += extents[item];
                }

                if (range.first_item != expected_first || range.last_item != expected_last) {
                    throw std::runtime_error(
                        "range mismatch at scroll=" + std::to_string(scroll) + " viewport=" +
                        std::to_string(viewport) + " overscan=" + std::to_string(overscan) +
                        " actual=" + std::to_string(range.first_item) + ":" +
                        std::to_string(range.last_item) + " expected=" +
                        std::to_string(expected_first) + ":" + std::to_string(expected_last)
                    );
                }
                require(
                    near(range.first_offset, expected_offset),
                    "indexed offset matches reference"
                );
            }
        }
    }
}

}

int main() {
    try {
        check_variable_extent_queries();
        check_measurement_updates_and_failures();
        check_reset_rejects_invalid_storage_and_extents();
        check_ranges_match_linear_reference();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
