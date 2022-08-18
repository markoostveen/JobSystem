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
#include <cassert>

namespace JbSystem {

	class JobSystem {

		friend class JobSystemWorker;

		typedef void(*WorkerThreadLoop)(JobSystemWorker* worker);

	public:
		JobSystem(const int threadCount = std::thread::hardware_concurrency() - 1, WorkerThreadLoop workerLoop = [](JobSystemWorker* worker) { worker->ThreadLoop(); });
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
		static Job* CreateJobWithParams(typename JobSystemWithParametersJob<Args...>::Function function, Args... args);
		static Job* CreateJob(void (*function)());

		static void DestroyNonScheduledJob(Job*& job);

		//Parallel
		template<class ...Args>
		static std::vector<Job*> CreateParallelJob(int startIndex, int endIndex, int batchSize, typename JobSystemWithParametersJob<const int&, Args...>::Function function, Args... args);
		static std::vector<Job*> CreateParallelJob(int startIndex, int endIndex, int batchSize, void (*function)(const int&));

		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		JobId Schedule(Job* const& newjob, const JobPriority priority);
		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		template<typename ...DependencyJobId>
		JobId Schedule(Job* const& job, const JobPriority priority, const DependencyJobId... dependencies);
		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		JobId Schedule(Job* const& job, const JobPriority priority, const std::vector<JobId>& dependencies);

		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		const std::vector<JobId> Schedule(const std::vector<Job*>& newjobs, const JobPriority priority);
		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		template<typename ...DependencyJobId>
		const std::vector<JobId> Schedule(const std::vector<Job*>& newjobs, const JobPriority priority, const DependencyJobId... dependencies);
		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		const std::vector<JobId> Schedule(const std::vector<Job*>& newjobs, const JobPriority priority, const std::vector<JobId>& dependencies);

		/// <summary>
		/// 
		/// </summary>
		/// <param name="jobId"></param>
		/// <returns>weather or not the job was completed</returns>
		bool IsJobCompleted(const JobId& jobId);

		/// <summary>
		/// 
		/// </summary>
		/// <param name="jobId"></param>
		/// <returns>weather or not job is currently scheduled and isn't executed</returns>
		bool IsJobScheduled(const JobId& jobId);

		/// <summary>
		/// Caller will help execute jobs until the job with specified Id is completed
		/// </summary>
		/// <param name="jobId"></param>
		/// <param name="maxMicroSecondsToWait">when elapsed function will return false if job hasn't been completed yet, 0 = infinity</param>
		/// <returns>weather or not the job was completed in time</returns>
		void WaitForJobCompletion(const JobId& jobId, JobPriority maximumHelpEffort = JobPriority::Low);

		/// <summary>
		/// Caller will help execute jobs until the job with specified Id is completed
		/// </summary>
		/// <param name="jobId"></param>
		/// <param name="maxMicroSecondsToWait">when elapsed function will return false if job hasn't been completed yet, 0 = infinity</param>
		/// <returns>weather or not the job was completed in time</returns>
		bool WaitForJobCompletion(const JobId& jobId, int maxMicroSecondsToWait, const JobPriority maximumHelpEffort = JobPriority::Low);

		/// <summary>
		/// Block execution until given jobs have been completed, this operation is blocking
		/// </summary>
		/// <param name="jobIds"></param>
		void WaitForJobCompletion(const std::vector<JobId>& jobIds, const JobPriority maximumHelpEffort = JobPriority::Low);

		/// <summary>
		/// wait for jobs to complete, then execute function
		/// </summary>
		/// <param name="dependencies">jobs to wait for before scheduling the 'newJob'</param>
		/// <param name="callback">function to execute after jobs have been completed</param>
		/// <returns></returns>
		template<class ...Args>
		void WaitForJobCompletion(const std::vector<JobId>& dependencies, typename JobSystemWithParametersJob<Args...>::Function function, Args... args);

		/// <summary>
		/// Executes a scheduled job
		/// </summary>
		void ExecuteJob(const JobPriority maxTimeInvestment = JobPriority::Low);

		int GetWorkerCount();
		int GetActiveWorkerCount();
		int GetWorkerId(JobSystemWorker* worker);

		/// <summary>
		/// Get singleton instance
		/// </summary>
		/// <returns></returns>
		static JobSystem* GetInstance();

		/// <summary>
		/// is the job system active or not
		/// </summary>
		bool Active = false;
		WorkerThreadLoop WorkerLoop;
	private:

		/// <summary>
		/// Shutdown all worker threads
		/// </summary>
		/// <returns> vector of all remaining jobs </returns>
		std::shared_ptr<std::vector<Job*>> Shutdown();

		int ScheduleFutureJob(Job* const& newFutureJob);
		const std::vector<JobId> BatchScheduleJob(const std::vector<Job*>& newjobs, const JobPriority priority);
		const std::vector<int> BatchScheduleFutureJob(const std::vector<Job*>& newjobs);

		Job* TakeJobFromWorker(JobSystemWorker& worker, const JobPriority maxTimeInvestment = JobPriority::Low);

		void Cleanup();

		void OptimizePerformance();

		void StartAllWorkers();

		bool RescheduleWorkerJobs(JobSystemWorker& worker);
		void RescheduleWorkerJobsFromInActiveWorkers();

		int GetRandomWorker();

		/// <summary>
		/// Schedules a job in a specific worker
		/// </summary>
		/// <param name="workerId"></param>
		/// <param name="newjob"></param>
		/// <returns></returns>
		JobId Schedule(const int& workerId, Job* const& newjob, const JobPriority priority);

		void SafeRescheduleJob(Job* const& oldJob, JobSystemWorker& oldWorker);

		const std::vector<JobId> Schedule(const std::vector<int>& workerIds, const JobPriority priority, const std::vector<Job*>& newjobs);

