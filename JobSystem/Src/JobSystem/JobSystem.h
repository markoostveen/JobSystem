#pragma once

#include "Job.h"
#include "WorkerThread.h"

#include <thread>
#include <atomic>
#include <mutex>
#include <set>
#include <unordered_set>
#include <array>
#include <type_traits>

namespace JbSystem {
	class JobSystemWorker;

	class JobSystem {
	public:
		JobSystem(int threadCount = std::thread::hardware_concurrency() - 1);
		~JobSystem();

		/// <summary>
		/// Use to modify properties of the jobSystem
		/// </summary>
		/// <param name="threadCount"></param>
		void ReConfigure(int threadCount = std::thread::hardware_concurrency() - 1) {
			Start(threadCount);
		}

		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		int Schedule(Job* newjob);

		//Single function
		template<JobFunction T>
		int Schedule(T function, JobPriority timeInvestment = JobPriority::Normal);
		template<JobFunction T, typename ...JobId>
		int ScheduleDependend(T function, JobPriority timeInvestment, JobId... dependencies);
		template<JobFunction T, typename ...JobId>
		int ScheduleDependend(T function, JobId... dependencies);
		template<JobFunction T>
		int ScheduleDependend(T function, JobPriority timeInvestment, std::vector<int> dependencies);

		//Parallel loops
		template<JobFunction T>
		std::vector<int> Schedule(int totalIterations, int batchSize, T function, JobPriority timeInvestment = JobPriority::Normal);
		template<JobFunction T, typename ...JobId>
		std::vector<int> ScheduleDependend(int totalIterations, int batchSize, T function, JobPriority timeInvestment, JobId... dependencies);
		template<JobFunction T, typename ...JobId>
		std::vector<int> ScheduleDependend(int totalIterations, int batchSize, T function, JobId... dependencies);
		template<JobFunction T>
		std::vector<int> ScheduleDependend(int totalIterations, int batchSize, T function, JobPriority timeInvestment, std::vector<int> dependencies);
		template<ParrallelJobFunction T>
		std::vector<int> Schedule(int startIndex, int endIndex, int batchSize, T function, JobPriority timeInvestment = JobPriority::Normal);
		template<ParrallelJobFunction T, typename ...JobId>
		std::vector<int> ScheduleDependend(int startIndex, int endIndex, int batchSize, T function, JobPriority timeInvestment, JobId... dependencies);
		template<ParrallelJobFunction T, typename ...JobId>
		std::vector<int> ScheduleDependend(int startIndex, int endIndex, int batchSize, T function, JobId... dependencies);
		template<ParrallelJobFunction T>
		std::vector<int> ScheduleDependend(int startIndex, int endIndex, int batchSize, T function, JobPriority timeInvestment, std::vector<int> dependencies);

		/// <summary>
		/// Caller will help execute jobs until the job with specified Id is completed
		/// </summary>
		/// <param name="jobId"></param>
		/// <returns>weather or not the job was completed</returns>
		bool IsJobCompleted(const int jobId);

		/// <summary>
		/// Caller will help execute jobs until the job with specified Id is completed
		/// </summary>
		/// <param name="jobId"></param>
		/// <param name="maxMicroSecondsToWait">when elapsed function will return false if job hasn't been completed yet, 0 = infinity</param>
		/// <returns>weather or not the job was completed in time</returns>
		bool WaitForJobCompletion(int jobId, JobPriority maximumHelpEffort = JobPriority::Low);

		/// <summary>
		/// Caller will help execute jobs until the job with specified Id is completed
		/// </summary>
		/// <param name="jobId"></param>
		/// <param name="maxMicroSecondsToWait">when elapsed function will return false if job hasn't been completed yet, 0 = infinity</param>
		/// <returns>weather or not the job was completed in time</returns>
		bool WaitForJobCompletion(int jobId, int maxMicroSecondsToWait, JobPriority maximumHelpEffort = JobPriority::Low);

		/// <summary>
		/// Block execution until given jobs have been completed, this operation is blocking
		/// </summary>
		/// <param name="jobIds"></param>
		void WaitForJobCompletion(std::vector<int>& jobIds, JobPriority maximumHelpEffort = JobPriority::Low);

		/// <summary>
		/// wait for jobs to complete, then execute function
		/// </summary>
		/// <param name="dependencies">jobs to wait for before scheduling the 'newJob'</param>
		/// <param name="callback">function to execute after jobs have been completed</param>
		/// <returns></returns>
		template<JobFunction T>
		void WaitForJobCompletion(const std::vector<int> dependencies, const T& callback, JobPriority maximumHelpEffort = JobPriority::Low);

