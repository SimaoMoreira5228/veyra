#include "veyra/layout/image/image.hpp"

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

bool near(float left, float right) noexcept {
    return std::abs(left - right) < 0.0001F;
}

void check_load_to_ready() {
    auto image = veyra::ImageController{};
    require(image.state() == veyra::ImageLoadState::empty, "fresh controller is empty");

    const auto request = image.load(42, veyra::ImageSourceKind::local, false);
    require(request.result == veyra::ImageRequestResult::started, "first load starts from empty");
    require(request.generation != 0, "accepted load issues a nonzero generation");
    require(image.state() == veyra::ImageLoadState::loading, "load enters loading");
    require(image.source_id() == 42, "load records the caller source id");
    require(image.is_current(request.generation), "issued generation is current");
    require(!image.has_intrinsic(), "intrinsic size is unknown before arrival");

    const auto intrinsic = image.set_intrinsic_size(request.generation, 4.0F, 2.0F);
    require(intrinsic.accepted, "intrinsic arrival for the live generation is accepted");
    require(intrinsic.refit, "late intrinsic arrival requests a fit recompute");
    require(image.has_intrinsic(), "intrinsic size is known after arrival");

    require(image.complete(request.generation), "completion for the live generation lands");
    require(image.state() == veyra::ImageLoadState::ready, "completion enters ready");
    require(!image.is_current(request.generation), "settled generation is no longer current");
    require(!image.complete(request.generation), "duplicate completion is rejected");
}

void check_broken_reasons() {
    auto image = veyra::ImageController{};

    const auto decode = image.load(1, veyra::ImageSourceKind::local, false);
    require(
        image.fail(decode.generation, veyra::ImageBrokenReason::decode_failed),
        "decode failure lands"
    );
    require(image.state() == veyra::ImageLoadState::broken, "decode failure enters broken");
    require(
        image.broken_reason() == veyra::ImageBrokenReason::decode_failed,
        "decode failure reason is readable"
    );

    const auto retry = image.load(1, veyra::ImageSourceKind::local, false);
    require(
        retry.result == veyra::ImageRequestResult::restarted,
        "loading over broken restarts the generation"
    );
    require(
        image.fail(retry.generation, veyra::ImageBrokenReason::invalid_dimensions),
        "invalid dimensions land"
    );
    require(
        image.broken_reason() == veyra::ImageBrokenReason::invalid_dimensions,
        "invalid dimensions reason is readable"
    );

    const auto cancelled = image.load(2, veyra::ImageSourceKind::local, false);
    require(
        image.fail(cancelled.generation, veyra::ImageBrokenReason::replaced),
        "app-reported replace cancellation lands"
    );
    require(
        image.broken_reason() == veyra::ImageBrokenReason::replaced,
        "replace cancellation reason is readable"
    );

    const auto empty_reason = image.load(3, veyra::ImageSourceKind::local, false);
    require(
        !image.fail(empty_reason.generation, veyra::ImageBrokenReason::none),
        "empty reason is rejected"
    );
    require(image.state() == veyra::ImageLoadState::loading, "empty reason leaves state alone");
}

void check_replace_cancels_old_generation() {
    auto image = veyra::ImageController{};

    const auto first = image.load(10, veyra::ImageSourceKind::local, false);
    const auto second = image.load(20, veyra::ImageSourceKind::local, false);
    require(
        second.result == veyra::ImageRequestResult::restarted,
        "new source while loading restarts"
    );
    require(second.generation != first.generation, "replace issues a fresh generation");
    require(image.source_id() == 20, "replace tracks the new source");

    require(!image.is_current(first.generation), "replaced generation is no longer current");
    require(!image.complete(first.generation), "late completion for a replaced source is rejected");
    require(
        !image.fail(first.generation, veyra::ImageBrokenReason::decode_failed),
        "late failure for a replaced source is rejected"
    );
    const auto stale_intrinsic = image.set_intrinsic_size(first.generation, 8.0F, 8.0F);
    require(!stale_intrinsic.accepted, "late intrinsic arrival for a replaced source is rejected");
    require(
        image.state() == veyra::ImageLoadState::loading,
        "stale reports leave the new load alone"
    );

    require(image.complete(second.generation), "completion for the replacing source lands");
    require(image.state() == veyra::ImageLoadState::ready, "replacing source reaches ready");
    require(image.source_id() == 20, "ready state keeps the replacing source");
}

void check_intrinsic_validation() {
    auto image = veyra::ImageController{};
    const auto request = image.load(5, veyra::ImageSourceKind::local, false);

    const auto zero = image.set_intrinsic_size(request.generation, 0.0F, 4.0F);
    require(!zero.accepted, "zero width is rejected");
    require(!image.has_intrinsic(), "rejected arrival leaves intrinsic unknown");

    const auto negative = image.set_intrinsic_size(request.generation, 4.0F, -1.0F);
    require(!negative.accepted, "negative height is rejected");

    const auto nan =
        image.set_intrinsic_size(request.generation, std::numeric_limits<float>::quiet_NaN(), 4.0F);
    require(!nan.accepted, "non-finite width is rejected");

    const auto valid = image.set_intrinsic_size(request.generation, 4.0F, 2.0F);
    require(valid.accepted && valid.refit, "valid arrival after rejections still lands");
    require(
        image.intrinsic_size().width == 4.0F && image.intrinsic_size().height == 2.0F,
        "stored intrinsic matches the arrival"
    );
}