		/// <summary>
		/// Take all scheduled jobs from all workers
		/// </summary>
		/// <returns></returns>
		std::shared_ptr<std::vector<Job*>> StealAllJobsFromWorkers();

		std::atomic<int> _activeWorkerCount = 0;
		int _workerCount = 0;
		std::vector<JobSystemWorker> _workers;

		JbSystem::mutex _optimizePerformance;

		const int _maxSchedulesTillMaintainance = 20;
		std::atomic<int> _schedulesTillMaintainance = _maxSchedulesTillMaintainance;

	};

	template<class ...Args>
	inline Job* JobSystem::CreateJobWithParams(typename JobSystemWithParametersJob<Args...>::Function function, Args... args)
	{
		void* location = boost::singleton_pool<typename JobSystemWithParametersJob<Args...>::Tag, sizeof(JobSystemWithParametersJob<Args...>)>::malloc();
		auto deconstructorCallback = [](JobSystemWithParametersJob<Args...>* const& job)
		{
			job->~JobSystemWithParametersJob<Args...>();
			boost::singleton_pool<typename JobSystemWithParametersJob<Args...>::Tag, sizeof(JobSystemWithParametersJob<Args...>)>::free(job);
		};
		return new (location) JobSystemWithParametersJob<Args...>(function, deconstructorCallback, std::forward<Args>(args)...);
	}

	template<class ...Args>
	inline std::vector<Job*> JobSystem::CreateParallelJob(int startIndex, int endIndex, int batchSize, typename JobSystemWithParametersJob<const int&, Args...>::Function function, Args ...args)
	{
		if (batchSize < 1)
			batchSize = 1;



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
			totalBatches++;
		}

		auto jobs = std::vector<Job*>();
		jobs.reserve(totalBatches + 1);

		for (int i = 0; i < totalBatches; i++)
		{
			jobStartIndex = startIndex + (i * batchSize);
			jobEndIndex = startIndex + ((i + 1) * batchSize);

			jobs.emplace_back(CreateJobWithParams(parallelFunction, function, jobStartIndex, jobEndIndex, args...));
		}

		jobStartIndex = startIndex + (totalBatches * batchSize);
		jobEndIndex = endIndex;

		//Create last job
		jobs.emplace_back(CreateJobWithParams(parallelFunction, function, jobStartIndex, jobEndIndex, args...));

		return jobs;
	}

	template<typename ...DependencyJobId>
	inline JobId JobSystem::Schedule(Job* const& job, const JobPriority priority, const DependencyJobId... dependencies)
	{
		const std::vector<JobId> dependencyArray = { dependencies ... };

		//Schedule jobs in the future, then when completed, schedule them for inside workers
		const int workerId = ScheduleFutureJob(job);

		WaitForJobCompletion(dependencyArray,
			[](auto jobSystem, auto workerId, auto callbackJob, auto priority)
			{
				jobSystem->Schedule(workerId, callbackJob, priority);
			}, this, workerId, job, priority);
		return job->GetId();
	}

	template<typename ...DependencyJobId>
	inline const std::vector<JobId> JobSystem::Schedule(const std::vector<Job*>& newjobs, const JobPriority priority, const DependencyJobId ...dependencies)
	{
		const std::vector<JobId> dependencyArray = { dependencies... };

		//Schedule jobs in the future, then when completed, schedule them for inside workers
		auto workerIds = BatchScheduleFutureJob(newjobs);

		WaitForJobCompletion(dependencyArray,
			[](auto jobSystem, auto workerIds, auto callbackJobs)
			{
				jobSystem->Schedule(workerIds, callbackJobs);
			}, this, workerIds, newjobs);

		std::vector<JobId> jobIds;
		size_t jobCount = newjobs.size();
		jobIds.reserve(jobCount);
		for (size_t i = 0; i < jobCount; i++)
		{
			jobIds.emplace_back(newjobs[i]->GetId());
		}

		return jobIds;
	}

	template<class ...Args>
	inline void JobSystem::WaitForJobCompletion(const std::vector<JobId>& dependencies, typename JobSystemWithParametersJob<Args...>::Function function, Args... args)
	{
		struct DependenciesTag {};

		auto jobScheduler = [](auto retryJob, Job* callback, JobSystem* jobSystem, std::vector<JobId>* dependencies) -> void {
			for (size_t i = 0; i < dependencies->size(); i++) {
				if (!jobSystem->IsJobCompleted(dependencies->at(i))) {
					if (i > 0) { // shrink the dependency array to reduce amount of jobs that need to be checked
						dependencies->erase(dependencies->begin(), dependencies->begin() + i);
					}

					// Prerequsites not met, queueing async job to check back later
					const JobPriority dependencyCheckPriority = JobPriority::Low;
					Job* rescheduleJob = JobSystem::CreateJobWithParams(retryJob,
						retryJob, callback, jobSystem, dependencies);


					jobSystem->RescheduleWorkerJobsFromInActiveWorkers();
					jobSystem->Schedule(rescheduleJob, dependencyCheckPriority);
					return;
				}
			}

			//When all dependencies are completed
			jobSystem->Schedule(callback, JobPriority::High);
			dependencies->~vector();
			boost::singleton_pool<DependenciesTag, sizeof(std::vector<JobId>)>::free(dependencies);
		};

		void* location = boost::singleton_pool<DependenciesTag, sizeof(std::vector<JobId>)>::malloc();
		auto jobDependencies = new(location) std::vector<JobId>({ dependencies });

		Job* callbackJob = JobSystem::CreateJobWithParams(function, std::forward<Args>(args)...);

		// run in sync, if dependencies have already completed we can immediatly schedule it
		jobScheduler(jobScheduler, callbackJob, this, jobDependencies);
	}
}
