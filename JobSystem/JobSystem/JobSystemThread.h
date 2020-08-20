#pragma once
#include "JobSystem.h"

#include <thread>
#include <functional>

namespace JbSystem {
	class JobSystemWorker {
	public:
		JobSystemWorker();

		const bool IsRunning();
		void start(); //Useful when thread became lost for some reason

		int CurrentJobId;
	private:
		std::thread _worker;
	};
}