#pragma once
#include "Job.h"

#include "AtomicMutex.h"

#include <thread>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <queue>
#include <condition_variable>

namespace JbSystem {
	class JobSystem;

	class JobSystemWorker {
		friend class JobSystem;

	public:
		JobSystemWorker(JobSystem* jobsystem);
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

		const Job* TryTakeJob(const JobPriority& maxTimeInvestment = JobPriority::High);

		/// <summary>
		/// Give a job to the worker thread
		/// NOTE* This will take ownership over the job
		/// </summary>
		/// <param name="newJob"></param>
		/// <param name="priority"></param>
		void GiveJob(Job*& newJob, const JobPriority priority);
		void GiveFutureJob(int& jobId);

		/// <summary>
		/// Finishes job and cleans up after
		/// </summary>
		/// <param name="job"></param>
		void FinishJob(const Job*& job);

		bool IsJobScheduled(const int& jobId);
		bool IsJobFinished(const int& jobId);

		//Is the read suppost to be active
		bool Active;
	private:
		void ThreadLoop();

		JobSystem* _jobsystem;

		mutex _modifyingThread;
		std::queue<Job*> _highPriorityTaskQueue;
		std::queue<Job*> _normalPriorityTaskQueue;
		std::queue<Job*> _lowPriorityTaskQueue;

		mutex _completedJobsMutex;
		std::unordered_set<int> _completedJobs;
		mutex _scheduledJobsMutex;
		std::unordered_set<int> _scheduledJobs;

		std::mutex _isRunningMutex;
		std::condition_variable _isRunningConditionalVariable;
		std::thread _worker;
	};
}