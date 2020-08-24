#include "JobSystemThread.h"

#include <iostream>
#include <string>
#include <chrono>

using namespace JbSystem;

void JobSystemWorker::ThreadLoop() {
	std::unique_lock ul(_isRunningMutex);
	//std::cout << "Worker has started" << std::endl;

	int noJobCount = 0;

	while (Active) {
		if (noJobCount > 2000)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));

		InternalJobBase* job;

		job = TryTakeJob();
		if (job != nullptr) {
			job->Run();
			_finishJobFunction(job);
			noJobCount = 0;
			continue;
		}

		job = _stealJobFunction();
		if (job != nullptr) {
			job->Run();
			_finishJobFunction(job);
			continue;
		}

		noJobCount++;
	}

	_isRunningConditionalVariable.notify_all();
	//std::cout << "Worker has exited!" << std::endl;
}

JobSystemWorker::JobSystemWorker(StealFunction stealJobFunction, FinishJobFunction finishJobFunction)
{
	_stealJobFunction = stealJobFunction;
	_finishJobFunction = finishJobFunction;
	Active = false;
}

JbSystem::JobSystemWorker::JobSystemWorker(JobSystemWorker& worker)
{
	_stealJobFunction = worker._stealJobFunction;
	_finishJobFunction = worker._finishJobFunction;
	Active = worker.Active;
	worker.Active = false;
	std::lock_guard workerLock(worker._queueMutex);
	for (size_t i = 0; i < worker._longTaskQueue.size(); i++)
	{
		InternalJobBase* job = worker._longTaskQueue.front();
		worker._longTaskQueue.pop();
		_longTaskQueue.emplace(job);
	}

	for (size_t i = 0; i < worker._mediumTaskQueue.size(); i++)
	{
		InternalJobBase* job = worker._mediumTaskQueue.front();
		worker._mediumTaskQueue.pop();
		_mediumTaskQueue.emplace(job);
	}

	for (size_t i = 0; i < worker._shortTaskQueue.size(); i++)
	{
		InternalJobBase* job = worker._shortTaskQueue.front();
		worker._shortTaskQueue.pop();
		_shortTaskQueue.emplace(job);
	}
}

JbSystem::JobSystemWorker::~JobSystemWorker()
{
	// free memory of all queued jobs in this worker object
	InternalJobBase* currentJob = TryTakeJob();
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

InternalJobBase* JbSystem::JobSystemWorker::TryTakeJob(JobTime maxTimeInvestment)
{
	//Return a result based on weight of a job

	std::lock_guard lock(_queueMutex);
	if (!_shortTaskQueue.empty() && maxTimeInvestment >= JobTime::Short) {
		auto value = _shortTaskQueue.front();
		_shortTaskQueue.pop();
		return value;
	}
	else if (!_mediumTaskQueue.empty() && maxTimeInvestment >= JobTime::Medium) {
		auto value = _mediumTaskQueue.front();
		_mediumTaskQueue.pop();
		return value;
	}
	else if (!_longTaskQueue.empty() && maxTimeInvestment >= JobTime::Long) {
		auto value = _longTaskQueue.front();
		_longTaskQueue.pop();
		return value;
	}

	return nullptr;
}

void JbSystem::JobSystemWorker::GiveJob(InternalJobBase*& newJob)
{
	JobTime timeInvestment = newJob->GetTimeInvestment();

	std::lock_guard lock(_queueMutex);
	if (timeInvestment == JobTime::Short)
		_shortTaskQueue.emplace(newJob);
	else if (timeInvestment == JobTime::Medium)
		_mediumTaskQueue.emplace(newJob);
	else
		_longTaskQueue.emplace(newJob);
}