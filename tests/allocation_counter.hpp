#ifndef VEYRA_TESTS_ALLOCATION_COUNTER_HPP
#define VEYRA_TESTS_ALLOCATION_COUNTER_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>

inline std::atomic<std::size_t> g_allocation_count{0};
inline std::atomic<bool> g_counting{false};
inline const void* volatile g_allocation_sink{};

inline void keep_allocation(const void* ptr) noexcept {
    g_allocation_sink = ptr;
}

inline void note_allocation() noexcept {
    if (g_counting.load(std::memory_order_relaxed)) {
        g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    }
}

inline std::size_t allocation_count() noexcept {
    return g_allocation_count.load(std::memory_order_relaxed);
}

inline bool counting() noexcept {
    return g_counting.load(std::memory_order_relaxed);
}

inline void* allocate_unaligned(std::size_t size) {
    note_allocation();

    if (size == 0) {
        size = 1;
    }

    void* result = std::malloc(size);

    if (result == nullptr) {
        throw std::bad_alloc{};
    }

    return result;
}

inline void* allocate_aligned(std::size_t size, std::size_t alignment) {
    note_allocation();

    if (alignment < alignof(void*)) {
        alignment = alignof(void*);
    }

    if (size == 0) {
        size = 1;
    }

    const auto header = sizeof(void*);

    if (size > std::numeric_limits<std::size_t>::max() - alignment - header) {
        throw std::bad_alloc{};
    }

    void* raw = std::malloc(size + alignment + header);

    if (raw == nullptr) {
        throw std::bad_alloc{};
    }

    const auto base = reinterpret_cast<std::uintptr_t>(raw) + header;
    const auto aligned = (base + alignment - 1U) & ~(alignment - 1U);
    auto* slot = reinterpret_cast<void**>(aligned - header);
    *slot = raw;

    return reinterpret_cast<void*>(aligned);
}

inline void release_unaligned(void* ptr) noexcept {
    std::free(ptr);
}

inline void release_aligned(void* ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }

    const auto slot = reinterpret_cast<void**>(static_cast<char*>(ptr) - sizeof(void*));

    std::free(*slot);
}

class Measure {
  public:
    Measure() noexcept {
        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting.store(true, std::memory_order_relaxed);
    }

    ~Measure() noexcept {
        g_counting.store(false, std::memory_order_relaxed);
    }

    Measure(const Measure&) = delete;
    Measure& operator=(const Measure&) = delete;

    std::size_t count() const noexcept {
        return allocation_count();
    }
};

void* operator new(std::size_t size) {
    return allocate_unaligned(size);
}

void* operator new[](std::size_t size) {
    return allocate_unaligned(size);
}

void operator delete(void* ptr) noexcept {
    release_unaligned(ptr);
}

void operator delete[](void* ptr) noexcept {
    release_unaligned(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    release_unaligned(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    release_unaligned(ptr);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    release_aligned(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
    release_aligned(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    release_aligned(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    release_aligned(ptr);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    note_allocation();

    if (size == 0) {
        size = 1;
    }

    return std::malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    note_allocation();

    if (size == 0) {
        size = 1;
    }

    return std::malloc(size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    release_unaligned(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    release_unaligned(ptr);
}

#endif
