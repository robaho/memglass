#pragma once

#include <atomic>
#include <cstring>
#include <optional>
#include <type_traits>

#ifdef __x86_64__
#include <immintrin.h>
#define MEMGLASS_PAUSE() _mm_pause()
#else
#define MEMGLASS_PAUSE() ((void)0)
#endif

namespace memglass {

// Seqlock-protected value for consistent reads of compound types.
// Uses atomic_signal_fence for compiler barrier combined with release/acquire
// semantics on sequence counter for CPU barriers.
//
// Key insight: atomic_signal_fence prevents compiler reordering, while the
// release/acquire on seq_ provides the necessary CPU memory ordering.
// Direct assignment (not memcpy) allows the compiler to optimize the copy.
template <typename T>
struct Guarded {
    static_assert(std::is_nothrow_copy_assignable_v<T>,
                  "Guarded<T> requires nothrow copy assignable T");
    static_assert(std::is_trivially_copy_assignable_v<T>,
                  "Guarded<T> requires trivially copy assignable T");

    Guarded() : seq_(0) {
    }
    explicit Guarded(const T &v) : seq_(0), value_(v) {
    }

    // Producer write - single writer assumed
    void write(const T &v) noexcept {
        std::size_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_release);  // Odd = write in progress
        std::atomic_signal_fence(std::memory_order_acq_rel);
        value_ = v;
        std::atomic_signal_fence(std::memory_order_acq_rel);
        seq_.store(s + 2, std::memory_order_release);  // Even = write complete
    }

    // Observer read - spins until consistent read obtained
    T read() const noexcept {
        T copy;
        std::size_t s1, s2;
        do {
            s1 = seq_.load(std::memory_order_acquire);
            std::atomic_signal_fence(std::memory_order_acq_rel);
            copy = value_;
            std::atomic_signal_fence(std::memory_order_acq_rel);
            s2 = seq_.load(std::memory_order_acquire);
        } while (s1 != s2 || s1 & 1);
        return copy;
    }

    // Try read without spinning (returns nullopt if write in progress or torn)
    std::optional<T> try_read() const noexcept {
        std::size_t s1 = seq_.load(std::memory_order_acquire);
        if (s1 & 1) return std::nullopt;

        std::atomic_signal_fence(std::memory_order_acq_rel);
        T copy = value_;
        std::atomic_signal_fence(std::memory_order_acq_rel);

        std::size_t s2 = seq_.load(std::memory_order_acquire);
        if (s1 != s2) return std::nullopt;

        return copy;
    }

private:
    T value_{};
    std::atomic<std::size_t> seq_;
};

// Spinlock-protected value for exclusive access
template <typename T>
struct Locked {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Locked<T> requires trivially copyable T");

    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    T value{};

    Locked() = default;
    explicit Locked(const T &v) : value(v) {
    }

    void write(const T &v) {
        while (lock_.test_and_set(std::memory_order_acquire)) {
            MEMGLASS_PAUSE();
        }
        std::memcpy(&value, &v, sizeof(T));
        lock_.clear(std::memory_order_release);
    }

    T read() const {
        while (lock_.test_and_set(std::memory_order_acquire)) {
            MEMGLASS_PAUSE();
        }
        T result;
        std::memcpy(&result, &value, sizeof(T));
        lock_.clear(std::memory_order_release);
        return result;
    }

    // Read-modify-write operation
    template <typename F>
    void update(F &&func) {
        while (lock_.test_and_set(std::memory_order_acquire)) {
            MEMGLASS_PAUSE();
        }
        func(value);
        lock_.clear(std::memory_order_release);
    }
};

}  // namespace memglass
