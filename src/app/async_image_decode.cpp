#include "veyra/app/async_image_decode.hpp"

#include <chrono>

namespace veyra::app {

AsyncImageDecoder::~AsyncImageDecoder() {
    stop();
}

void AsyncImageDecoder::stop() noexcept {
    {
        std::lock_guard lock(mutex_);
        if (!thread_.joinable()) {
            return;
        }
        stopping_ = true;
    }
    work_.notify_all();
    thread_.join();

    {
        std::lock_guard lock(mutex_);
        stopping_ = false;
        jobs_ = {};
    }
}

bool AsyncImageDecoder::start_locked() noexcept {
    if (thread_.joinable()) {
        return true;
    }
    try {
        thread_ = std::thread(&AsyncImageDecoder::thread_main, this);
    } catch (...) {
        return false;
    }
    return true;
}

bool AsyncImageDecoder::submit(
    WindowId window,
    std::uint64_t request,
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    DecodeFunction decode
) noexcept {
    if (window == kInvalidWindowId || request == 0 || bytes.empty() || frames.empty() ||
        delays_ms.empty() || decode == nullptr) {
        return false;
    }

    std::lock_guard lock(mutex_);
    if (!start_locked()) {
        return false;
    }

    for (auto& job : jobs_) {
        if (job.active) {
            continue;
        }
        job = {
            .window = window,
            .request = request,
            .bytes = bytes,
            .budget = budget,
            .frames = frames,
            .delays_ms = delays_ms,
            .decode = decode,
            .result = ImageDecodeResult::invalid_input,
            .decoded_frames = 0,
            .active = true,
            .running = false,
            .done = false,
            .cancelled = false,
        };
        work_.notify_one();
        return true;
    }
    return false;
}

bool AsyncImageDecoder::poll(AsyncDecodeResult& out) noexcept {
    std::lock_guard lock(mutex_);
    return take_done_locked(out);
}

bool AsyncImageDecoder::wait(AsyncDecodeResult& out, unsigned timeout_ms) noexcept {
    std::unique_lock lock(mutex_);
    const auto ready = done_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
        for (const auto& job : jobs_) {
            if (job.active && job.done) {
                return true;
            }
        }
        return false;
    });
    return ready && take_done_locked(out);
}

void AsyncImageDecoder::cancel_window(WindowId window) noexcept {
    std::lock_guard lock(mutex_);
    for (auto& job : jobs_) {
        if (!job.active || job.window != window || job.done) {
            continue;
        }
        job.cancelled = true;
        if (!job.running) {
            job.result = ImageDecodeResult::invalid_input;
            job.decoded_frames = 0;
            job.done = true;
        }
    }
    done_.notify_all();
}

bool AsyncImageDecoder::take_done_locked(AsyncDecodeResult& out) noexcept {
    for (auto& job : jobs_) {
        if (!job.active || !job.done) {
            continue;
        }
        out = {
            .window = job.window,
            .request = job.request,
            .result = job.result,
            .decoded_frames = job.decoded_frames,
            .cancelled = job.cancelled,
        };
        job = {};
        return true;
    }
    return false;
}

void AsyncImageDecoder::thread_main() noexcept {
    for (;;) {
        Job* job = nullptr;
        {
            std::unique_lock lock(mutex_);
            work_.wait(lock, [&] {
                if (stopping_) {
                    return true;
                }
                for (const auto& entry : jobs_) {
                    if (entry.active && !entry.running && !entry.done) {
                        return true;
                    }
                }
                return false;
            });

            if (stopping_) {
                return;
            }

            for (auto& entry : jobs_) {
                if (entry.active && !entry.running && !entry.done) {
                    entry.running = true;
                    job = &entry;
                    break;
                }
            }
        }

        if (job != nullptr) {
            const auto result = job->cancelled ? ImageDecodeResult::invalid_input
                                               : job->decode(
                                                     job->bytes,
                                                     job->budget,
                                                     job->frames,
                                                     job->delays_ms,
                                                     job->decoded_frames
                                                 );

            {
                std::lock_guard lock(mutex_);
                job->result = result;
                job->done = true;
                job->running = false;
            }
            done_.notify_all();
        }
    }
}

}
