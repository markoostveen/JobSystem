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
	class JobSystem {
	public:
		JobSystem(const int threadCount = std::thread::hardware_concurrency() - 1);
		~JobSystem();

		/// <summary>
		/// Use to modify properties of the jobSystem
		/// Start or Restart the jobsystem, with the desired amount of workers
		/// Note, currently scheduled jobs will be rescheduled, during rescheduling, priority order isn't guarenteed
		/// </summary>
		/// <param name="threadCount"></param>
		void ReConfigure(const int threadCount = std::thread::hardware_concurrency() - 1);

		//Single
		template<class ...Args>
		static JobBase* CreateJob(const JobPriority priority, typename Job<Args...>::Function function, Args... args);
		static JobBase* CreateJob(const JobPriority priority, void (*function)());

		//Parallel
		template<class ...Args>
		static std::vector<JobBase*>* CreateParallelJob(const JobPriority priority, int startIndex, int endIndex, int batchSize, typename Job<int, Args...>::Function function, Args... args);
		static std::vector<JobBase*>* CreateParallelJob(const JobPriority priority, int startIndex, int endIndex, int batchSize, void (*function)(int));

		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		const int Schedule(JobBase* newjob);
		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		template<typename ...JobId>
		const int Schedule(JobBase* job, const JobId... dependencies);
		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		const int Schedule(JobBase* job, const std::vector<int> dependencies);

		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		const std::vector<int> Schedule(std::vector<JobBase*>* newjobs);
		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		template<typename ...JobId>
		const std::vector<int> Schedule(std::vector<JobBase*>* newjobs, const JobId... dependencies);
		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		const std::vector<int> Schedule(std::vector<JobBase*>* newjobs, const std::vector<int> dependencies);

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
		bool WaitForJobCompletion(int jobId, const JobPriority maximumHelpEffort = JobPriority::Low);

		/// <summary>
		/// Caller will help execute jobs until the job with specified Id is completed
		/// </summary>
		/// <param name="jobId"></param>
		/// <param name="maxMicroSecondsToWait">when elapsed function will return false if job hasn't been completed yet, 0 = infinity</param>
		/// <returns>weather or not the job was completed in time</returns>
		bool WaitForJobCompletion(int jobId, int maxMicroSecondsToWait, const JobPriority maximumHelpEffort = JobPriority::Low);

		/// <summary>
		/// Block execution until given jobs have been completed, this operation is blocking
		/// </summary>
		/// <param name="jobIds"></param>
		void WaitForJobCompletion(std::vector<int>& jobIds, const JobPriority maximumHelpEffort = JobPriority::Low);

		/// <summary>
		/// wait for jobs to complete, then execute function
		/// </summary>
		/// <param name="dependencies">jobs to wait for before scheduling the 'newJob'</param>
		/// <param name="callback">function to execute after jobs have been completed</param>
		/// <returns></returns>
		template<class ...Args>
		void WaitForJobCompletion(const std::vector<int> dependencies, typename Job<Args...>::Function function, Args... args);

		/// <summary>
		/// Executes a scheduled job
		/// </summary>
		void ExecuteJob(const JobPriority maxTimeInvestment = JobPriority::Low);

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
		std::vector<JobBase*>* Shutdown();

		int ScheduleFutureJob(const JobBase* newFutureJob);
		std::vector<int> BatchScheduleJob(const std::vector<JobBase*>* newjobs);
		std::vector<int> BatchScheduleFutureJob(const std::vector<JobBase*>* newjobs);

		void ExecuteJobFromWorker(const JobPriority maxTimeInvestment = JobPriority::Low);

		void Cleanup();

		/// <summary>
		/// Take all scheduled jobs from all workers
		/// </summary>
		/// <returns></returns>
		std::vector<JobBase*>* StealAllJobsFromWorkers();

		std::mutex _jobsMutex;
		std::unordered_set<int> _scheduledJobs;

		//Jobs that have been completed
		//Note: they are ordered in terms of thread Ids, this way we can be sure to have access at the same time

		int _workerCount = 0;
		std::vector<JobSystemWorker> _workers;

		const int _maxSchedulesTillMaintainance = 1000;
		std::atomic<int> _schedulesTillMaintainance = _maxSchedulesTillMaintainance;
	};

	template<class ...Args>
	JobBase* JobSystem::CreateJob(const JobPriority priority, typename Job<Args...>::Function function, Args... args)
	{
		return new Job<Args...>(priority, function, std::forward<Args>(args)...);
	}

	template<class ...Args>
	std::vector<JobBase*>* JobSystem::CreateParallelJob(const JobPriority priority, int startIndex, int endIndex, int batchSize, typename Job<int, Args...>::Function function, Args ...args)
	{
		auto jobs = new std::vector<JobBase*>();

		auto parallelFunction = [](typename Job<int, Args...>::Function callback, int startIndex, int endIndex, Args ...args)
		{
			for (int i = startIndex; i < endIndex; i++)
			{
				callback(i, std::forward<Args>(args)...);
			}
		};

		int jobStartIndex;
		int jobEndIndex;

		//Schedule and create lambda for all job kinds
		int totalBatches = 0;
		int endOfRange = endIndex - startIndex;
		int CurrentBatchEnd = endOfRange;
		while (CurrentBatchEnd > batchSize) {
			CurrentBatchEnd -= batchSize;

			jobStartIndex = startIndex + endOfRange - ((totalBatches + 1) * batchSize);
			jobEndIndex = startIndex + endOfRange - (totalBatches * batchSize);

			jobs->emplace_back(CreateJob(priority, parallelFunction, function, jobStartIndex, jobEndIndex, std::forward<Args>(args)...));
			totalBatches++;
		}

		jobStartIndex = startIndex;
		jobEndIndex = startIndex + endOfRange - (totalBatches * batchSize);

		//Create last job
		jobs->emplace_back(CreateJob(priority, parallelFunction, function, jobStartIndex, jobEndIndex, std::forward<Args>(args)...));

		return jobs;
	}

	template<typename ...JobId>
	inline const int JobSystem::Schedule(JobBase* job, const JobId... dependencies)
	{
		const std::vector<int> dependencyArray = { dependencies ... };

		//Schedule jobs in the future, then when completed, schedule them for inside workers
		const int jobId = ScheduleFutureJob(job);

		WaitForJobCompletion(dependencyArray,
			[](auto jobSystem, auto callbackJob)
			{
				jobSystem->Schedule(callbackJob);
			}, this, job);
		return jobId;
	}

	template<typename ...JobId>
	inline const std::vector<int> JobSystem::Schedule(std::vector<JobBase*>* newjobs, const JobId ...dependencies)
	{
		const std::vector<int> dependencyArray = { dependencies ... };

		//Schedule jobs in the future, then when completed, schedule them for inside workers
		auto jobIds = BatchScheduleFutureJob(newjobs);

		WaitForJobCompletion(dependencyArray,
			[](auto jobSystem, auto callbackJobs)
			{
				jobSystem->Schedule(callbackJobs);
			}, this, newjobs);

		return jobIds;
	}

	template<class ...Args>
	inline void JobSystem::WaitForJobCompletion(const std::vector<int> dependencies, typename Job<Args...>::Function function, Args... args)
	{
		auto jobScheduler = [](auto retryJob, auto callback, JobSystem* jobSystem, std::vector<int>* dependencies) -> void {
			for (size_t i = 0; i < dependencies->size(); i++) {
				if (!jobSystem->IsJobCompleted(dependencies->at(i))) {
					// Prerequsites not met, queueing async job to check back later
					constexpr JobPriority dependencyCheckPriority = JobPriority::Low;
					auto rescheduleJob = JobSystem::CreateJob(dependencyCheckPriority, retryJob,
						retryJob, callback, jobSystem, dependencies);
					jobSystem->Schedule(rescheduleJob);
					return;
				}
			}

			//When all dependencies are completed
			jobSystem->Schedule(callback);
			delete dependencies;
		};

		auto callbackJob = JobSystem::CreateJob(JobPriority::High, function, std::forward<Args>(args)...);
		auto jobDependencies = new std::vector<int>();
		jobDependencies->insert(jobDependencies->begin(), dependencies.begin(), dependencies.end());

		// run in sync
		jobScheduler(jobScheduler, callbackJob, this, jobDependencies);
	}
}
