#pragma once

#include <atomic>

namespace JbSystem {
	class mutex {
	public:

		mutex() : _flag(false) {

		}
		mutex(mutex&&) = delete;
		mutex(const mutex&) = delete;
		mutex operator=(const mutex&) = delete;
		mutex operator=(mutex&&) = delete;

		~mutex() {
			unlock();
		}

		bool try_lock() {
			if (_flag.exchange(true, std::memory_order_relaxed)) {
				return false;
			}
			std::atomic_thread_fence(std::memory_order_acquire);
			return true;
		}

		void lock()
		{
			while (!try_lock()) {}
		}

		void unlock()
		{
			std::atomic_thread_fence(std::memory_order_release);
			_flag.store(false, std::memory_order_relaxed);
		}

	private:
		std::atomic<bool> _flag;
	};
}
