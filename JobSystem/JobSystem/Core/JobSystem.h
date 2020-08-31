#pragma once

#include "InternalJob.h"
#include "JobSystemThread.h"

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
		JobSystem() = default;
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
		int Schedule(InternalJobBase* newjob);

		//Single function
		template<JobFunction T>
		int Schedule(T function, JobPriority timeInvestment);
		template<JobFunction T, typename ...JobId>
		int Schedule(T function, JobPriority timeInvestment, JobId... dependencies);
		template<JobFunction T>
		int Schedule(T function, JobPriority timeInvestment, std::vector<int> dependencies);

		//Parallel loops
		template<JobFunction T>
		std::vector<int> Schedule(int totalIterations, int batchSize, JobPriority timeInvestment, T function);
		template<JobFunction T, typename ...JobId>
		std::vector<int> Schedule(int totalIterations, int batchSize, JobPriority timeInvestment, T function, JobId... dependencies);
		template<JobFunction T>
		std::vector<int> Schedule(int totalIterations, int batchSize, JobPriority timeInvestment, T function, std::vector<int> dependencies);
		template<ParrallelJobFunction T>
		std::vector<int> Schedule(int startIndex, int endIndex, int batchSize, JobPriority timeInvestment, T function);
		template<ParrallelJobFunction T, typename ...JobId>
		std::vector<int> Schedule(int startIndex, int endIndex, int batchSize, JobPriority timeInvestment, T function, JobId... dependencies);
		template<ParrallelJobFunction T>
		std::vector<int> Schedule(int startIndex, int endIndex, int batchSize, JobPriority timeInvestment, T function, std::vector<int> dependencies);

		/// <summary>
		/// Caller will help execute jobs until the job with specified Id is completed
		/// </summary>
		/// <param name="jobId"></param>
		/// <param name="maxMicroSecondsToWait">when elapsed function will return false if job hasn't been completed yet, 0 = infinity</param>
		/// <returns>weather or not the job was completed</returns>
		bool WaitForJobCompletion(int jobId, bool helpExecutingOtherJobs = true, int maxMicroSecondsToWait = 0);

		/// <summary>
		/// Block execution until given jobs have been completed, this operation is blocking
		/// </summary>
		/// <param name="jobIds"></param>
		void WaitForJobCompletion(std::vector<int>& jobIds);

		/// <summary>
		/// wait for jobs to complete, then execute function
		/// </summary>
		/// <param name="dependencies">jobs to wait for before scheduling the 'newJob'</param>
		/// <param name="callback">function to execute after jobs have been completed</param>
		/// <returns></returns>
		template<JobFunction T>
		void WaitForJobCompletion(const std::vector<int> dependencies, const T& callback);

		/// <summary>
		/// Executes a scheduled job
		/// </summary>
		void ExecuteJob(JobPriority maxTimeInvestment = JobPriority::Low);

		/// <summary>
		/// Get singleton instance
		/// </summary>
		/// <returns></returns>
		static JobSystem* GetInstance();

		int ActiveJobCount();

	private:

		/// <summary>
		/// Shutdown all worker threads
		/// </summary>
		/// <returns> vector of all remaining jobs </returns>
		std::vector<InternalJobBase*> Shutdown();

		/// <summary>
		/// Start or Restart the jobsystem, with the desired amount of workers
		/// Note, this function may also be called after it has been started initially queued jobs will be rescheduled
		/// </summary>
		/// <param name="threadCount"></param>
		void Start(int threadCount = std::thread::hardware_concurrency() - 1);
		int ScheduleFutureJob(InternalJobBase* newFutureJob);
		std::vector<int> BatchScheduleJob(std::vector<InternalJobBase*> newjobs, JobPriority durationOfEveryJob);
		std::vector<int> BatchScheduleFutureJob(std::vector<InternalJobBase*> newjobs);

		void ExecuteJobFromWorker(JobPriority maxTimeInvestment = JobPriority::Low);

		bool IsJobCompleted(int& jobId);

		void Cleanup();

		/// <summary>
		/// Take all scheduled jobs from all workers
		/// </summary>
		/// <returns></returns>
		std::vector<InternalJobBase*> StealAllJobsFromWorkers();

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
		return Schedule(new InternalJob(function, priority));
	}

	template<JobFunction T, typename ...JobId>
	inline int JobSystem::Schedule(T function, JobPriority timeInvestment, JobId... dependencies)
	{
		InternalJob<T>* newJob = new InternalJob(function, timeInvestment);

		const std::vector<int> dependencyArray = { dependencies ... };

		//Schedule jobs in the future, then when completed, schedule them for inside workers
		int jobId = ScheduleFutureJob(newJob);
		WaitForJobCompletion(dependencyArray, [this, newJob]() { Schedule(newJob); });
		return jobId;
	}

	template<JobFunction T>
	inline int JobSystem::Schedule(T function, JobPriority timeInvestment, std::vector<int> dependencies)
	{
		if (dependencies.size() == 0) {
			return Schedule(function, timeInvestment);
		}

		InternalJob<T>* newJob = new InternalJob(function, timeInvestment);

		//Schedule jobs in the future, then when completed, schedule them for inside workers
		int jobId = ScheduleFutureJob(newJob);
		WaitForJobCompletion(dependencies, [this, newJob]() { Schedule(newJob); });
		return jobId;
	}

	template<JobFunction T>
	inline std::vector<int> JobSystem::Schedule(int totalIterations, int batchSize, JobPriority timeInvestment, T function)
	{
		std::vector<InternalJobBase*> jobs;

		//Schedule and create lambda for all job kinds
		int totalBatches = 0;
		int CurrentBatchEnd = totalIterations;
		while (CurrentBatchEnd > batchSize) {
			CurrentBatchEnd -= batchSize;
			jobs.push_back(new InternalJob(
				[function, jobStartIndex = totalIterations - ((totalBatches + 1) * batchSize), jobEndIndex = totalIterations - (totalBatches * batchSize)](){
				for (int i = jobStartIndex; i < jobEndIndex; i++)
				{
					function();
				}
			}, timeInvestment));
			totalBatches++;
		}

		//Create last job
		jobs.push_back(new InternalJob(
			[function, jobStartIndex = 0, jobEndIndex = totalIterations - (totalBatches * batchSize)](){
			for (int i = jobStartIndex; i < jobEndIndex; i++)
			{
				function();
			}
		}, timeInvestment));

		return BatchScheduleJob(jobs, timeInvestment);
	}

	template<JobFunction T, typename ...JobId>
	inline std::vector<int> JobSystem::Schedule(int totalIterations, int batchSize, JobPriority timeInvestment, T function, JobId ...dependencies)
	{
		std::vector<InternalJobBase*> jobs;

		//Schedule and create lambda for all job kinds
		int totalBatches = 0;
		int CurrentBatchEnd = totalIterations;
		while (CurrentBatchEnd > batchSize) {
			CurrentBatchEnd -= batchSize;
			jobs.push_back(new InternalJob(
				[function, jobStartIndex = totalIterations - ((totalBatches + 1) * batchSize), jobEndIndex = totalIterations - (totalBatches * batchSize)](){
				for (int i = jobStartIndex; i < jobEndIndex; i++)
				{
					function(i);
				}
			}, timeInvestment));
			totalBatches++;
		}

		//Create last job
		jobs.push_back(new InternalJob(
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

	template<JobFunction T>
	inline std::vector<int> JobSystem::Schedule(int totalIterations, int batchSize, JobPriority timeInvestment, T function, std::vector<int> dependencies)
	{
		if (jobIds.size() == 0) {
			return Schedule(totalIterations, batchSize, timeInvestment, function);
		}

		std::vector<InternalJobBase*> jobs;

		//Schedule and create lambda for all job kinds
		int totalBatches = 0;
		int CurrentBatchEnd = totalIterations;
		while (CurrentBatchEnd > batchSize) {
			CurrentBatchEnd -= batchSize;
			jobs.push_back(new InternalJob(
				[function, jobStartIndex = totalIterations - ((totalBatches + 1) * batchSize), jobEndIndex = totalIterations - (totalBatches * batchSize)](){
				for (int i = jobStartIndex; i < jobEndIndex; i++)
				{
					function(i);
				}
			}, timeInvestment));
			totalBatches++;
		}

		//Create last job
		jobs.push_back(new InternalJob(
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
	inline std::vector<int> JobSystem::Schedule(int startIndex, int endIndex, int batchSize, JobPriority timeInvestment, T function)
	{
		std::vector<InternalJobBase*> jobs;

		//Schedule and create lambda for all job kinds
		int totalBatches = 0;
		int endOfRange = endIndex - startIndex;
		int CurrentBatchEnd = endOfRange;
		while (CurrentBatchEnd > batchSize) {
			CurrentBatchEnd -= batchSize;
			jobs.push_back(new InternalJob(
				[function, jobStartIndex = endOfRange - ((totalBatches + 1) * batchSize), jobEndIndex = endOfRange - (totalBatches * batchSize)](){
				for (int i = jobStartIndex; i < jobEndIndex; i++)
				{
					function(i);
				}
			}, timeInvestment));
			totalBatches++;
		}

		//Create last job
		jobs.push_back(new InternalJob(
			[function, jobStartIndex = 0, jobEndIndex = endOfRange - (totalBatches * batchSize)](){
			for (int i = jobStartIndex; i < jobEndIndex; i++)
			{
				function(i);
			}
		}, timeInvestment));

		return BatchScheduleJob(jobs, timeInvestment);
	}
	template<ParrallelJobFunction T, typename ...JobId>
	inline std::vector<int> JobSystem::Schedule(int startIndex, int endIndex, int batchSize, JobPriority timeInvestment, T function, JobId ... dependencies)
	{
		std::vector<InternalJobBase*> jobs;

		//Schedule and create lambda for all job kinds
		int totalBatches = 0;
		int endOfRange = endIndex - startIndex;
		int CurrentBatchEnd = endOfRange;
		while (CurrentBatchEnd > batchSize) {
			CurrentBatchEnd -= batchSize;
			jobs.push_back(new InternalJob(
				[function, jobStartIndex = endOfRange - ((totalBatches + 1) * batchSize), jobEndIndex = endOfRange - (totalBatches * batchSize)](){
				for (int i = jobStartIndex; i < jobEndIndex; i++)
				{
					function(i);
				}
			}, timeInvestment));
			totalBatches++;
		}

		//Create last job
		jobs.push_back(new InternalJob(
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
	template<ParrallelJobFunction T>
	inline std::vector<int> JobSystem::Schedule(int startIndex, int endIndex, int batchSize, JobPriority timeInvestment, T function, std::vector<int> dependencies)
	{
		if (dependencies.size() == 0) {
			return Schedule(startIndex, endIndex, batchSize, timeInvestment, function);
		}

		std::vector<InternalJobBase*> jobs;

		//Schedule and create lambda for all job kinds
		int totalBatches = 0;
		int endOfRange = endIndex - startIndex;
		int CurrentBatchEnd = endOfRange;
		while (CurrentBatchEnd > batchSize) {
			CurrentBatchEnd -= batchSize;
			jobs.push_back(new InternalJob(
				[function, jobStartIndex = endOfRange - ((totalBatches + 1) * batchSize), jobEndIndex = endOfRange - (totalBatches * batchSize)](){
				for (int i = jobStartIndex; i < jobEndIndex; i++)
				{
					function(i);
				}
			}, timeInvestment));
			totalBatches++;
		}

		//Create last job
		jobs.push_back(new InternalJob(
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
	inline void JobSystem::WaitForJobCompletion(const std::vector<int> dependencies, const T& callback)
	{
		constexpr JobPriority dependencyCheckPriority = JobPriority::Low;

		std::function<void()> jobScheduler;
		jobScheduler = [this, callback, &jobScheduler, dependencies, dependencyCheckPriority]() {
			bool result = false;
			for (int i = 0; i < dependencies.size(); i++) {
				if (WaitForJobCompletion(dependencies.at(i), false)) {
					result = true;
					break;
				}
			}

			if (!result)
				Schedule(jobScheduler, dependencyCheckPriority);
			else {
				//When all dependencies are
				callback();
			}
		};

		Schedule(new InternalJob(jobScheduler, dependencyCheckPriority));
	}
}
