#include "JobSystem.h"

#include <functional>
#include <iostream>
#include <algorithm>

using namespace JbSystem;

JobSystem* JobSystemSingleton = new JobSystem();

JobSystem::~JobSystem()
{
	auto jobs = Shutdown();
}

void JbSystem::JobSystem::Start(int threadCount)
{
	if (threadCount <= 1) {
		std::cout << "JobSystem cannot start with 0-1 workers" << std::endl;
		return;
	}

	std::vector<Job*> jobs;
	if (_workerCount != 0) {
		std::cout << "JobSystem is shutting down" << std::endl;

		//Shut down workers safely, and extract scheduled jobs
		jobs = Shutdown();
	}
	_jobsMutex.lock();

	// Grab last batch of jobs that were created in the time that lock was released in shutdown methon till now
	auto lastBatchOfJobs = StealAllJobsFromWorkers();
	jobs.insert(jobs.begin(), lastBatchOfJobs.begin(), lastBatchOfJobs.end());

	std::cout << "JobSystem is starting" << std::endl;

	//Change amount of worker threads
	_workerCount = threadCount;
	_workers.reserve(threadCount);
	for (int i = 0; i < _workerCount; i++)
	{
		// set callback function for worker threads to call the execute job on the job system
		_workers.emplace_back([this] { ExecuteJobFromWorker(); });
	}

	for (int i = 0; i < _workerCount; i++)
	{
		// set callback function for worker threads to call the execute job on the job system
		_workers[i].Start();
	}

	_jobsMutex.unlock();

	//Reschedule saved jobs
	std::thread rescheduleJob = std::thread([&]() {
		int iteration = 0;
		while (iteration < jobs.size()) {
			for (int i = 0; i < _workerCount && iteration < jobs.size(); i++)
			{
				_workers[i].GiveJob(jobs[iteration]);
				iteration++;
			}
		}
		});

	//wait for rescheduling to be done, then return the caller
	rescheduleJob.join();
	std::cout << "JobSystem started with " << threadCount << " workers!" << std::endl;
}

std::vector<Job*> JbSystem::JobSystem::Shutdown()
{
	//Wait for jobsystem to finish remaining jobs
	int realScheduledJobs = 0;
	while (realScheduledJobs > 0) {
		realScheduledJobs = 0;
		for (size_t i = 0; i < _workerCount; i++)
		{
			realScheduledJobs += _workers[i]._highPriorityTaskQueue.size();
			realScheduledJobs += _workers[i]._normalPriorityTaskQueue.size();
			realScheduledJobs += _workers[i]._lowPriorityTaskQueue.size();
		}
	}

	//Let worker safely shutdown and complete it's last job
	for (int i = 0; i < _workerCount; i++)
	{
		_workers[i].Active = false;
		_workers[i].WaitForShutdown();
	}

	_jobsMutex.lock();

	//Get jobs finished while threads were stopping
	std::vector<Job*>allJobs = StealAllJobsFromWorkers();

	_workerCount = 0;
	_workers.clear();
	_schedulesTillMaintainance = _maxSchedulesTillMaintainance;
	_jobsMutex.unlock();
	return allJobs;
}

void JbSystem::JobSystem::ExecuteJob(JobPriority maxTimeInvestment)
{
	ExecuteJobFromWorker(maxTimeInvestment);
}

JobSystem* JbSystem::JobSystem::GetInstance()
{
	return JobSystemSingleton;
}

/// <summary>
/// This reading is not very accurate as it only represents the scheduled jobs count and does not reflect what has already been completed
/// </summary>
/// <returns></returns>
int JbSystem::JobSystem::ActiveJobCount()
{
	_jobsMutex.lock();
	int jobCount = _scheduledJobs.size();
	_jobsMutex.unlock();
	std::cout << "Total remaining: " << jobCount << std::endl;
	return jobCount;
}

int JbSystem::JobSystem::Schedule(Job* newjob)
{
	int jobId = newjob->GetId();

#ifdef DEBUG
	if (_workerCount != 0) {
#endif

		_jobsMutex.lock();
		int worker = rand() % _workerCount;
		if (!_scheduledJobs.contains(jobId))
			_scheduledJobs.insert(jobId);
		_workers[worker].GiveJob(newjob);
		_jobsMutex.unlock();

		// Make sure that all workers are still running correctly
		_schedulesTillMaintainance--;
		if (_schedulesTillMaintainance == 0) {
			//std::cout << "Validating Jobsystem threads" << std::endl;
			_schedulesTillMaintainance = _maxSchedulesTillMaintainance;
			Schedule([&]() { Cleanup(); }, JobPriority::Normal);
		}

		return jobId;
#ifdef DEBUG
	}
	else {
		std::cout << "Jobsystem is not running, please start it explicitly. This job is now floating in the void!" << std::endl;
		throw 1;
	}
#endif
}

int JbSystem::JobSystem::ScheduleFutureJob(Job* newFutureJob)
{
	int jobId = newFutureJob->GetId();
	_jobsMutex.lock();
	_scheduledJobs.insert(jobId);
	_jobsMutex.unlock();
	return jobId;
}

