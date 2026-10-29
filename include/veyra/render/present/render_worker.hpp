#ifndef VEYRA_RENDER_RENDER_WORKER_HPP
#define VEYRA_RENDER_RENDER_WORKER_HPP

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

namespace veyra {

class RenderWorker {
  public:
    using Task = void (*)(void*) noexcept;
    static constexpr std::size_t kCapacity = 32;

    RenderWorker() noexcept = default;
    ~RenderWorker();

    RenderWorker(const RenderWorker&) = delete;
    RenderWorker& operator=(const RenderWorker&) = delete;
    RenderWorker(RenderWorker&&) = delete;
    RenderWorker& operator=(RenderWorker&&) = delete;

    bool start() noexcept;
    void stop() noexcept;
    bool running() const noexcept;
    bool is_worker_thread() const noexcept;
    std::thread::id thread_id() const noexcept;

    bool submit(Task task, void* context) noexcept;
    bool wait_idle() noexcept;
    bool run(Task task, void* context) noexcept;

  private:
    struct Job {
        Task task{};
        void* context{};
    };

    void thread_main() noexcept;

    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::array<Job, kCapacity> jobs_{};
    std::size_t head_{};
    std::size_t tail_{};
    std::size_t count_{};
    bool busy_{};
    bool stopping_{};
    std::atomic<bool> running_{};
    std::thread thread_;
};

}

namespace veyra::render {
using ::veyra::RenderWorker;
}

namespace veyra::render::present {
using ::veyra::RenderWorker;
}

#endif
