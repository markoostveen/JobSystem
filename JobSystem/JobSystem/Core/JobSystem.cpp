#include "JobSystem.h"

#include <functional>
#include <iostream>
#include <algorithm>

using namespace JbSystem;

JobSystem* JobSystemSingleton = new JobSystem();

JobSystem::~JobSystem()
{
	auto jobs = Shutdown();

	//Delete all remaining jobs
	for (size_t i = 0; i < jobs.size(); i++)
	{
		FinishJob(jobs[i]);
	}
}

void JbSystem::JobSystem::Start(int threadCount)
{
	if (threadCount == 0) {
		std::cout << "JobSystem cannot start with 0 workers" << std::endl;
		return;
	}

	std::vector<InternalJobBase*> jobs;
	if (_workerCount != 0) {
		std::cout << "JobSystem is shutting down" << std::endl;

		//Shut down workers safely, and extract scheduled jobs
		jobs = Shutdown();
	}
	_jobsMutex.lock();

	// Grab last batch of jobs that were created in the time that lock was released in shutdown methon till now
	auto lastBatchOfJobs = StealAllJobsFromWorkers();
	for (int i = 0; i < lastBatchOfJobs.size(); i++)
	{
		jobs.push_back(lastBatchOfJobs[i]);
	}

	std::cout << "JobSystem is starting" << std::endl;

	//Change amount of worker threads
	_workerCount = threadCount;
	_workers.reserve(threadCount);
	for (int i = 0; i < _workerCount; i++)
	{
		// set callback function for worker threads to call the execute job on the job system
		_workers.push_back(JobSystemWorker([this] { return StealJobFromWorker(); }, [this](InternalJobBase*& job) {FinishJob(job); }));
	}
	_jobsMutex.unlock();

	//Reschedule saved jobs
	int rescheduleJob = Schedule([&]() {
		std::unordered_set<int> jobIds;
		for (size_t i = 0; i < jobs.size(); i++)
		{
			jobIds.insert(jobs[i]->GetId());
		}
		_jobsMutex.lock();
		_scheduledJobs.reserve(jobs.size());
		_scheduledJobs.insert(jobIds.begin(), jobIds.end());
		_jobsMutex.unlock();

		int iteration = 0;
		while (iteration < jobs.size()) {
			for (int i = 0; i < _workerCount && iteration < jobs.size(); i++)
			{
				_workers[i].GiveJob(jobs[iteration]);
				iteration++;
			}
		}
		}, JobTime::Long);

	for (int i = 0; i < _workerCount; i++)
	{
		// set callback function for worker threads to call the execute job on the job system
		_workers[i].Start();
	}

	//wait for rescheduling to be done, then return the caller
	WaitForJobCompletion(rescheduleJob);
	std::cout << "JobSystem started with " << threadCount << " workers!" << std::endl;
}

std::vector<InternalJobBase*> JbSystem::JobSystem::Shutdown()
{
	//Stop the worker thread after last iteration
	for (int i = 0; i < _workerCount; i++)
	{
		_workers[i].Active = false;
	}

	std::vector<InternalJobBase*> allJobs;
	std::thread stealingJobsThread([&]() {allJobs = StealAllJobsFromWorkers(); });
	for (int i = 0; i < _workerCount; i++)
	{
		_workers[i].WaitForShutdown();
	}

	_jobsMutex.lock();
	stealingJobsThread.join();

	//Get jobs finished while threads were stopping
	auto lastBatchOfJobs = StealAllJobsFromWorkers();
	for (int i = 0; i < lastBatchOfJobs.size(); i++)
	{
		allJobs.push_back(lastBatchOfJobs[i]);
	}

	_workerCount = 0;
	_workers.clear();
	_schedulesTillMaintainance = _maxSchedulesTillMaintainance;
	_jobsMutex.unlock();
	return allJobs;
}

void JbSystem::JobSystem::ExecuteJob()
{
	JobSystem* jobsystem = GetInstance();
	InternalJobBase* job = jobsystem->StealJobFromWorker(JobTime::Medium);
	if (job != nullptr)
	{
		job->Run();
		jobsystem->FinishJob(job);
	}
}

JobSystem* JbSystem::JobSystem::GetInstance()
{
	return JobSystemSingleton;
}

std::atomic<int> finishedJobs = 0;
int JbSystem::JobSystem::ActiveJobCount()
{
	_jobsMutex.lock();
	int jobCount = _scheduledJobs.size();
	_jobsMutex.unlock();
	std::cout << "Total remaining: " << jobCount << " Total jobs Finished: " << finishedJobs << std::endl;
	return jobCount;
}

int JbSystem::JobSystem::ScheduleJob(InternalJobBase* newjob)
{
	int jobId = newjob->GetId();

#ifdef DEBUG
	if (_workerCount != 0) {
#endif

		_jobsMutex.lock();
		int worker = rand() % _workerCount;
		_scheduledJobs.insert(jobId);
		_workers[worker].GiveJob(newjob);
		_jobsMutex.unlock();

		// Make sure that all workers are still running correctly
		_schedulesTillMaintainance--;
		if (_schedulesTillMaintainance == 0) {
			//std::cout << "Validating Jobsystem threads" << std::endl;
			_schedulesTillMaintainance = _maxSchedulesTillMaintainance;
			Schedule([&]() {
				for (size_t i = 0; i < _workerCount; i++)
				{
					if (!_workers[i].IsRunning() && _workers[i].Active == true)
						_workers[i].Start();
				}
				}, JobTime::Short);
		}

		return jobId;
#ifdef DEBUG
	}
	else {
		std::cout << "Jobsystem is not running, please start it explicitly. This job is not scheduled!" << std::endl;
		_jobsMutex.lock();
		_scheduledJobs.insert(jobId);
		_jobsMutex.unlock();
		FinishJob(newjob);
		return jobId;
	}
#endif
}

void JbSystem::JobSystem::WaitForJobCompletion(int jobId)
{
	JobSystem* jobSystem = GetInstance();

	jobSystem->_jobsMutex.lock();
	bool jobFinished = !jobSystem->_scheduledJobs.contains(jobId);
	jobSystem->_jobsMutex.unlock();

	while (!jobFinished) {
		ExecuteJob();// use resources to aid workers instead of sleeping

		jobSystem->_jobsMutex.lock();
		jobFinished = !jobSystem->_scheduledJobs.contains(jobId);
		jobSystem->_jobsMutex.unlock();
	}
}

InternalJobBase* JbSystem::JobSystem::StealJobFromWorker(JobTime maxTimeInvestment)
{
	int worker = rand() % _workerCount;
	return _workers[worker].TryTakeJob(maxTimeInvestment);
}

void JbSystem::JobSystem::FinishJob(InternalJobBase*& job)
{
	_jobsMutex.lock();
	_scheduledJobs.erase(job->GetId());
	_jobsMutex.unlock();
	delete job;
	finishedJobs++;
}

std::vector<InternalJobBase*> JbSystem::JobSystem::StealAllJobsFromWorkers()
{
	std::vector<InternalJobBase*> jobs = std::vector<InternalJobBase*>();
	jobs.reserve(10000);
	for (size_t i = 0; i < _workerCount; i++)
	{
		InternalJobBase* job = _workers[i].TryTakeJob();
		while (job != nullptr) {
			jobs.push_back(job);
			job = _workers[i].TryTakeJob();
		}
	}
	return jobs;
}