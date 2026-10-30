#ifndef VEYRA_APP_ASYNC_IMAGE_DECODE_HPP
#define VEYRA_APP_ASYNC_IMAGE_DECODE_HPP

#include "veyra/media/codec/image_codecs.hpp"
#include "veyra/platform/core/window.hpp"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <thread>

namespace veyra::app {

using DecodeFunction = ImageDecodeResult (*)(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded_frames
) noexcept;

struct AsyncDecodeResult {
    WindowId window{};
    std::uint64_t request{};
    ImageDecodeResult result{ImageDecodeResult::invalid_input};
    std::uint32_t decoded_frames{};
    bool cancelled{};
};

class AsyncImageDecoder {
  public:
    static constexpr std::size_t kCapacity = 8;

    AsyncImageDecoder() noexcept = default;
    ~AsyncImageDecoder();

    AsyncImageDecoder(const AsyncImageDecoder&) = delete;
    AsyncImageDecoder& operator=(const AsyncImageDecoder&) = delete;
    AsyncImageDecoder(AsyncImageDecoder&&) = delete;
    AsyncImageDecoder& operator=(AsyncImageDecoder&&) = delete;

    // NOTE: the encoded bytes, frame targets, and delay buffer must outlive the
    // request until its result is polled or the decoder is destroyed.
    bool submit(
        WindowId window,
        std::uint64_t request,
        std::span<const std::uint8_t> bytes,
        const ImageDecodeBudget& budget,
        std::span<ImageFrameTarget> frames,
        std::span<std::uint32_t> delays_ms,
        DecodeFunction decode
    ) noexcept;

    bool poll(AsyncDecodeResult& out) noexcept;
    bool wait(AsyncDecodeResult& out, unsigned timeout_ms) noexcept;
    void cancel_window(WindowId window) noexcept;
    void stop() noexcept;

  private:
    struct Job {
        WindowId window{};
        std::uint64_t request{};
        std::span<const std::uint8_t> bytes{};
        ImageDecodeBudget budget{};
        std::span<ImageFrameTarget> frames{};
        std::span<std::uint32_t> delays_ms{};
        DecodeFunction decode{};
        ImageDecodeResult result{ImageDecodeResult::invalid_input};
        std::uint32_t decoded_frames{};
        bool active{};
        bool running{};
        bool done{};
        bool cancelled{};
    };

    bool start_locked() noexcept;
    bool take_done_locked(AsyncDecodeResult& out) noexcept;
    void thread_main() noexcept;

    std::mutex mutex_;
    std::condition_variable work_;
    std::condition_variable done_;
    std::array<Job, kCapacity> jobs_{};
    bool stopping_{};
    std::thread thread_;
};

}

namespace veyra::app {
using ::veyra::app::AsyncDecodeResult;
using ::veyra::app::AsyncImageDecoder;
using ::veyra::app::DecodeFunction;
}

#endif
