#include "veyra/render/retained_scroll.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

constexpr auto width = std::size_t{10};
constexpr auto height = std::size_t{8};

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

veyra::DisplayColor row_color(std::size_t row) {
    return veyra::DisplayColor{
        .red = static_cast<std::uint8_t>(10 + row * 20),
        .green = 30,
        .blue = 50,
    };
}

struct FrameCommands {
    std::array<veyra::DisplayCommand, height> draws{};
};

FrameCommands scrolled_rows(int offset_y) {
    auto frame = FrameCommands{};

    for (auto row = std::size_t{}; row < height; ++row) {
        const auto source = static_cast<int>(row) - offset_y;
        const auto wrapped = (source % 8 + 8) % 8;
        frame.draws[row] = veyra::FillRect{
            .bounds =
                {
                    .y = static_cast<float>(row),
                    .width = static_cast<float>(width),
                    .height = 1.0F,
                },
            .color = row_color(static_cast<std::size_t>(wrapped)),
        };
    }

    return frame;
}

void paint_oracle(
    std::span<const veyra::DisplayCommand> commands,
    std::span<veyra::SoftwarePixel> pixels,
    std::size_t stride,
    std::size_t oracle_width,
    std::size_t oracle_height
) {
    std::array<veyra::SoftwareClipRect, 4> clips{};
    const auto workspace = veyra::SoftwareRenderWorkspace{.clips = clips};
    const auto surface = veyra::SoftwareSurface{
        .width = oracle_width,
        .height = oracle_height,
        .stride = stride,
        .pixels = pixels,
    };

    require(
        veyra::render_software(commands, surface, workspace) ==
            veyra::SoftwareRenderResult::complete,
        "the oracle renders"
    );
}

void require_same_pixels(
    std::span<const veyra::SoftwarePixel> got,
    std::span<const veyra::SoftwarePixel> want,
    std::string_view message
) {
    require(got.size() == want.size(), "oracle and retained agree on size");

    for (auto index = std::size_t{}; index < got.size(); ++index) {
        const auto& left = got[index];
        const auto& right = want[index];
        require(
            left.red == right.red && left.green == right.green && left.blue == right.blue &&
                left.alpha == right.alpha,
            message
        );
    }
}

struct Harness {
    std::array<veyra::SoftwarePixel, width * height> pixels{};
    std::array<veyra::SoftwarePixel, width * height> mirror{};
    std::array<veyra::SoftwareClipRect, 4> clips{};
    veyra::RetainedScrollSurface retained{};
    veyra::RetainedScrollStats stats{};
    veyra::RetainedScrollVerifyStats verify_stats{};

    veyra::SoftwareSurface surface() {
        return veyra::SoftwareSurface{
            .width = width,
            .height = height,
            .stride = width,
            .pixels = pixels,
        };
    }

    veyra::SoftwareSurface scratch() {
        return veyra::SoftwareSurface{
            .width = width,
            .height = height,
            .stride = width,
            .pixels = mirror,
        };
    }

    veyra::SoftwareRenderWorkspace workspace() {
        return veyra::SoftwareRenderWorkspace{.clips = clips};
    }

    veyra::RetainedScrollResult
    present(std::span<const veyra::DisplayCommand> commands, veyra::RetainedScrollRequest request) {
        return veyra::render_retained_scrolled(
            commands,
            surface(),
            workspace(),
            request,
            retained,
            stats
        );
    }

    veyra::RetainedVerifyResult present_verified(
        std::span<const veyra::DisplayCommand> commands,
        veyra::RetainedScrollRequest request
    ) {
        return veyra::render_retained_scrolled_verified(
            commands,
            surface(),
            workspace(),
            request,
            retained,
            scratch(),
            stats,
            verify_stats
        );
    }

    void require_matches_oracle(std::span<const veyra::DisplayCommand> commands) {
        auto want = std::array<veyra::SoftwarePixel, width * height>{};
        paint_oracle(commands, want, width, width, height);
        require_same_pixels(pixels, want, "retained output matches a full repaint");
    }
};

