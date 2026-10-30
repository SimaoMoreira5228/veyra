#include "veyra/app/sdl3/window_host.hpp"

namespace veyra::app::sdl3 {

bool Sdl3WindowHost::begin_decode(
    WindowId window,
    std::uint64_t request,
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    DecodeFunction decode
) noexcept {
    std::lock_guard lock(state_mutex_);
    if (!windows_.is_open(window) || !decodes_.begin(window, request)) {
        return false;
    }
    if (!decoder_.submit(window, request, bytes, budget, frames, delays_ms, decode)) {
        decodes_.take(request);
        return false;
    }
    return true;
}

bool Sdl3WindowHost::poll_decode(AsyncDecodeResult& out) noexcept {
    std::lock_guard lock(state_mutex_);
    if (!decoder_.poll(out)) {
        return false;
    }
    out.cancelled = out.cancelled || decodes_.discarded(out.request);
    if (!out.cancelled) {
        decodes_.complete(out.request);
    }
    return true;
}

bool Sdl3WindowHost::wait_decode(AsyncDecodeResult& out, unsigned timeout_ms) noexcept {
    std::lock_guard lock(state_mutex_);
    if (!decoder_.wait(out, timeout_ms)) {
        return false;
    }
    out.cancelled = out.cancelled || decodes_.discarded(out.request);
    if (!out.cancelled) {
        decodes_.complete(out.request);
    }
    return true;
}

bool Sdl3WindowHost::begin_decode(WindowId window, std::uint64_t request) noexcept {
    std::lock_guard lock(state_mutex_);
    if (!windows_.is_open(window)) {
        return false;
    }
    return decodes_.begin(window, request);
}

bool Sdl3WindowHost::complete_decode(std::uint64_t request) noexcept {
    std::lock_guard lock(state_mutex_);
    return decodes_.complete(request);
}

bool Sdl3WindowHost::take_decode(std::uint64_t request) noexcept {
    std::lock_guard lock(state_mutex_);
    return decodes_.take(request);
}

bool Sdl3WindowHost::decode_discarded(std::uint64_t request) noexcept {
    std::lock_guard lock(state_mutex_);
    return decodes_.discarded(request);
}

}
