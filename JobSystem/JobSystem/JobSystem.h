#pragma once

#include "Job.h"
#include "JobSystemThread.h"

#include <thread>
#include <atomic>
#include <mutex>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <array>

namespace JbSystem {
	class JobSystemWorker;

	class JobSystem {
	public:
		JobSystem(int threadCount);
		~JobSystem();

		template<typename JobFunction>
		static const JobBase* Schedule(JobFunction function);
		template<typename JobFunction, typename ...JobType>
		static const JobBase* Schedule(JobFunction function, JobType... dependency);
		template<typename JobFunction>
		static const JobBase* Schedule(JobFunction function, std::vector<const JobBase*>& dependencies);

		static const void Schedule(const JobBase* job);

		/// <summary>
		/// Find a suitable job, caller is expected to do the work, and destory the object
		/// </summary>
		/// <returns></returns>
		static JobBase* GetWork();
		static void CompleteJob(const JobBase* job);

	private:
		static JobSystem* GetInstance();

		JobBase* FindWork();

		void scheduleJob(JobBase* newjob);
		void scheduleFutureJob(JobBase* newjob);
		void AddDependencyToJob(int id, int dependency);
		void ThrowAwayCompletedJobs();

		std::atomic<int> _currentWorkItem;
		std::mutex _currentWorkItemMutex;

		int _maxJobsTillRemove;
		int _jobsTillRemove;
		std::mutex _jobVectorMutex;
		std::mutex _dependencyJobVectorMutex;
		std::vector<JobBase*> _Jobs;
		std::unordered_map<int, int> _JobIdToJobIndex;//use to lookup from job ID to get a Job from the _Jobs
		std::unordered_set<int> _JobCompletion;
		std::vector<JobBase*> _UnscheduledJobs;
		std::unordered_map<int, int> _JobIdToUnscheduledJobIndex;//use to lookup from job ID to get a Job from the _UnscheduledJobs
		std::unordered_multimap<int, int> _dependencyOpenJobMap;
		std::unordered_multimap<int, int> _openJobDependencyMap;
		std::unordered_set<int> _blockedJobIds;

		int _workerCount;
		std::vector<JobSystemWorker> _workers;
	};

	template<typename JobFunction>
	inline const JobBase* JobSystem::Schedule(JobFunction function)
	{
		auto newJob = new Job<JobFunction>(&function);
		JobSystem* jobSystem = GetInstance();
		jobSystem->scheduleJob(newJob);
		return newJob;
	}

	template<typename JobFunction, typename	 ...JobType>
	inline const JobBase* JobSystem::Schedule(JobFunction function, JobType... dependencies)
	{
		auto newJob = new Job<JobFunction>(&function);
		JobSystem* jobSystem = GetInstance();
		int newJobId = newJob->GetId();

		std::array<const JobBase*, sizeof...(JobType)> dependencyArray = { { dependencies ... } };

		for (int i = 0; i < dependencyArray.size(); i++) {
			jobSystem->AddDependencyToJob(newJobId, dependencyArray.at(i)->GetId());
		}

		jobSystem->_jobVectorMutex.lock();
		jobSystem->_blockedJobIds.insert(newJobId);
		jobSystem->_jobVectorMutex.unlock();
		jobSystem->scheduleFutureJob(newJob);

		return newJob;
	}
	template<typename JobFunction>
	inline const JobBase* JobSystem::Schedule(JobFunction function, std::vector<const JobBase*>& dependencies)
	{
		auto newJob = new Job<JobFunction>(&function);
		int newJobId = newJob->GetId();
		JobSystem* jobSystem = GetInstance();

		for (size_t i = 0; i < dependencies.size(); i++)
		{
			jobSystem->AddDependencyToJob(newJobId, dependencies[i]->GetId());
		}

		jobSystem->_jobVectorMutex.lock();
		jobSystem->_blockedJobIds.insert(newJobId);
		jobSystem->_jobVectorMutex.unlock();
		jobSystem->scheduleFutureJob(newJob);
		return newJob;
	}
}
