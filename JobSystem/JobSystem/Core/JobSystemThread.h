#pragma once
#include "InternalJob.h"

#include <thread>
#include <functional>
#include <queue>
#include <mutex>
#include <unordered_set>

namespace JbSystem {
	class JobSystemWorker {
		using executeExternalFunction = std::function<void()>;

		friend class JobSystem;

	public:
		JobSystemWorker(executeExternalFunction executeExternalJobFunction);
		JobSystemWorker(const JobSystemWorker& worker);
		~JobSystemWorker();

		/// <summary>
		/// returns weather or not worker is open to execute tasks
		/// there might be a delay in thread actually exiting be carefull
		/// </summary>
		/// <returns></returns>
		const bool IsRunning();
		void WaitForShutdown();
		void Start(); //Useful when thread became lost for some reason

		Job* TryTakeJob(JobPriority maxTimeInvestment = JobPriority::High);
		void GiveJob(Job*& newJob);

		/// <summary>
		/// Finishes job and cleans up after
		/// </summary>
		/// <param name="job"></param>
		void FinishJob(Job*& job);

		bool IsJobFinished(int& jobId);

		//Is the read suppost to be active
		bool Active;
	private:
		void ThreadLoop();

		executeExternalFunction _executeExternalJobFunction;

		std::mutex _queueMutex;
		std::queue<Job*> _highPriorityTaskQueue;
		std::queue<Job*> _normalPriorityTaskQueue;
		std::queue<Job*> _lowPriorityTaskQueue;

		std::mutex _completedJobsMutex;
		std::unordered_set<int> _completedJobs;

		std::mutex _isRunningMutex;
		std::condition_variable _isRunningConditionalVariable;
		std::thread _worker;
	};
}