		/// <summary>
		/// Executes a scheduled job
		/// </summary>
		void ExecuteJob(JobPriority maxTimeInvestment = JobPriority::Low);

		/// <summary>
		/// Get singleton instance
		/// </summary>
		/// <returns></returns>
		static std::shared_ptr<JobSystem> GetInstance();

	private:

		/// <summary>
		/// Shutdown all worker threads
		/// </summary>
		/// <returns> vector of all remaining jobs </returns>
		std::vector<Job*> Shutdown();

		/// <summary>
		/// Start or Restart the jobsystem, with the desired amount of workers
		/// Note, this function may also be called after it has been started initially queued jobs will be rescheduled
		/// </summary>
		/// <param name="threadCount"></param>
		void Start(int threadCount = std::thread::hardware_concurrency() - 1);
		int ScheduleFutureJob(Job* newFutureJob);
		std::vector<int> BatchScheduleJob(std::vector<Job*> newjobs, JobPriority durationOfEveryJob);
		std::vector<int> BatchScheduleFutureJob(std::vector<Job*> newjobs);

		void ExecuteJobFromWorker(JobPriority maxTimeInvestment = JobPriority::Low);

		void Cleanup();

		/// <summary>
		/// Take all scheduled jobs from all workers
		/// </summary>
		/// <returns></returns>
		std::vector<Job*> StealAllJobsFromWorkers();

		std::mutex _jobsMutex;
		std::unordered_set<int> _scheduledJobs;

		//Jobs that have been completed
		//Note: they are ordered in terms of thread Ids, this way we can be sure to have access at the same time

		int _workerCount = 0;
		std::vector<JobSystemWorker> _workers;

		int _maxSchedulesTillMaintainance = 1000;
		std::atomic<int> _schedulesTillMaintainance = _maxSchedulesTillMaintainance;
	};

	template<JobFunction T>
	inline int JobSystem::Schedule(T function, JobPriority priority)
	{
		return Schedule(new Job(function, priority));
	}

	template<JobFunction T, typename ...JobId>
	inline int JobSystem::ScheduleDependend(T function, JobPriority timeInvestment, JobId... dependencies)
	{
		Job* newJob = new Job(function, timeInvestment);

		const std::vector<int> dependencyArray = { dependencies ... };

		//Schedule jobs in the future, then when completed, schedule them for inside workers
		int jobId = ScheduleFutureJob(newJob);
		WaitForJobCompletion(dependencyArray, [this, newJob]() { Schedule(newJob); });
		return jobId;
	}

	template<JobFunction T, typename ...JobId>
	inline int JobSystem::ScheduleDependend(T function, JobId ...dependencies)
	{
		return ScheduleDependend(function, JobPriority::Normal, dependencies...);
	}

	template<JobFunction T>
	inline int JobSystem::ScheduleDependend(T function, JobPriority timeInvestment, std::vector<int> dependencies)
	{
		if (dependencies.size() == 0) {
			return Schedule(function, timeInvestment);
		}

		Job* newJob = new Job(function, timeInvestment);

		//Schedule jobs in the future, then when completed, schedule them for inside workers
		int jobId = ScheduleFutureJob(newJob);
		WaitForJobCompletion(dependencies, [this, newJob]() { Schedule(newJob); });
		return jobId;
	}

	template<JobFunction T>
	inline std::vector<int> JobSystem::Schedule(int totalIterations, int batchSize, T function, JobPriority timeInvestment)
	{
		std::vector<Job*> jobs;

		//Schedule and create lambda for all job kinds
		int totalBatches = 0;
		int CurrentBatchEnd = totalIterations;
		while (CurrentBatchEnd > batchSize) {
			CurrentBatchEnd -= batchSize;
			jobs.push_back(new Job(
				[function, jobStartIndex = totalIterations - ((totalBatches + 1) * batchSize), jobEndIndex = totalIterations - (totalBatches * batchSize)](){
				for (int i = jobStartIndex; i < jobEndIndex; i++)
				{
					function();
				}
			}, timeInvestment));
			totalBatches++;
		}

		//Create last job
		jobs.push_back(new Job(
			[function, jobStartIndex = 0, jobEndIndex = totalIterations - (totalBatches * batchSize)](){
			for (int i = jobStartIndex; i < jobEndIndex; i++)
			{
				function();
			}
		}, timeInvestment));

		return BatchScheduleJob(jobs, timeInvestment);
	}

