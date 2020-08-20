#include "JobSystem.h"

#include <functional>
#include <iostream>
#include <algorithm>

using namespace JbSystem;

JobSystem* JobSystemSingleton = new JobSystem(std::thread::hardware_concurrency() - 1);

JobSystem::JobSystem(int threadCount)
{
	_workerCount = threadCount;
	_Jobs.reserve(threadCount * size_t(4));
	_JobIdToJobIndex.reserve(threadCount * size_t(4));
	_JobIdToUnscheduledJobIndex.reserve(threadCount * size_t(4));
	_UnscheduledJobs.reserve(threadCount * size_t(4));
	_blockedJobIds.reserve(threadCount * size_t(4));
	_workers.reserve(threadCount);
	for (int i = 0; i < _workerCount; i++)
	{
		// set callback function for worker threads to call the execute job on the job system
		_workers.push_back(JobSystemWorker());
	}
	_currentWorkItem = 0;
	_maxJobsTillRemove = 100;
	_jobsTillRemove = _maxJobsTillRemove;
}

JobSystem::~JobSystem()
{
	_jobVectorMutex.lock();
	_Jobs.clear();
	auto destoryJob = []() {std::terminate(); };
	for (size_t i = 0; i < _workerCount; i++)
	{
		_Jobs.push_back(new Job<void()>(destoryJob));
	}
	_jobVectorMutex.unlock();
}

JobBase* JbSystem::JobSystem::GetWork()
{
	JobSystem* jobsystem = GetInstance();
	jobsystem->_currentWorkItemMutex.lock();
	jobsystem->_jobVectorMutex.lock();
	JobBase* job = GetInstance()->FindWork();
	jobsystem->_currentWorkItemMutex.unlock();
	jobsystem->_jobVectorMutex.unlock();
	return job;
}

JobSystem* JbSystem::JobSystem::GetInstance()
{
	return JobSystemSingleton;
}

JobBase* JbSystem::JobSystem::FindWork()
{
	if (_currentWorkItem >= _Jobs.size()) {
		return nullptr;
	}

	JobBase* possibleJob = _Jobs[_currentWorkItem];

	if (_blockedJobIds.contains(possibleJob->GetId())) {
		return nullptr;
	}

	_currentWorkItem++;

	return possibleJob;
}

const void JbSystem::JobSystem::Schedule(const JobBase* job)
{
	return GetInstance()->scheduleJob(const_cast<JobBase*>(job));
}

void JbSystem::JobSystem::scheduleJob(JobBase* newjob)
{
	JobSystem* jobSystem = JobSystemSingleton;

	jobSystem->_jobVectorMutex.lock();
	_JobIdToJobIndex.insert(std::pair(newjob->GetId(), _Jobs.size()));
	jobSystem->_Jobs.push_back(newjob);
	jobSystem->_jobVectorMutex.unlock();

	//Let the jobsystem know that a job has been queued, queue cleanup job at a regular interval
	jobSystem->_jobsTillRemove--;
	if (jobSystem->_jobsTillRemove == 0) {
		jobSystem->_jobsTillRemove = jobSystem->_maxJobsTillRemove;
		JobSystem::Schedule([]() { JobSystemSingleton->ThrowAwayCompletedJobs(); });
	}
}

void JbSystem::JobSystem::scheduleFutureJob(JobBase* newjob)
{
	_dependencyJobVectorMutex.lock();
	_JobIdToUnscheduledJobIndex.insert(std::pair(newjob->GetId(), _UnscheduledJobs.size()));
	_UnscheduledJobs.push_back(newjob);
	_dependencyJobVectorMutex.unlock();
}