std::vector<int> JbSystem::JobSystem::BatchScheduleJob(std::vector<Job*> newjobs, JobPriority durationOfEveryJob)
{
#ifdef DEBUG
	if (_workerCount != 0) {
#endif
		std::vector<int> jobIds = BatchScheduleFutureJob(newjobs);

		_jobsMutex.lock();
		_scheduledJobs.insert(jobIds.begin(), jobIds.end());
		_jobsMutex.unlock();

		for (size_t i = 0; i < newjobs.size(); i++)
		{
			int worker = rand() % _workerCount;
			_workers[worker].GiveJob(newjobs[i]);

			// Make sure that all workers are still running correctly
			_schedulesTillMaintainance--;
			if (_schedulesTillMaintainance == 0) {
				//std::cout << "Validating Jobsystem threads" << std::endl;
				_schedulesTillMaintainance = _maxSchedulesTillMaintainance;
				Schedule([&]() { Cleanup(); }, JobPriority::Normal);
			}
		}

		return jobIds;

#ifdef DEBUG
	}
	else {
		std::cout << "Jobsystem is not running, please start it explicitly. This job is now floating in the void!" << std::endl;
		throw 1;
	}
#endif
}

std::vector<int> JbSystem::JobSystem::BatchScheduleFutureJob(std::vector<Job*> newjobs)
{
	std::vector<int> jobIds;
	int totalAmountOfJobs = newjobs.size();
	jobIds.reserve(totalAmountOfJobs);

	for (size_t i = 0; i < totalAmountOfJobs; i++)
	{
		jobIds.emplace_back(newjobs[i]->GetId());
	}

	_jobsMutex.lock();
	for (int i = 0; i < totalAmountOfJobs; i++)
	{
		int jobId = jobIds[i];
		if (!_scheduledJobs.contains(jobId))
			_scheduledJobs.emplace(jobId);
	}
	_jobsMutex.unlock();
	return jobIds;
}

bool JbSystem::JobSystem::WaitForJobCompletion(int jobId, bool helpExecutingOtherJobs, int maxMicroSecondsToWait)
{
	bool jobFinished = IsJobCompleted(jobId);

	std::chrono::time_point start = std::chrono::steady_clock::now();

	int minimalWait = 100;
	if (!helpExecutingOtherJobs)
		minimalWait = 0;

	while (!jobFinished) {
		if (helpExecutingOtherJobs)
			ExecuteJob();// use resources to aid workers instead of sleeping

		int passedMicroSeconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();

		if (passedMicroSeconds < maxMicroSecondsToWait)
			continue;

		for (size_t i = 0; i < _workerCount; i++)
		{
			// check if a worker has finished the job
			if (_workers[i].IsJobFinished(jobId)) {
				return true;
			}
		}
		_jobsMutex.lock();
		jobFinished = !_scheduledJobs.contains(jobId);
		_jobsMutex.unlock();

		if (maxMicroSecondsToWait == 0)
			start = std::chrono::steady_clock::now();
		else if (passedMicroSeconds > maxMicroSecondsToWait && !jobFinished)
			return false;
	}

	return true;
}

void JbSystem::JobSystem::WaitForJobCompletion(std::vector<int>& jobIds)
{
	bool finished = false;
	auto waitLambda = [&finished]() {finished = true; };
	WaitForJobCompletion(jobIds, waitLambda);
	while (!finished) { ExecuteJob(JobPriority::High); }
}

void JbSystem::JobSystem::ExecuteJobFromWorker(JobPriority maxTimeInvestment)
{
	int worker = rand() % _workerCount;
	Job* job = _workers[worker].TryTakeJob(maxTimeInvestment);
	if (job != nullptr)
	{
		job->Run();
		_workers[worker].FinishJob(job);
	}
}

bool JbSystem::JobSystem::IsJobCompleted(int& jobId)
{
	bool finished = true;
	for (size_t i = 0; i < _workerCount; i++)
	{
		// check if a worker has finished the job
		finished = _workers[i].IsJobFinished(jobId);
		if (finished) {
			return true;
		}
	}
	_jobsMutex.lock();
	bool contains = !_scheduledJobs.contains(jobId);
	_jobsMutex.unlock();
	return contains;
}

void JbSystem::JobSystem::Cleanup()
{
	//remove deleted jobs
	_jobsMutex.lock();
	for (int i = 0; i < _workerCount; i++)
	{
		JobSystemWorker& worker = _workers[i];
		worker._completedJobsMutex.lock();
		for (auto it = worker._completedJobs.begin(); it != worker._completedJobs.end(); it++)
		{
			int jobId = *it;
			_scheduledJobs.erase(jobId);
		}
		worker._completedJobs.clear();
		worker._completedJobsMutex.unlock();
	}
	_jobsMutex.unlock();

	for (size_t i = 0; i < _workerCount; i++)
	{
		if (!_workers[i].IsRunning() && _workers[i].Active == true)
			_workers[i].Start();
	}
}

std::vector<Job*> JbSystem::JobSystem::StealAllJobsFromWorkers()
{
	std::vector<Job*> jobs = std::vector<Job*>();
	jobs.reserve(10000);
	for (size_t i = 0; i < _workerCount; i++)
	{
		Job* job = _workers[i].TryTakeJob();
		while (job != nullptr) {
			jobs.push_back(job);
			job = _workers[i].TryTakeJob();
		}
	}
	return jobs;
}