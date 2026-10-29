#include "veyra/render/present/render_worker.hpp"

#include <utility>

namespace veyra {

RenderWorker::~RenderWorker() {
    stop();
}

bool RenderWorker::start() noexcept {
    if (running_.load(std::memory_order_acquire)) {
        return true;
    }

    try {
        thread_ = std::thread(&RenderWorker::thread_main, this);
    } catch (...) {
        return false;
    }

    running_.store(true, std::memory_order_release);
    return true;
}

void RenderWorker::stop() noexcept {
    {
        std::lock_guard lock(mutex_);
        if (!thread_.joinable()) {
            return;
        }
        stopping_ = true;
    }
    wake_.notify_all();
    thread_.join();

    {
        std::lock_guard lock(mutex_);
        stopping_ = false;
        head_ = 0;
        tail_ = 0;
        count_ = 0;
        busy_ = false;
    }
    running_.store(false, std::memory_order_release);
}

bool RenderWorker::running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

bool RenderWorker::is_worker_thread() const noexcept {
    return thread_.get_id() == std::this_thread::get_id();
}

std::thread::id RenderWorker::thread_id() const noexcept {
    return thread_.get_id();
}

bool RenderWorker::submit(Task task, void* context) noexcept {
    if (task == nullptr) {
        return false;
    }

    std::lock_guard lock(mutex_);
    if (!running_.load(std::memory_order_acquire) || stopping_ || count_ == kCapacity) {
        return false;
    }

    jobs_[tail_] = {.task = task, .context = context};
    tail_ = (tail_ + 1) % kCapacity;
    ++count_;
    wake_.notify_one();
    return true;
}

bool RenderWorker::wait_idle() noexcept {
    std::unique_lock lock(mutex_);
    if (!thread_.joinable()) {
        return count_ == 0 && !busy_;
    }

    idle_.wait(lock, [&] {
        return count_ == 0 && !busy_;
    });
    return true;
}

bool RenderWorker::run(Task task, void* context) noexcept {
    if (task == nullptr) {
        return false;
    }

    std::unique_lock lock(mutex_);
    if (!running_.load(std::memory_order_acquire) || stopping_ || count_ == kCapacity) {
        return false;
    }

    jobs_[tail_] = {.task = task, .context = context};
    tail_ = (tail_ + 1) % kCapacity;
    ++count_;
    wake_.notify_one();
    idle_.wait(lock, [&] {
        return count_ == 0 && !busy_;
    });
    return true;
}

void RenderWorker::thread_main() noexcept {
    for (;;) {
        Task task = nullptr;
        void* context = nullptr;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] {
                return stopping_ || count_ != 0;
            });
            if (count_ == 0) {
                if (stopping_) {
                    return;
                }
                continue;
            }

            task = jobs_[head_].task;
            context = jobs_[head_].context;
            head_ = (head_ + 1) % kCapacity;
            --count_;
            busy_ = true;
        }

        task(context);

        {
            std::lock_guard lock(mutex_);
            busy_ = false;
            if (count_ == 0) {
                idle_.notify_all();
            }
        }
    }
}

}
