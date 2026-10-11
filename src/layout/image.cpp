#include "veyra/layout/image.hpp"

#include <cmath>
#include <limits>

namespace veyra {

ImageRequest
ImageController::load(std::uint64_t source_id, ImageSourceKind kind, bool allow_remote) noexcept {
    if (source_id == 0) {
        return {};
    }

    if (kind == ImageSourceKind::remote && !allow_remote) {
        return {.result = ImageRequestResult::remote_not_allowed};
    }

    const bool restart = state_ != ImageLoadState::empty;
    generation_ = next_generation(generation_);
    source_ = source_id;
    state_ = ImageLoadState::loading;
    reason_ = ImageBrokenReason::none;
    intrinsic_ = {};
    has_intrinsic_ = false;

    return {
        .result = restart ? ImageRequestResult::restarted : ImageRequestResult::started,
        .generation = generation_,
    };
}

ImageRequest ImageController::reload() noexcept {
    if (state_ == ImageLoadState::empty) {
        return {};
    }

    generation_ = next_generation(generation_);
    state_ = ImageLoadState::loading;
    reason_ = ImageBrokenReason::none;
    intrinsic_ = {};
    has_intrinsic_ = false;

    return {
        .result = ImageRequestResult::restarted,
        .generation = generation_,
    };
}

void ImageController::clear() noexcept {
    generation_ = next_generation(generation_);
    source_ = 0;
    state_ = ImageLoadState::empty;
    reason_ = ImageBrokenReason::none;
    intrinsic_ = {};
    has_intrinsic_ = false;
}

bool ImageController::complete(std::uint64_t generation) noexcept {
    if (!is_current(generation)) {
        return false;
    }

    state_ = ImageLoadState::ready;

    return true;
}

bool ImageController::fail(std::uint64_t generation, ImageBrokenReason reason) noexcept {
    if (!is_current(generation)) {
        return false;
    }

    if (reason == ImageBrokenReason::none) {
        return false;
    }

    state_ = ImageLoadState::broken;
    reason_ = reason;

    return true;
}

ImageIntrinsicUpdate
ImageController::set_intrinsic_size(std::uint64_t generation, float width, float height) noexcept {
    if (!is_current(generation)) {
        return {};
    }

    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0F || height <= 0.0F) {
        return {};
    }

    intrinsic_ = {.width = width, .height = height};
    has_intrinsic_ = true;

    return {
        .accepted = true,
        .refit = true,
    };
}

ImageLoadState ImageController::state() const noexcept {
    return state_;
}

std::uint64_t ImageController::source_id() const noexcept {
    return source_;
}

std::uint64_t ImageController::generation() const noexcept {
    return generation_;
}

bool ImageController::is_current(std::uint64_t generation) const noexcept {
    if (generation == 0) {
        return false;
    }

    if (generation != generation_) {
        return false;
    }

    return state_ == ImageLoadState::loading;
}

std::uint64_t ImageController::next_generation(std::uint64_t generation) noexcept {
    if (generation == std::numeric_limits<std::uint64_t>::max()) {
        return 1;
    }

    const auto next = generation + 1;

    return next == 0 ? 1 : next;
}

bool ImageController::has_intrinsic() const noexcept {
    return has_intrinsic_;
}

ImageSize ImageController::intrinsic_size() const noexcept {
    return intrinsic_;
}

ImageBrokenReason ImageController::broken_reason() const noexcept {
    return reason_;
}

bool ImageController::write_image_semantics(
    std::string_view label,
    SemanticNodeData& output
) const noexcept {
    if (state_ == ImageLoadState::empty) {
        return false;
    }

    if (label.empty()) {
        return false;
    }

    std::uint16_t states{};
    if (state_ == ImageLoadState::loading) {
        states = states | SemanticState::busy;
    }
    if (state_ == ImageLoadState::broken) {
        states = states | SemanticState::invalid;
    }

    output = {
        .role = SemanticRole::image,
        .label = label,
        .states = states,
    };

    return true;
}

}
