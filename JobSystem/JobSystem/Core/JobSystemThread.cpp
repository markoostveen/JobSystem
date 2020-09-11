#include "JobSystemThread.h"

#include <iostream>
#include <string>
#include <chrono>

using namespace JbSystem;

void JobSystemWorker::ThreadLoop() {
	std::unique_lock ul(_isRunningMutex);
	//std::cout << "Worker has started" << std::endl;

	int noJobCount = 0;

	Job* job;

	while (Active) {
		job = TryTakeJob();
		if (job != nullptr) {
			noJobCount = 0;
			job->Run();
			FinishJob(job);
			continue;
		}

		noJobCount++;
		if (noJobCount > 20000) {
			std::this_thread::sleep_for(std::chrono::microseconds(250));
		}

		_executeExternalJobFunction();
	}

	_isRunningConditionalVariable.notify_all();
	//std::cout << "Worker has exited!" << std::endl;
}

JobSystemWorker::JobSystemWorker(executeExternalFunction executeExternalJobFunction)
{
	_executeExternalJobFunction = executeExternalJobFunction;
	Active = false;
}

JbSystem::JobSystemWorker::JobSystemWorker(const JobSystemWorker& worker)
{
	_executeExternalJobFunction = worker._executeExternalJobFunction;
	_lowPriorityTaskQueue = worker._lowPriorityTaskQueue;
	_normalPriorityTaskQueue = worker._normalPriorityTaskQueue;
	_highPriorityTaskQueue = worker._highPriorityTaskQueue;
	Active = false;
}

JbSystem::JobSystemWorker::~JobSystemWorker()
{
	// free memory of all queued jobs in this worker object
	Job* currentJob = TryTakeJob();
	while (currentJob != nullptr) {
		delete currentJob;
		currentJob = TryTakeJob();
	}

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

Job* JbSystem::JobSystemWorker::TryTakeJob(JobPriority maxTimeInvestment)
{
	//Return a result based on weight of a job

	_queueMutex.lock();
	if (!_highPriorityTaskQueue.empty() && maxTimeInvestment >= JobPriority::High) {
		auto value = _highPriorityTaskQueue.front();
		_highPriorityTaskQueue.pop();
		_queueMutex.unlock();
		return value;
	}
	else if (!_normalPriorityTaskQueue.empty() && maxTimeInvestment >= JobPriority::Normal) {
		auto value = _normalPriorityTaskQueue.front();
		_normalPriorityTaskQueue.pop();
		_queueMutex.unlock();
		return value;
	}
	else if (!_lowPriorityTaskQueue.empty() && maxTimeInvestment >= JobPriority::Low) {
		auto value = _lowPriorityTaskQueue.front();
		_lowPriorityTaskQueue.pop();
		_queueMutex.unlock();
		return value;
	}

	_queueMutex.unlock();
	return nullptr;
}

void JbSystem::JobSystemWorker::GiveJob(Job*& newJob)
{
	JobPriority timeInvestment = newJob->GetTimeInvestment();

	_queueMutex.lock();
	if (timeInvestment == JobPriority::High)
		_highPriorityTaskQueue.emplace(newJob);
	else if (timeInvestment == JobPriority::Normal)
		_normalPriorityTaskQueue.emplace(newJob);
	else
		_lowPriorityTaskQueue.emplace(newJob);
	_queueMutex.unlock();
}

void JbSystem::JobSystemWorker::FinishJob(Job*& job)
{
	_completedJobsMutex.lock();
	_completedJobs.emplace(job->GetId());
	_completedJobsMutex.unlock();
	delete job;
}

bool JbSystem::JobSystemWorker::IsJobFinished(int& jobId)
{
	_completedJobsMutex.lock();
	bool contains = _completedJobs.contains(jobId);
	_completedJobsMutex.unlock();
	return contains;
}