	template<JobFunction T, typename ...JobId>
	inline std::vector<int> JobSystem::ScheduleDependend(int totalIterations, int batchSize, T function, JobPriority timeInvestment, JobId ...dependencies)
	{
		std::vector<Job*> jobs;

		//Schedule and create lambda for all job kinds
		int totalBatches = 0;
		int CurrentBatchEnd = totalIterations;
		while (CurrentBatchEnd > batchSize) {
			CurrentBatchEnd -= batchSize;
			jobs.push_back(new Job(
				[function, jobStartIndex = totalIterations - ((totalBatches + 1) * batchSize), jobEndIndex = totalIterations - (totalBatches * batchSize)](){
				for (int i = jobStartIndex; i < jobEndIndex; i++)
				{
					function(i);
				}
			}, timeInvestment));
			totalBatches++;
		}

		//Create last job
		jobs.push_back(new Job(
			[function, jobStartIndex = 0, jobEndIndex = totalIterations - (totalBatches * batchSize)](){
			for (int i = jobStartIndex; i < jobEndIndex; i++)
			{
				function(i);
			}
		}, timeInvestment));

		const std::vector<int> dependencyArray = { jobIds ... };

		//Schedule jobs in the future, then when completed, schedule them for inside workers
		std::vector<int> jobIds = BatchScheduleFutureJob(jobs);
		WaitForJobCompletion(dependencyArray, [this, jobs, timeInvestment]() { BatchScheduleJob(jobs, timeInvestment); });
		return jobIds;
	}

	template<JobFunction T, typename ...JobId>
	inline std::vector<int> JobSystem::ScheduleDependend(int totalIterations, int batchSize, T function, JobId ...dependencies)
	{
		return ScheduleDependend(totalIterations, batchSize, function, dependencies...);
	}

	template<JobFunction T>
	inline std::vector<int> JobSystem::ScheduleDependend(int totalIterations, int batchSize, T function, JobPriority timeInvestment, std::vector<int> dependencies)
	{
		if (jobIds.size() == 0) {
			return Schedule(totalIterations, batchSize, timeInvestment, function);
		}

		std::vector<Job*> jobs;

		//Schedule and create lambda for all job kinds
		int totalBatches = 0;
		int CurrentBatchEnd = totalIterations;
		while (CurrentBatchEnd > batchSize) {
			CurrentBatchEnd -= batchSize;
			jobs.push_back(new Job(
				[function, jobStartIndex = totalIterations - ((totalBatches + 1) * batchSize), jobEndIndex = totalIterations - (totalBatches * batchSize)](){
				for (int i = jobStartIndex; i < jobEndIndex; i++)
				{
					function(i);
				}
			}, timeInvestment));
			totalBatches++;
		}

		//Create last job
		jobs.push_back(new Job(
			[function, jobStartIndex = 0, jobEndIndex = totalIterations - (totalBatches * batchSize)](){
			for (int i = jobStartIndex; i < jobEndIndex; i++)
			{
				function(i);
			}
		}, timeInvestment));

		//Schedule jobs in the future, then when completed, schedule them for inside workers
		std::vector<int> jobIds = BatchScheduleFutureJob(jobs);
		WaitForJobCompletion(dependencies, [this, jobs, timeInvestment]() { BatchScheduleJob(jobs, timeInvestment); });
		return jobIds;
	}

