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

// Compiler barrier to prevent reordering
#if defined(__GNUC__) || defined(__clang__)
#define MEMGLASS_COMPILER_BARRIER() asm volatile("" ::: "memory")
#else
#define MEMGLASS_COMPILER_BARRIER() std::atomic_signal_fence(std::memory_order_seq_cst)
#endif

// Seqlock-protected value for consistent reads of compound types
// This implementation uses a classic seqlock pattern with proper memory barriers
template<typename T>
struct Guarded {
    static_assert(std::is_trivially_copyable_v<T>, "Guarded<T> requires trivially copyable T");

    std::atomic<uint32_t> seq_{0};
    T value_{};

    Guarded() = default;
    explicit Guarded(const T& v) : value_(v) {}

    // Producer write - classic seqlock write
    void write(const T& v) {
        // Increment to odd (write in progress)
        uint32_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_relaxed);
        MEMGLASS_COMPILER_BARRIER();

        // Copy the data
        value_ = v;

        // Compiler barrier + store release to make data visible
        MEMGLASS_COMPILER_BARRIER();
        seq_.store(s + 2, std::memory_order_release);
    }

    // Observer read (spins until consistent)
    T read() const {
        T result;
        uint32_t s1, s2;
        do {
            // Load sequence with acquire semantics
            s1 = seq_.load(std::memory_order_acquire);

            // If odd, write in progress - spin
            if (s1 & 1) {
                MEMGLASS_PAUSE();
                continue;
            }

            // Full memory fence to ensure all subsequent loads happen after s1
            std::atomic_thread_fence(std::memory_order_seq_cst);

            // Copy the data using memcpy to ensure proper ordering
            std::memcpy(&result, const_cast<const T*>(&value_), sizeof(T));

            // Full memory fence to ensure all prior loads complete before s2
            std::atomic_thread_fence(std::memory_order_seq_cst);

            // Re-load sequence to check consistency
            s2 = seq_.load(std::memory_order_acquire);

            // If sequence changed, we may have read torn data - retry
        } while (s1 != s2);

        return result;
    }

    // Try read without spinning (returns nullopt if write in progress or torn)
    std::optional<T> try_read() const {
        uint32_t s1 = seq_.load(std::memory_order_acquire);
        if (s1 & 1) return std::nullopt;

        std::atomic_thread_fence(std::memory_order_seq_cst);

        T result;
        std::memcpy(&result, const_cast<const T*>(&value_), sizeof(T));

        std::atomic_thread_fence(std::memory_order_seq_cst);

        uint32_t s2 = seq_.load(std::memory_order_acquire);
        if (s1 != s2) return std::nullopt;

        return result;
    }
};

// Spinlock-protected value for exclusive access
template<typename T>
struct Locked {
    static_assert(std::is_trivially_copyable_v<T>, "Locked<T> requires trivially copyable T");

    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    T value{};

    Locked() = default;
    explicit Locked(const T& v) : value(v) {}

    void write(const T& v) {
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
    template<typename F>
    void update(F&& func) {
        while (lock_.test_and_set(std::memory_order_acquire)) {
            MEMGLASS_PAUSE();
        }
        func(value);
        lock_.clear(std::memory_order_release);
    }
};

} // namespace memglass
