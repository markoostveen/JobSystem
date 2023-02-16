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
#include <functional>

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
		/// Will check if jobs are completed, if they are they will be removed from the given vector
		/// </summary>
		/// <param name="jobIds"></param>
		/// <returns>weather or not the job was completed</returns>
		bool AreJobsCompleted(std::vector<JobId>& jobIds);

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
		void ScheduleAfterJobCompletion(const std::vector<JobId>& dependencies, const JobPriority& dependencyPriority, typename JobSystemWithParametersJob<Args...>::Function function, Args... args);


		/// <summary>
		/// Wait until all jobs committed to the jobsystem have been completed
		/// </summary>
		void WaitForAllJobs();

		/// <summary>
		/// Executes a scheduled job
		/// </summary>
		void ExecuteJob();

		/// <summary>
		/// Executes a scheduled job
		/// </summary>
		void ExecuteJob(const JobPriority maxTimeInvestment);

		void StartAllWorkers();

		int GetWorkerCount();
		int GetActiveWorkerCount();
		int GetWorkerId(JobSystemWorker* worker);

		void ShowStats(bool option = true);

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
		std::vector<Job*> Shutdown();

		int ScheduleFutureJob(Job* const& newFutureJob);
		const std::vector<JobId> BatchScheduleJob(const std::vector<Job*>& newjobs, const JobPriority priority);
		const std::vector<int> BatchScheduleFutureJob(const std::vector<Job*>& newjobs);

		Job* TakeJobFromWorker(JobSystemWorker& worker, const JobPriority maxTimeInvestment = JobPriority::Low);


		bool IsJobCompleted(const JobId& jobId, JobSystemWorker*& jobWorker);

		void Cleanup();

		void OptimizePerformance();

		bool RescheduleWorkerJobs(JobSystemWorker& worker);
		void RescheduleWorkerJobsFromInActiveWorkers();

		bool TryRunJob(JobSystemWorker& worker, Job*& currentJob);
		void RunJob(JobSystemWorker& worker, Job*& currentJob);
		void RunJobInNewThread(JobSystemWorker& worker, Job*& currentJob);

		int GetRandomWorker();

		/// <summary>
		/// Schedules a job in a specific worker
		/// </summary>
		/// <param name="workerId"></param>
		/// <param name="newjob"></param>
		/// <returns></returns>
		JobId Schedule(const int& workerId, Job* const& newJob, const JobPriority priority);

		/// <summary>
		/// Schedules a job in a specific worker
		/// </summary>
		/// <param name="workerId"></param>
		/// <param name="newjob"></param>
		/// <returns></returns>
		JobId Schedule(JobSystemWorker& worker, Job* const& newJob, const JobPriority priority);

		void SafeRescheduleJob(Job* const& oldJob, JobSystemWorker& oldWorker);

		const std::vector<JobId> Schedule(const std::vector<int>& workerIds, const JobPriority priority, const std::vector<Job*>& newjobs);

		/// <summary>
		/// Take all scheduled jobs from all workers
		/// </summary>
		/// <returns></returns>
		std::vector<Job*> StealAllJobsFromWorkers();

		void MaybeOptimize();
		void MaybeHelpLowerQueue(const JobPriority& priority);

		std::atomic<int> _activeWorkerCount = 0;
		int _workerCount = 0;
		std::vector<JobSystemWorker> _workers;

		JbSystem::mutex _optimizePerformance;

		const int _maxJobExecutionsBeforePerformanceOptimization = 10;
		std::atomic<int> _jobExecutionsTillOptimization = _maxJobExecutionsBeforePerformanceOptimization;

		std::atomic<bool> _preventIncomingScheduleCalls;

		std::atomic<bool> _showStats;

		// Deadlock prevention
		JbSystem::mutex _spawnedThreadsMutex;
		std::unordered_map<std::thread::id, std::thread> _spawnedThreadsExecutingIgnoredJobs;
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

		ScheduleAfterJobCompletion(dependencyArray, priority,
			[](JobSystem* jobSystem, int workerId, Job* callbackJob, JobPriority priority)
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
			[](auto jobSystem, auto workerIds, auto callbackJobs, JobPriority priority)
			{
				jobSystem->Schedule(workerIds, callbackJobs, priority);
			}, this, workerIds, newjobs, priority);

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
	inline void JobSystem::ScheduleAfterJobCompletion(const std::vector<JobId>& dependencies, const JobPriority& dependencyPriority, typename JobSystemWithParametersJob<Args...>::Function function, Args... args)
	{
		assert(dependencies.size() > 0);

		struct DependenciesTag {};

		auto rescheduleJob = [](auto& rescheduleCallback, auto& retryCallback, Job*& callback, const JobPriority& reschedulePriority, JobSystem*& jobSystem, std::vector<JobId>*& dependencies, JobSystemWorker*& suggestedWorker) {

			// Prerequsites not met, queueing async job to check back later
			Job* rescheduleJob = JobSystem::CreateJobWithParams(retryCallback, rescheduleCallback, retryCallback,
				callback, reschedulePriority, jobSystem, dependencies, suggestedWorker);
            rescheduleJob->SetEmptyStackRequired(true);

			jobSystem->RescheduleWorkerJobsFromInActiveWorkers();
			jobSystem->Schedule(rescheduleJob, JobPriority::Low);
		};

		auto jobScheduler = [](auto rescheduleCallback, auto retryCallback, Job* callback, JobPriority reschedulePriority, JobSystem* jobSystem, std::vector<JobId>* dependencies, JobSystemWorker* suggestedJobWorker) -> void {
			for (size_t i = 0; i < dependencies->size(); i++) {
				if (!jobSystem->IsJobCompleted(dependencies->at(i), suggestedJobWorker)) {

					rescheduleCallback(rescheduleCallback, retryCallback, callback, reschedulePriority, jobSystem, dependencies, suggestedJobWorker);
					return;
				}
			}

			//When all dependencies are completed
			jobSystem->Schedule(callback, reschedulePriority);
			dependencies->~vector();
			boost::singleton_pool<DependenciesTag, sizeof(std::vector<JobId>)>::free(dependencies);
		};

		void* location = boost::singleton_pool<DependenciesTag, sizeof(std::vector<JobId>)>::malloc();
		auto jobDependencies = new(location) std::vector<JobId>({ dependencies });

		Job* callbackJob = JobSystem::CreateJobWithParams(function, std::forward<Args>(args)...);
		callbackJob->SetIgnoreCallback([jobDependencies = std::vector<JobId>({ dependencies })](const JobId& proposedJobId) {
				for (const auto& dependencyId : jobDependencies) {
					if (dependencyId == proposedJobId)
						return true;
				}
				return false;
			}
		);

		// run in sync, if dependencies have already completed we can immediatly schedule it
		jobScheduler(rescheduleJob, jobScheduler, callbackJob, dependencyPriority, this, jobDependencies, nullptr);
	}
}
