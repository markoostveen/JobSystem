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
		bool IsRunning();
		void WaitForShutdown();
		void Start(); //Useful when thread became lost for some reason
		int WorkerId();

		Job* TryTakeJob(const JobPriority& maxTimeInvestment);
		void UnScheduleJob(const JobId& previouslyScheduledJob);
		void ScheduleJob(const JobId& jobId);

		bool IsJobInQueue(const JobId& jobId);

		size_t ScheduledJobCount();

		/// <summary>
		/// Give a job to the worker thread
		/// NOTE* This will take ownership over the job
		/// </summary>
		/// <param name="newJob"></param>
		/// <param name="priority"></param>
		bool GiveJob(Job* const& newJob, const JobPriority priority);
		void GiveFutureJob(const JobId& jobId);
		void GiveFutureJobs(const std::vector<Job*>& newjobs, int startIndex, int size);

		/// <summary>
		/// Finishes job and cleans up after
		/// </summary>
		/// <param name="job"></param>
		void FinishJob(Job*& job);

		bool IsJobScheduled(const JobId& jobId);

		void ThreadLoop();

		void RequestShutdown();

		//Is the read suppost to be active
		std::atomic<bool> Active;

		bool Busy();

	private:

		struct PausedJob {
			PausedJob(Job* affectedJob, JobSystemWorker& worker);

			Job* AffectedJob;
			JobSystemWorker& Worker;
		};

		JobSystem* _jobsystem;

		std::atomic<bool> _shutdownRequested;

		mutex _modifyingThread;
		std::deque<Job*> _highPriorityTaskQueue;
		std::deque<Job*> _normalPriorityTaskQueue;
		std::deque<Job*> _lowPriorityTaskQueue;

		mutex _scheduledJobsMutex;
		std::unordered_set<int> _scheduledJobs;

		mutex _isRunningMutex;
		std::thread _worker;

		std::atomic<bool> _isBusy;

		// DeadLock prevention
		JbSystem::mutex _jobsRequiringIgnoringMutex;
		std::unordered_set<Job*> _jobsRequiringIgnoring;
		JbSystem::mutex _pausedJobsMutex;
		std::unordered_map<JobId, PausedJob> _pausedJobs;

	};
}