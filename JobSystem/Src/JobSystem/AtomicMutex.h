#pragma once

#include <atomic>

namespace JbSystem {
	class mutex {
		std::atomic<bool> flag{ false };

	public:

		inline bool try_lock() {
			if (flag.exchange(true, std::memory_order_relaxed))
				return false;
			std::atomic_thread_fence(std::memory_order_acquire);
			return true;
		}

		inline void lock()
		{
			while (flag.exchange(true, std::memory_order_relaxed));
			std::atomic_thread_fence(std::memory_order_acquire);
		}

		inline void unlock()
		{
			std::atomic_thread_fence(std::memory_order_release);
			flag.store(false, std::memory_order_relaxed);
		}
	};
}
