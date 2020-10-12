#pragma once

#include "Job.h"
#include "WorkerThread.h"

#include "boost/pool/singleton_pool.hpp"

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
		static Job* CreateJob(const JobPriority priority, typename JobSystemWithParametersJob<Args...>::Function function, Args... args);
		template<class ...Args>
		inline static Job* CreateJob(typename JobSystemWithParametersJob<Args...>::Function function, Args... args)
		{
			return CreateJob(JobPriority::Normal, function, std::forward<Args>(args)...);
		}

		static Job* CreateJob(const JobPriority priority, void (*function)());
		inline static Job* CreateJob(void (*function)()) {
			return CreateJob(JobPriority::Normal, function);
		}

		//Parallel
		template<class ...Args>
		static std::shared_ptr<std::vector<Job*>> CreateParallelJob(const JobPriority priority, int startIndex, int endIndex, int batchSize, typename JobSystemWithParametersJob<const int&, Args...>::Function function, Args... args);
		template<class ...Args>
		static std::shared_ptr<std::vector<Job*>> CreateParallelJob(int startIndex, int endIndex, int batchSize, typename JobSystemWithParametersJob<const int&, Args...>::Function function, Args... args) {
			return CreateParallelJob(JobPriority::Normal, startIndex, endIndex, batchSize, function, std::forward<Args>(args)...);
		}
		static std::shared_ptr<std::vector<Job*>> CreateParallelJob(const JobPriority priority, int startIndex, int endIndex, int batchSize, void (*function)(const int&));
		static std::shared_ptr<std::vector<Job*>> CreateParallelJob(int startIndex, int endIndex, int batchSize, void (*function)(const int&)) {
			return CreateParallelJob(JobPriority::Normal, startIndex, endIndex, batchSize, function);
		}

		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		const int Schedule(Job* newjob);
		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		template<typename ...JobId>
		const int Schedule(Job* job, const JobId... dependencies);
		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		const int Schedule(Job* job, const std::vector<int> dependencies);

		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		const std::vector<int> Schedule(std::shared_ptr<std::vector<Job*>> newjobs);
		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		template<typename ...JobId>
		const std::vector<int> Schedule(std::shared_ptr<std::vector<Job*>> newjobs, const JobId... dependencies);
		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		const std::vector<int> Schedule(std::shared_ptr<std::vector<Job*>> newjobs, const std::vector<int> dependencies);

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
		void WaitForJobCompletion(const std::vector<int>& dependencies, typename JobSystemWithParametersJob<Args...>::Function function, Args... args);

		/// <summary>
		/// Executes a scheduled job
		/// </summary>
		void ExecuteJob(const JobPriority maxTimeInvestment = JobPriority::Low);

		/// <summary>
		/// Get singleton instance
		/// </summary>
		/// <returns></returns>
		static JobSystem* GetInstance();

		/// <summary>
		/// is the job system active or not
		/// </summary>
		bool Active = false;
	private:

		/// <summary>
		/// Shutdown all worker threads
		/// </summary>
		/// <returns> vector of all remaining jobs </returns>
		std::vector<Job*>* Shutdown();

		int ScheduleFutureJob(const Job* newFutureJob);
		std::vector<int> BatchScheduleJob(const std::vector<Job*>* newjobs);
		std::vector<int> BatchScheduleFutureJob(const std::vector<Job*>* newjobs);

		void ExecuteJobFromWorker(const JobPriority maxTimeInvestment = JobPriority::Low);

		void Cleanup();

		int GetRandomWorker();

		/// <summary>
		/// Schedules a job in a specific worker
		/// </summary>
		/// <param name="workerId"></param>
		/// <param name="newjob"></param>
		/// <returns></returns>
		const int Schedule(const int& workerId, Job* newjob);

		const std::vector<int> Schedule(const std::vector<int>& workerIds, std::shared_ptr<std::vector<Job*>> newjobs);

		/// <summary>
		/// Take all scheduled jobs from all workers
		/// </summary>
		/// <returns></returns>
		std::vector<Job*>* StealAllJobsFromWorkers();

		int _workerCount = 0;
		std::vector<JobSystemWorker> _workers;

		const int _maxSchedulesTillMaintainance = 1000;
		std::atomic<int> _schedulesTillMaintainance = _maxSchedulesTillMaintainance;
	};

	template<class ...Args>
	inline Job* JobSystem::CreateJob(const JobPriority priority, typename JobSystemWithParametersJob<Args...>::Function function, Args... args)
	{
		void* location = boost::singleton_pool<typename JobSystemWithParametersJob<Args...>::Tag, sizeof(JobSystemWithParametersJob<Args...>)>::malloc();
		auto deconstructorCallback = [](JobSystemWithParametersJob<Args...>* job)
		{
			boost::singleton_pool<typename JobSystemWithParametersJob<Args...>::Tag, sizeof(JobSystemWithParametersJob<Args...>)>::free(job);
		};
		return new (location) JobSystemWithParametersJob<Args...>(priority, function, deconstructorCallback, std::forward<Args>(args)...);
	}

	template<class ...Args>
	inline std::shared_ptr<std::vector<Job*>> JobSystem::CreateParallelJob(const JobPriority priority, int startIndex, int endIndex, int batchSize, typename JobSystemWithParametersJob<const int&, Args...>::Function function, Args ...args)
	{
		auto jobs = std::make_shared<std::vector<Job*>>();

		auto parallelFunction = [](typename JobSystemWithParametersJob<const int&, Args...>::Function callback, int startIndex, int endIndex, Args ...args)
		{
			for (int& i = startIndex; i < endIndex; i++)
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
	inline const int JobSystem::Schedule(Job* job, const JobId... dependencies)
	{
		const std::vector<int> dependencyArray = { dependencies ... };

		//Schedule jobs in the future, then when completed, schedule them for inside workers
		const int workerId = ScheduleFutureJob(job);

		WaitForJobCompletion(dependencyArray,
			[](auto jobSystem, auto workerId, auto callbackJob)
			{
				jobSystem->Schedule(workerId, callbackJob);
			}, this, workerId, job);
		return job->GetId();
	}

	template<typename ...JobId>
	inline const std::vector<int> JobSystem::Schedule(std::shared_ptr<std::vector<Job*>> newjobs, const JobId ...dependencies)
	{
		const std::vector<int> dependencyArray = { dependencies ... };

		//Schedule jobs in the future, then when completed, schedule them for inside workers
		auto workerIds = BatchScheduleFutureJob(newjobs.get());

		WaitForJobCompletion(dependencyArray,
			[](auto jobSystem, auto workerIds, auto callbackJobs)
			{
				jobSystem->Schedule(workerIds, callbackJobs);
			}, this, workerIds, newjobs);

		std::vector<int> jobIds;
		size_t jobCount = newjobs->size();
		jobIds.reserve(jobCount);
		for (size_t i = 0; i < jobCount; i++)
		{
			jobIds.emplace_back(newjobs->at(i)->GetId());
		}

		return jobIds;
	}

	template<class ...Args>
	inline void JobSystem::WaitForJobCompletion(const std::vector<int>& dependencies, typename JobSystemWithParametersJob<Args...>::Function function, Args... args)
	{
		struct DependenciesTag {};

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
			dependencies->~vector();
			boost::singleton_pool<DependenciesTag, sizeof(std::vector<int>)>::free(dependencies);
		};

		void* location = boost::singleton_pool<DependenciesTag, sizeof(std::vector<int>)>::malloc();
		auto jobDependencies = new(location) std::vector<int>();
		jobDependencies->reserve(dependencies.size());
		jobDependencies->insert(jobDependencies->begin(), dependencies.begin(), dependencies.end());

		auto callbackJob = JobSystem::CreateJob(JobPriority::High, function, std::forward<Args>(args)...);

		// run in sync, if dependencies have already completed we can immediatly schedule it
		jobScheduler(jobScheduler, callbackJob, this, jobDependencies);
	}
}
