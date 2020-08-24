#pragma once

#include "InternalJob.h"
#include "JobSystemThread.h"

#include <thread>
#include <atomic>
#include <mutex>
#include <set>
#include <unordered_set>
#include <array>

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
		static void ReConfigure(int threadCount = std::thread::hardware_concurrency() - 1) {
			GetInstance()->Start(threadCount);
		}

		template<typename JobFunction>
		static int Schedule(JobFunction function, JobTime timeInvestment);
		template<typename JobFunction, typename ...JobId>
		static int Schedule(JobFunction function, JobTime timeInvestment, JobId... jobIds);
		template<typename JobFunction>
		static int Schedule(JobFunction function, std::vector<int> jobIds, JobTime timeInvestment);

		/// <summary>
		/// Caller will help execute jobs until the job with specified Id is completed
		/// </summary>
		/// <param name="jobId"></param>
		static void WaitForJobCompletion(int jobId);

		/// <summary>
		/// Executes a scheduled job
		/// </summary>
		static void ExecuteJob();

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
		/// <summary>
		/// Gives the job to one of the workers for execution
		/// </summary>
		/// <param name="newjob"></param>
		/// <returns></returns>
		int ScheduleJob(InternalJobBase* newjob);

		InternalJobBase* StealJobFromWorker(JobTime maxTimeInvestment = JobTime::Long);

		/// <summary>
		/// Finishes job and cleans up after
		/// </summary>
		/// <param name="job"></param>
		void FinishJob(InternalJobBase*& job);

		/// <summary>
		/// Take all scheduled jobs from all workers
		/// </summary>
		/// <returns></returns>
		std::vector<InternalJobBase*> StealAllJobsFromWorkers();

		/// <summary>
		/// Schedules the jobs into active workers
		/// </summary>
		/// <param name="jobs"></param>
		void RescheduleJobs(std::vector<InternalJobBase*>& jobs);

		std::mutex _reconfigureLock;

		std::mutex _jobsMutex;
		std::unordered_set<int> _scheduledJobs;

		int _workerCount = 0;
		std::vector<JobSystemWorker> _workers;

		int _maxSchedulesTillMaintainance = 100;
		std::atomic<int> _schedulesTillMaintainance = _maxSchedulesTillMaintainance;
	};

	template<typename JobFunction>
	inline int JobSystem::Schedule(JobFunction function, JobTime length)
	{
		return GetInstance()->ScheduleJob(new InternalJob(function, length));
	}

	template<typename JobFunction, typename	 ...JobId>
	inline int JobSystem::Schedule(JobFunction function, JobTime timeInvestment, JobId... jobIds)
	{
		if (timeInvestment == JobTime::Short)
			timeInvestment = JobTime::Medium;

		auto jobLambda = [function, ... jobIds = std::forward<JobId>(jobIds)]() {
			std::array<int, sizeof...(JobId)> dependencyArray = { jobIds ... };
			for (int i = 0; i < dependencyArray.size(); i++) {
				WaitForJobCompletion(dependencyArray.at(i));
			}

			function();
		};

		return Schedule(jobLambda, timeInvestment);
	}

	template<typename JobFunction>
	inline int JobSystem::Schedule(JobFunction function, std::vector<int> jobIds, JobTime timeInvestment)
	{
		if (timeInvestment == JobTime::Short)
			timeInvestment = JobTime::Medium;

		auto jobLambda = [function, jobIds]() {
			for (int i = 0; i < jobIds.size(); i++) {
				WaitForJobCompletion(jobIds[i]);
			}

			function();
		};

		return Schedule(jobLambda, timeInvestment);
	}
}
