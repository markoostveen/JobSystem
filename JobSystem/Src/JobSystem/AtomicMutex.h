#pragma once

#include <atomic>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#include <xmmintrin.h> // For _mm_pause() on x86/x86_64
#endif

namespace JbSystem
{
    class Mutex
    {
      public:
        Mutex() : _flag(false) {}
        Mutex(const Mutex&)            = delete;
        Mutex(Mutex&&)                 = delete;
        Mutex& operator=(const Mutex&) = delete;
        Mutex& operator=(Mutex&&)      = delete;
        ~Mutex() noexcept { unlock(); }

        inline bool try_lock() noexcept
        {
            return !_flag.exchange(true, std::memory_order_acquire);
        }

        inline void lock() noexcept
        {
            int backoff = 1;
            while (!try_lock())
            {
                for (int i = 0; i < backoff; ++i)
                {
                    // Architecture-specific pause instruction
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
                    _mm_pause(); // Use _mm_pause() on x86/x86_64
#elif defined(__arm__) || defined(__aarch64__)
                    __asm__ volatile("yield" ::: "memory"); // Use yield on ARM/ARM64
#elif defined(__riscv)
                    __asm__ volatile("nop"); // RISC-V placeholder; could also use a backoff strategy
#else
                    // Fallback to an empty pause if the architecture is unsupported
#endif
                }
                if (backoff < 16)
                    backoff *= 2;
            }
        }

        inline void unlock() noexcept
        {
            _flag.store(false, std::memory_order_release);
        }

      private:
        alignas(8) std::atomic<bool> _flag; // Align to cache line size to avoid false sharing
    };
} // namespace JbSystem