void JbSystem::JobSystem::AddDependencyToJob(int id, int dependency)
{
	_dependencyJobVectorMutex.lock();
	_jobVectorMutex.lock();
	if (_JobIdToJobIndex.contains(dependency) || _JobIdToUnscheduledJobIndex.contains(dependency)) {
		_dependencyJobVectorMutex.unlock();
		_jobVectorMutex.unlock();
		return;
	}
	_jobVectorMutex.unlock();

	_dependencyOpenJobMap.insert(std::pair(dependency, id));
	_openJobDependencyMap.insert(std::pair(id, dependency));
	if (!_blockedJobIds.contains(id)) _blockedJobIds.insert(id);
	_dependencyJobVectorMutex.unlock();
}

void JbSystem::JobSystem::ThrowAwayCompletedJobs()
{
	if (_JobCompletion.size() <= 1) return;

	_currentWorkItemMutex.lock();
	_jobVectorMutex.lock();
	size_t size = _JobCompletion.size();

	std::sort(_Jobs.begin(), _Jobs.end(), [=](const JobBase* a, const JobBase* b)
		{
			return _JobCompletion.contains(a->GetId()) && !_JobCompletion.contains(b->GetId());
		});
	_JobCompletion.clear();
	for (size_t i = 0; i < size; i++)
	{
		//Free memory from the job
		JobBase* job = _Jobs[i];
		_JobIdToJobIndex.erase(job->GetId());
		delete job;
	}
	_Jobs.erase(_Jobs.begin(), _Jobs.begin() + size);
	_currentWorkItem -= size;

	_currentWorkItemMutex.unlock();
	_jobVectorMutex.unlock();
}

void JbSystem::JobSystem::CompleteJob(const JobBase* job)
{
	int jobId = job->GetId();
	JobSystemSingleton->_dependencyJobVectorMutex.lock();
	if (JobSystemSingleton->_dependencyOpenJobMap.contains(jobId)) {
		auto dependencyCurrent = JobSystemSingleton->_dependencyOpenJobMap.lower_bound(jobId);
		auto dependencyEnd = JobSystemSingleton->_dependencyOpenJobMap.upper_bound(jobId);

		//Iterator over the all jobs that were dependend on the completed job
		while (dependencyCurrent != dependencyEnd)
		{
			int openJobId = dependencyCurrent->second;
			if (dependencyCurrent->first == jobId)
			{
				auto openJobCurrent = JobSystemSingleton->_openJobDependencyMap.lower_bound(openJobId);
				auto openJobEnd = JobSystemSingleton->_openJobDependencyMap.upper_bound(openJobId);
				while (openJobCurrent != openJobEnd)
				{
					if (openJobCurrent->first == openJobId) {
						JobSystemSingleton->_openJobDependencyMap.erase(openJobCurrent);
						//if (!JobSystemSingleton->_openJobDependencyMap.contains(openJobId))
						break;
						//openJobCurrent = JobSystemSingleton->_openJobDependencyMap.lower_bound(openJobId);
					}
					else
						openJobCurrent++;
				}
			}
			dependencyCurrent++;

			//Schedule the no longer depended job
			if (!JobSystemSingleton->_openJobDependencyMap.contains(openJobId)) {
				JobSystemSingleton->_jobVectorMutex.lock();
				JobSystemSingleton->_blockedJobIds.erase(openJobId);
				JobSystemSingleton->_jobVectorMutex.unlock();
				for (int i = 0; i < JobSystemSingleton->_UnscheduledJobs.size(); i++)
				{
					JobBase* job = JobSystemSingleton->_UnscheduledJobs[i];
					int jobId = job->GetId();
					if (jobId == openJobId) {
						JobSystemSingleton->scheduleJob(job);
						JobSystemSingleton->_UnscheduledJobs.erase(JobSystemSingleton->_UnscheduledJobs.begin() + i);
						JobSystemSingleton->_JobIdToUnscheduledJobIndex.erase(jobId);
					}
				}
			}
		}
		JobSystemSingleton->_dependencyOpenJobMap.erase(jobId);
	}
	JobSystemSingleton->_jobVectorMutex.lock();
	JobSystemSingleton->_JobCompletion.insert(job->GetId());
	JobSystemSingleton->_jobVectorMutex.unlock();
	JobSystemSingleton->_dependencyJobVectorMutex.unlock();
}