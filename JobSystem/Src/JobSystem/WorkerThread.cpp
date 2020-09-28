#include "WorkerThread.h"

#include "JobSystem.h"

#include <iostream>
#include <string>
#include <chrono>

using namespace JbSystem;

void JobSystemWorker::ThreadLoop() {
	std::unique_lock ul(_isRunningMutex);
	//std::cout << "Worker has started" << std::endl;

	while (Active) {
		Job* job = TryTakeJob();
		if (job != nullptr) {
			job->Run();
			FinishJob(job);
			continue;
		}

		_jobsystem->ExecuteJob(JobPriority::Low);
	}

	_isRunningConditionalVariable.notify_all();
	//std::cout << "Worker has exited!" << std::endl;
}

JobSystemWorker::JobSystemWorker(JobSystem* jobsystem)
{
	_jobsystem = jobsystem;
	Active = false;
}

JbSystem::JobSystemWorker::JobSystemWorker(const JobSystemWorker& worker)
{
	_jobsystem = worker._jobsystem;
	Active = false;
}

JbSystem::JobSystemWorker::~JobSystemWorker()
{
	if (_worker.joinable())
		_worker.join();
}

const bool JobSystemWorker::IsRunning()
{
	return Active;
}

void JbSystem::JobSystemWorker::WaitForShutdown()
{
	std::unique_lock ul(_isRunningMutex);
	_isRunningConditionalVariable.wait(ul, [&] {return Active == false; });
}

void JobSystemWorker::Start()
{
	if (Active == true)
		std::cout << "Jobsystem thread restarted, but exited previously because of an error" << std::endl;
	Active = true;
	if (_worker.joinable())
		_worker.join();
	_worker = std::thread(&JobSystemWorker::ThreadLoop, this);
}

Job* JbSystem::JobSystemWorker::TryTakeJob(const JobPriority maxTimeInvestment)
{
	//Return a result based on priority of a job

	_jobsMutex.lock();
	Job* value = nullptr;
	if (maxTimeInvestment >= JobPriority::High) {
		if (!_highPriorityTaskQueue.empty()) {
			value = _highPriorityTaskQueue.front();
			_highPriorityTaskQueue.pop();
			_jobsMutex.unlock();
			return value;
		}
	}

	if (maxTimeInvestment >= JobPriority::Normal) {
		if (!_normalPriorityTaskQueue.empty()) {
			value = _normalPriorityTaskQueue.front();
			_normalPriorityTaskQueue.pop();
			_jobsMutex.unlock();
			return value;
		}
	}

	if (maxTimeInvestment >= JobPriority::Low) {
		if (!_lowPriorityTaskQueue.empty()) {
			value = _lowPriorityTaskQueue.front();
			_lowPriorityTaskQueue.pop();
			_jobsMutex.unlock();
			return value;
		}
	}

	_jobsMutex.unlock();

	return nullptr;
}

void JbSystem::JobSystemWorker::GiveJob(Job* newJob)
{
	const JobPriority timeInvestment = newJob->GetPriority();

	_jobsMutex.lock();
	if (timeInvestment == JobPriority::High) {
		_highPriorityTaskQueue.emplace(newJob);
	}

	else if (timeInvestment == JobPriority::Normal) {
		_normalPriorityTaskQueue.emplace(newJob);
	}

	else if (timeInvestment == JobPriority::Low) {
		_lowPriorityTaskQueue.emplace(newJob);
	}

	_jobsMutex.unlock();
}

void JbSystem::JobSystemWorker::FinishJob(Job*& job)
{
	const int jobId = job->GetId();
	_completedJobsMutex.lock();
	if (!_completedJobs.contains(jobId))
		_completedJobs.emplace(jobId);
	_completedJobsMutex.unlock();
	job->Free();
}

bool JbSystem::JobSystemWorker::IsJobFinished(const int jobId)
{
	_completedJobsMutex.lock();
	bool contains = _completedJobs.contains(jobId);
	_completedJobsMutex.unlock();
	return contains;
}