void check_first_frame_paints_full() {
    auto harness = Harness{};
    const auto frame = scrolled_rows(0);

    require(
        harness.present(frame.draws, {.delta_x = 0, .delta_y = 0, .content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "an unpainted layer presents"
    );
    require(!harness.stats.reused, "the first frame cannot reuse");
    require(harness.stats.strip_count == 0, "the first frame reports no strips");
    harness.require_matches_oracle(frame.draws);
}

void check_scroll_down_reuses_blit() {
    auto harness = Harness{};
    const auto first = scrolled_rows(0);
    const auto second = scrolled_rows(3);

    require(
        harness.present(first.draws, {.content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "the first frame presents"
    );
    require(
        harness.present(second.draws, {.delta_y = 3, .content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "a downward scroll presents"
    );
    require(harness.stats.reused, "a pure scroll reuses the blit");
    require(harness.stats.strip_count == 1, "a vertical scroll repaints one strip");
    harness.require_matches_oracle(second.draws);
}

void check_scroll_up_reuses_blit() {
    auto harness = Harness{};
    const auto first = scrolled_rows(0);
    const auto second = scrolled_rows(-2);

    require(
        harness.present(first.draws, {.content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "the first frame presents"
    );
    require(
        harness.present(second.draws, {.delta_y = -2, .content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "an upward scroll presents"
    );
    require(harness.stats.reused, "an upward scroll reuses the blit");
    harness.require_matches_oracle(second.draws);
}

void check_horizontal_scroll_reuses_blit() {
    auto harness = Harness{};
    auto first = std::array<veyra::DisplayCommand, 5>{};
    auto second = std::array<veyra::DisplayCommand, 5>{};

    for (auto stripe = std::size_t{}; stripe < 5; ++stripe) {
        first[stripe] = veyra::FillRect{
            .bounds =
                {
                    .x = static_cast<float>(stripe * 2),
                    .width = 2.0F,
                    .height = static_cast<float>(height),
                },
            .color = row_color(stripe),
        };
        second[stripe] = veyra::FillRect{
            .bounds =
                {
                    .x = static_cast<float>(stripe * 2),
                    .width = 2.0F,
                    .height = static_cast<float>(height),
                },
            .color = row_color((stripe + 5 - 2) % 5),
        };
    }

    require(
        harness.present(first, {.content_generation = 1}) == veyra::RetainedScrollResult::complete,
        "the first frame presents"
    );
    require(
        harness.present(second, {.delta_x = 4, .content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "a rightward scroll presents"
    );
    require(harness.stats.reused, "a horizontal scroll reuses the blit");

    auto want = std::array<veyra::SoftwarePixel, width * height>{};
    paint_oracle(second, want, width, width, height);
    require_same_pixels(harness.pixels, want, "a horizontal scroll matches a full repaint");
}

void check_diagonal_scroll_repaints_two_strips() {
    auto harness = Harness{};
    const auto first = scrolled_rows(0);
    const auto second = scrolled_rows(2);

    require(
        harness.present(first.draws, {.content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "the first frame presents"
    );
    require(
        harness.present(second.draws, {.delta_x = 2, .delta_y = 2, .content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "a diagonal scroll presents"
    );
    require(harness.stats.reused, "a diagonal scroll reuses the blit");
    require(harness.stats.strip_count == 2, "a diagonal scroll repaints two strips");
    harness.require_matches_oracle(second.draws);
}

void check_generation_change_repaints_full() {
    auto harness = Harness{};
    const auto frame = scrolled_rows(0);

    require(
        harness.present(frame.draws, {.content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "the first frame presents"
    );
    require(
        harness.present(frame.draws, {.delta_y = 2, .content_generation = 2}) ==
            veyra::RetainedScrollResult::complete,
        "changed content presents"
    );
    require(!harness.stats.reused, "changed content never reuses the blit");
    harness.require_matches_oracle(frame.draws);
}

void check_zero_delta_same_generation_is_noop() {
    auto harness = Harness{};
    const auto frame = scrolled_rows(0);

    require(
        harness.present(frame.draws, {.content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "the first frame presents"
    );
    const auto painted = harness.pixels;

    require(
        harness.present(frame.draws, {.content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "a still frame presents"
    );
    require(!harness.stats.reused, "a still frame reports no reuse");
    require(harness.stats.strip_count == 0, "a still frame reports no strips");
    require_same_pixels(harness.pixels, painted, "a still frame leaves pixels alone");
}

void check_large_jump_repaints_without_blit() {
    auto harness = Harness{};
    const auto first = scrolled_rows(0);
    const auto second = scrolled_rows(5);

    require(
        harness.present(first.draws, {.content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "the first frame presents"
    );
    require(
        harness.present(second.draws, {.delta_y = 100, .content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "an oversized scroll presents"
    );
    require(!harness.stats.reused, "an oversized scroll skips the blit");
    require(harness.stats.strip_count == 1, "an oversized scroll repaints the extent");
    harness.require_matches_oracle(second.draws);
}

void check_resize_repaints_full() {
    auto harness = Harness{};
    const auto frame = scrolled_rows(0);

    require(
        harness.present(frame.draws, {.content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "the first frame presents"
    );

    auto roomy = std::array<veyra::SoftwarePixel, width*(height + 2)>{};
    const auto grown = veyra::SoftwareSurface{
        .width = width,
        .height = height + 2,
        .stride = width,
        .pixels = roomy,
    };

    require(
        veyra::render_retained_scrolled(
            frame.draws,
            grown,
            harness.workspace(),
            {.content_generation = 1},
            harness.retained,
            harness.stats
        ) == veyra::RetainedScrollResult::complete,
        "a resized surface presents"
    );
    require(!harness.stats.reused, "a resized surface cannot reuse");

    auto want = std::vector<veyra::SoftwarePixel>(roomy.size());
    paint_oracle(frame.draws, want, width, width, height + 2);
    require_same_pixels(roomy, want, "a resized surface matches a full repaint");
}

void check_reallocated_storage_repaints_full() {
    auto harness = Harness{};
    const auto frame = scrolled_rows(0);

    require(
        harness.present(frame.draws, {.content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "the first frame presents"
    );

    auto fresh = std::array<veyra::SoftwarePixel, width * height>{};
    const auto moved = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = fresh,
    };

    require(
        veyra::render_retained_scrolled(
            frame.draws,
            moved,
            harness.workspace(),
            {.content_generation = 1},
            harness.retained,
            harness.stats
        ) == veyra::RetainedScrollResult::complete,
        "reallocated storage presents"
    );
    require(!harness.stats.reused, "reallocated storage cannot reuse");
    require_same_pixels(fresh, harness.pixels, "reallocated storage matches the old frame");
}

void check_clipped_scroll_updates_clip_only() {
    auto harness = Harness{};
    auto first = FrameCommands{};
    auto second = FrameCommands{};

    for (auto row = std::size_t{}; row < height; ++row) {
        const auto top_static = row < 2;
        const auto bottom_static = row > 5;
        first.draws[row] = veyra::FillRect{
            .bounds = {.y = static_cast<float>(row), .width = 10.0F, .height = 1.0F},
            .color = row_color(row),
        };

        const auto shifted = top_static || bottom_static
            ? row
            : static_cast<std::size_t>((static_cast<int>(row) - 2 + 8) % 8);
        second.draws[row] = veyra::FillRect{
            .bounds = {.y = static_cast<float>(row), .width = 10.0F, .height = 1.0F},
            .color = row_color(shifted),
        };
    }

    require(
        harness.present(first.draws, {.content_generation = 1}) ==
            veyra::RetainedScrollResult::complete,
        "the first frame presents"
    );
    require(
        harness.present(
            second.draws,
            {
                .delta_y = 2,
                .has_clip = true,
                .clip = {.left = 0, .top = 2, .width = 10, .height = 4},
                .content_generation = 1,
            }
        ) == veyra::RetainedScrollResult::complete,
        "a clipped scroll presents"
    );
    require(harness.stats.reused, "a clipped scroll reuses the blit");
    harness.require_matches_oracle(second.draws);
}

void check_strided_surface_scrolls() {
    constexpr auto stride = std::size_t{12};
    auto pixels = std::array<veyra::SoftwarePixel, stride * height>{};
    auto padding = std::array<veyra::SoftwarePixel, stride * height>{};
    std::array<veyra::SoftwareClipRect, 4> clips{};
    auto retained = veyra::RetainedScrollSurface{};
    auto stats = veyra::RetainedScrollStats{};
    const auto frame = scrolled_rows(0);
    const auto moved = scrolled_rows(2);
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = stride,
        .pixels = pixels,
    };
    const auto workspace = veyra::SoftwareRenderWorkspace{.clips = clips};

    require(
        veyra::render_retained_scrolled(
            frame.draws,
            surface,
            workspace,
            {.content_generation = 1},
            retained,
            stats
        ) == veyra::RetainedScrollResult::complete,
        "a strided first frame presents"
    );
    require(
        veyra::render_retained_scrolled(
            moved.draws,
            surface,
            workspace,
            {.delta_y = 2, .content_generation = 1},
            retained,
            stats
        ) == veyra::RetainedScrollResult::complete,
        "a strided scroll presents"
    );
    require(stats.reused, "a strided scroll reuses the blit");

    auto want = std::array<veyra::SoftwarePixel, stride * height>{};
    paint_oracle(moved.draws, want, stride, width, height);
    require_same_pixels(pixels, want, "a strided scroll matches a full repaint");
}

void check_verified_scroll_matches_oracle() {
    auto harness = Harness{};
    const auto first = scrolled_rows(0);
    const auto second = scrolled_rows(3);

    require(
        harness.present_verified(first.draws, {.content_generation = 1}) ==
            veyra::RetainedVerifyResult::complete,
        "a verified first frame presents"
    );
    require(harness.verify_stats.verified, "a verified first frame runs the comparison");
    require(
        harness.verify_stats.first_different == width * height,
        "a matching frame reports the full span"
    );
    harness.require_matches_oracle(first.draws);

    require(
        harness.present_verified(second.draws, {.delta_y = 3, .content_generation = 1}) ==
            veyra::RetainedVerifyResult::complete,
        "a verified scroll presents"
    );
    require(harness.stats.reused, "a verified scroll reuses the blit");
    require(harness.stats.strip_count == 1, "a verified scroll repaints one strip");
    require(harness.verify_stats.verified, "a verified scroll runs the comparison");
    require(
        harness.verify_stats.first_different == width * height,
        "a matching scroll reports the full span"
    );
    harness.require_matches_oracle(second.draws);
}

void check_verified_heals_corrupted_retained() {
    auto harness = Harness{};
    const auto first = scrolled_rows(0);
    const auto second = scrolled_rows(3);

    require(
        harness.present_verified(first.draws, {.content_generation = 1}) ==
            veyra::RetainedVerifyResult::complete,
        "the first frame presents"
    );

    constexpr auto corrupt = std::size_t{1 * width + 4};
    harness.pixels[corrupt] = veyra::SoftwarePixel{.red = 1, .green = 2, .blue = 3, .alpha = 4};

    require(
        harness.present_verified(second.draws, {.delta_y = 3, .content_generation = 1}) ==
            veyra::RetainedVerifyResult::mismatch,
        "a corrupted blit reports a mismatch"
    );
    require(harness.verify_stats.verified, "a mismatch still ran the comparison");
    require(
        harness.verify_stats.first_different == corrupt + 3 * width,
        "the mismatch points at the shifted scribble"
    );
    harness.require_matches_oracle(second.draws);

    require(
        harness.present_verified(second.draws, {.content_generation = 1}) ==
            veyra::RetainedVerifyResult::complete,
        "a healed layer verifies clean"
    );
    require(harness.verify_stats.verified, "the follow-up runs the comparison");
    harness.require_matches_oracle(second.draws);
}

void check_verified_rejects_mismatched_scratch() {
    auto harness = Harness{};
    const auto frame = scrolled_rows(0);

    require(
        harness.present_verified(frame.draws, {.content_generation = 1}) ==
            veyra::RetainedVerifyResult::complete,
        "the first frame presents"
    );
    const auto painted = harness.pixels;

    auto roomy = std::array<veyra::SoftwarePixel, (width + 1) * height>{};
    const auto bad_scratch = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width + 1,
        .pixels = roomy,
    };

    require(
        veyra::render_retained_scrolled_verified(
            frame.draws,
            harness.surface(),
            harness.workspace(),
            {.content_generation = 1},
            harness.retained,
            bad_scratch,
            harness.stats,
            harness.verify_stats
        ) == veyra::RetainedVerifyResult::invalid_input,
        "a mismatched scratch fails closed"
    );
    require(!harness.verify_stats.verified, "a rejected frame never compares");
    require(
        !harness.stats.reused && harness.stats.strip_count == 0,
        "a rejected frame reports no work"
    );
    require_same_pixels(harness.pixels, painted, "a rejected frame leaves pixels alone");
}

void check_verified_ignores_stride_padding() {
    constexpr auto stride = std::size_t{12};
    auto pixels = std::array<veyra::SoftwarePixel, stride * height>{};
    auto mirror = std::array<veyra::SoftwarePixel, stride * height>{};

    for (auto& pixel : mirror) {
        pixel = veyra::SoftwarePixel{.red = 9, .green = 9, .blue = 9, .alpha = 9};
    }

    std::array<veyra::SoftwareClipRect, 4> clips{};
    auto retained = veyra::RetainedScrollSurface{};
    auto stats = veyra::RetainedScrollStats{};
    auto verify_stats = veyra::RetainedScrollVerifyStats{};
    const auto frame = scrolled_rows(0);
    const auto moved = scrolled_rows(2);
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = stride,
        .pixels = pixels,
    };
    const auto scratch = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = stride,
        .pixels = mirror,
    };
    const auto workspace = veyra::SoftwareRenderWorkspace{.clips = clips};

    require(
        veyra::render_retained_scrolled_verified(
            frame.draws,
            surface,
            workspace,
            {.content_generation = 1},
            retained,
            scratch,
            stats,
            verify_stats
        ) == veyra::RetainedVerifyResult::complete,
        "a strided first frame verifies"
    );
    require(
        veyra::render_retained_scrolled_verified(
            moved.draws,
            surface,
            workspace,
            {.delta_y = 2, .content_generation = 1},
            retained,
            scratch,
            stats,
            verify_stats
        ) == veyra::RetainedVerifyResult::complete,
        "stride padding never trips the comparison"
    );
    require(verify_stats.verified, "a strided scroll runs the comparison");

    auto want = std::array<veyra::SoftwarePixel, stride * height>{};
    paint_oracle(moved.draws, want, stride, width, height);
    require_same_pixels(pixels, want, "a strided scroll matches a full repaint");
}

void check_verified_invalid_surface_fails() {
    auto harness = Harness{};
    const auto frame = scrolled_rows(0);
    const auto broken = veyra::SoftwareSurface{
        .width = 0,
        .height = height,
        .stride = width,
        .pixels = harness.pixels,
    };

    require(
        veyra::render_retained_scrolled_verified(
            frame.draws,
            broken,
            harness.workspace(),
            {.content_generation = 1},
            harness.retained,
            harness.scratch(),
            harness.stats,
            harness.verify_stats
        ) == veyra::RetainedVerifyResult::invalid_surface,
        "an empty surface fails closed"
    );
    require(!harness.verify_stats.verified, "a failed surface never compares");
}

void check_invalid_surface_fails() {
    auto harness = Harness{};
    const auto frame = scrolled_rows(0);
    const auto broken = veyra::SoftwareSurface{
        .width = 0,
        .height = height,
        .stride = width,
        .pixels = harness.pixels,
    };

    require(
        veyra::render_retained_scrolled(
            frame.draws,
            broken,
            harness.workspace(),
            {.content_generation = 1},
            harness.retained,
            harness.stats
        ) == veyra::RetainedScrollResult::invalid_surface,
        "an empty surface fails closed"
    );
}

}

int main() {
    try {
        check_first_frame_paints_full();
        check_scroll_down_reuses_blit();
        check_scroll_up_reuses_blit();
        check_horizontal_scroll_reuses_blit();
        check_diagonal_scroll_repaints_two_strips();
        check_generation_change_repaints_full();
        check_zero_delta_same_generation_is_noop();
        check_large_jump_repaints_without_blit();
        check_resize_repaints_full();
        check_reallocated_storage_repaints_full();
        check_clipped_scroll_updates_clip_only();
        check_strided_surface_scrolls();
        check_invalid_surface_fails();
        check_verified_scroll_matches_oracle();
        check_verified_heals_corrupted_retained();
        check_verified_rejects_mismatched_scratch();
        check_verified_ignores_stride_padding();
        check_verified_invalid_surface_fails();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