void check_retry_after_broken() {
    auto image = veyra::ImageController{};

    const auto first = image.load(7, veyra::ImageSourceKind::local, false);
    image.set_intrinsic_size(first.generation, 4.0F, 2.0F);
    require(
        image.fail(first.generation, veyra::ImageBrokenReason::decode_failed),
        "first attempt fails"
    );

    const auto retry = image.reload();
    require(retry.result == veyra::ImageRequestResult::restarted, "explicit reload restarts");
    require(retry.generation != first.generation, "reload fences the failed generation");
    require(image.state() == veyra::ImageLoadState::loading, "reload re-enters loading");
    require(!image.has_intrinsic(), "reload drops the failed attempt's dimensions");
    require(
        image.broken_reason() == veyra::ImageBrokenReason::none,
        "reload clears the broken reason"
    );
    require(!image.complete(first.generation), "failed generation stays fenced after reload");

    image.set_intrinsic_size(retry.generation, 4.0F, 2.0F);
    require(image.complete(retry.generation), "retry completion lands");
    require(image.state() == veyra::ImageLoadState::ready, "retry reaches ready");

    auto empty = veyra::ImageController{};
    const auto rejected = empty.reload();
    require(
        rejected.result == veyra::ImageRequestResult::rejected,
        "reload from empty is rejected"
    );
    require(rejected.generation == 0, "rejected reload issues no generation");
}

void check_source_rejection() {
    auto image = veyra::ImageController{};

    const auto zero = image.load(0, veyra::ImageSourceKind::local, false);
    require(zero.result == veyra::ImageRequestResult::rejected, "zero source id is rejected");
    require(zero.generation == 0, "rejected load issues no generation");
    require(image.state() == veyra::ImageLoadState::empty, "rejected load leaves state empty");

    const auto blocked = image.load(9, veyra::ImageSourceKind::remote, false);
    require(
        blocked.result == veyra::ImageRequestResult::remote_not_allowed,
        "remote source without permission is refused"
    );
    require(image.state() == veyra::ImageLoadState::empty, "refused remote leaves state empty");

    const auto allowed = image.load(9, veyra::ImageSourceKind::remote, true);
    require(
        allowed.result == veyra::ImageRequestResult::started,
        "remote source with explicit permission starts"
    );
    require(image.state() == veyra::ImageLoadState::loading, "allowed remote enters loading");
}

void check_clear_fences_completions() {
    auto image = veyra::ImageController{};

    const auto request = image.load(11, veyra::ImageSourceKind::local, false);
    image.clear();
    require(image.state() == veyra::ImageLoadState::empty, "clear returns to empty");
    require(image.source_id() == 0, "clear drops the source id");
    require(!image.complete(request.generation), "completion after clear is rejected");
    require(
        !image.fail(request.generation, veyra::ImageBrokenReason::decode_failed),
        "failure after clear is rejected"
    );

    const auto after = image.load(12, veyra::ImageSourceKind::local, false);
    require(after.result == veyra::ImageRequestResult::started, "load after clear starts fresh");
}

void check_fit_composition() {
    auto image = veyra::ImageController{};
    const auto request = image.load(21, veyra::ImageSourceKind::local, false);
    require(!image.has_intrinsic(), "fit must wait for intrinsic arrival");

    image.set_intrinsic_size(request.generation, 4.0F, 2.0F);
    image.complete(request.generation);

    const auto box = veyra::LayoutRect{.x = 3.0F, .y = 7.0F, .width = 8.0F, .height = 8.0F};
    auto placement = veyra::ImagePlacement{};
    require(
        veyra::fit_image(box, image.intrinsic_size(), veyra::ImageFitMode::contain, placement) ==
            veyra::ImageFitResult::complete,
        "ready intrinsic feeds fit_image directly"
    );
    require(
        near(placement.destination.x, 3.0F) && near(placement.destination.y, 9.0F) &&
            near(placement.destination.width, 8.0F) && near(placement.destination.height, 4.0F),
        "contain over controller intrinsic centers without changing aspect ratio"
    );

    require(
        veyra::fit_image(box, image.intrinsic_size(), veyra::ImageFitMode::cover, placement) ==
            veyra::ImageFitResult::complete,
        "cover over controller intrinsic completes"
    );
    require(
        near(placement.source.x, 1.0F) && near(placement.source.y, 0.0F) &&
            near(placement.source.width, 2.0F) && near(placement.source.height, 2.0F),
        "cover over controller intrinsic crops the source"
    );
}


}

int main() {
    try {
        check_load_to_ready();
        check_broken_reasons();
        check_replace_cancels_old_generation();
        check_intrinsic_validation();
        check_retry_after_broken();
        check_source_rejection();
        check_clear_fences_completions();
        check_fit_composition();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