	template<ParrallelJobFunction T>
	inline std::vector<int> JobSystem::Schedule(int startIndex, int endIndex, int batchSize, T function, JobPriority timeInvestment)
	{
		std::vector<Job*> jobs;

		//Schedule and create lambda for all job kinds
		int totalBatches = 0;
		int endOfRange = endIndex - startIndex;
		int CurrentBatchEnd = endOfRange;
		while (CurrentBatchEnd > batchSize) {
			CurrentBatchEnd -= batchSize;
			jobs.push_back(new Job(
				[function, jobStartIndex = endOfRange - ((totalBatches + 1) * batchSize), jobEndIndex = endOfRange - (totalBatches * batchSize)](){
				for (int i = jobStartIndex; i < jobEndIndex; i++)
				{
					function(i);
				}
			}, timeInvestment));
			totalBatches++;
		}

		//Create last job
		jobs.push_back(new Job(
			[function, jobStartIndex = 0, jobEndIndex = endOfRange - (totalBatches * batchSize)](){
			for (int i = jobStartIndex; i < jobEndIndex; i++)
			{
				function(i);
			}
		}, timeInvestment));

		return BatchScheduleJob(jobs, timeInvestment);
	}
	template<ParrallelJobFunction T, typename ...JobId>
	inline std::vector<int> JobSystem::ScheduleDependend(int startIndex, int endIndex, int batchSize, T function, JobPriority timeInvestment, JobId ... dependencies)
	{
		std::vector<Job*> jobs;

		//Schedule and create lambda for all job kinds
		int totalBatches = 0;
		int endOfRange = endIndex - startIndex;
		int CurrentBatchEnd = endOfRange;
		while (CurrentBatchEnd > batchSize) {
			CurrentBatchEnd -= batchSize;
			jobs.push_back(new Job(
				[function, jobStartIndex = endOfRange - ((totalBatches + 1) * batchSize), jobEndIndex = endOfRange - (totalBatches * batchSize)](){
				for (int i = jobStartIndex; i < jobEndIndex; i++)
				{
					function(i);
				}
			}, timeInvestment));
			totalBatches++;
		}

		//Create last job
		jobs.push_back(new Job(
			[function, jobStartIndex = 0, jobEndIndex = endOfRange - (totalBatches * batchSize)](){
			for (int i = jobStartIndex; i < jobEndIndex; i++)
			{
				function(i);
			}
		}, timeInvestment));

		const std::vector<int> dependencyArray = { dependencies ... };

		//Schedule jobs in the future, then when completed, schedule them for inside workers
		std::vector<int> jobIds = BatchScheduleFutureJob(jobs);
		WaitForJobCompletion(dependencyArray, [this, jobs, timeInvestment]() { BatchScheduleJob(jobs, timeInvestment); });
		return jobIds;
	}
	template<ParrallelJobFunction T, typename ...JobId>
	inline std::vector<int> JobSystem::ScheduleDependend(int startIndex, int endIndex, int batchSize, T function, JobId ...dependencies)
	{
		return ScheduleDependend(startIndex, endIndex, batchSize, function, JobPriority::Normal, dependencies...);
	}
	template<ParrallelJobFunction T>
	inline std::vector<int> JobSystem::ScheduleDependend(int startIndex, int endIndex, int batchSize, T function, JobPriority timeInvestment, std::vector<int> dependencies)
	{
		if (dependencies.size() == 0) {
			return Schedule(startIndex, endIndex, batchSize, function, timeInvestment);
		}

		std::vector<Job*> jobs;

		//Schedule and create lambda for all job kinds
		int totalBatches = 0;
		int endOfRange = endIndex - startIndex;
		int CurrentBatchEnd = endOfRange;
		while (CurrentBatchEnd > batchSize) {
			CurrentBatchEnd -= batchSize;
			jobs.push_back(new Job(
				[function, jobStartIndex = endOfRange - ((totalBatches + 1) * batchSize), jobEndIndex = endOfRange - (totalBatches * batchSize)](){
				for (int i = jobStartIndex; i < jobEndIndex; i++)
				{
					function(i);
				}
			}, timeInvestment));
			totalBatches++;
		}

		//Create last job
		jobs.push_back(new Job(
			[function, jobStartIndex = 0, jobEndIndex = endOfRange - (totalBatches * batchSize)](){
			for (int i = jobStartIndex; i < jobEndIndex; i++)
			{
				function(i);
			}
		}, timeInvestment));

		//Schedule jobs in the future, then when completed, schedule them for inside workers
		std::vector<int> jobIds = BatchScheduleFutureJob(jobs);
		WaitForJobCompletion(dependencies, [this, jobs, timeInvestment]() { BatchScheduleJob(jobs, timeInvestment); });
		return jobIds;
	}

	template<JobFunction T>
	inline void JobSystem::WaitForJobCompletion(const std::vector<int> dependencies, const T& callback, JobPriority maximumHelpEffort)
	{
		auto jobScheduler = [this, dependencies, maximumHelpEffort](auto& retryFunction, auto& callback) -> void {
			for (size_t i = 0; i < dependencies.size(); i++) {
				if (!WaitForJobCompletion(dependencies.at(i), maximumHelpEffort)) {
					// Prerequsites not met, queueing async job to check back later
					constexpr JobPriority dependencyCheckPriority = JobPriority::Low;
					Schedule(new Job([retryFunction, callback]() -> void { retryFunction(retryFunction, callback); }, dependencyCheckPriority));
					return;
				}
			}

			//When all dependencies are completed
			callback();
		};

		// run in sync
		jobScheduler(jobScheduler, callback);
	}
}
