#include "WorkerThread.h"

#include "JobSystem.h"

#include <iostream>
#include <string>
#include <chrono>

using namespace JbSystem;

void JobSystemWorker::ThreadLoop() {
	std::unique_lock ul(_isRunningMutex);
	//std::cout << "Worker has started" << std::endl;

	int noWork = 0;

	while (Active) {
		Job* job = TryTakeJob();
		if (job != nullptr) {
			job->Run();
			FinishJob(job);
			noWork = 0;
			continue;
		}

		_jobsystem->ExecuteJob(JobPriority::Low);
		noWork++;

		if (noWork > 500000) {

			// Check if other works are active
			bool otherWorkersActive = false;
			for (size_t i = 0; i < _jobsystem->_activeWorkerCount.load(); i++)
			{
				JobSystemWorker& worker = _jobsystem->_workers[i];
				if (worker.Active && this != &worker) {
					otherWorkersActive = true;
					continue;
				}
			}

			// Do not shutdown in case there are no other workers
			if (otherWorkersActive || !_jobsystem->Active || _shutdownRequested.load()) {
				break;
			}

			std::this_thread::yield();
		}
	}

	Active = false;
	//std::cout << "Worker has exited!" << std::endl;
}

void JbSystem::JobSystemWorker::RequestShutdown()
{
	_shutdownRequested.store(true);
}

JobSystemWorker::JobSystemWorker(JobSystem* jobsystem)
{
	_jobsystem = jobsystem;
	Active = false;
	_shutdownRequested.store(false);
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
}

void JobSystemWorker::Start()
{
	if (Active) {
		std::cout << "Jobsystem thread detected an error, cannot continue!" << std::endl;
		std::terminate();
	}

	Active = true;
	_jobsystem->OptimizePerformance(); // Determin best scaling options


	if (_worker.get_id() != std::thread::id()) {
		if (_worker.joinable())
			_worker.join();
		else
			_worker.detach();
	}
	_worker = std::thread([](JobSystemWorker* worker){ worker->_jobsystem->WorkerLoop(worker); }, this);
}

int JbSystem::JobSystemWorker::WorkerId()
{
	return _jobsystem->GetWorkerId(this);
}

Job* JbSystem::JobSystemWorker::TryTakeJob(const JobPriority& maxTimeInvestment)
{
	//Return a result based on priority of a job
	Job* value = nullptr;

	bool locked = _modifyingThread.try_lock();
	if (!locked)
		return value;

	if (maxTimeInvestment >= JobPriority::High) {
		if (!_highPriorityTaskQueue.empty()) {
			value = _highPriorityTaskQueue.front();
			_highPriorityTaskQueue.pop();
			_modifyingThread.unlock();
			return value;
		}
	}

	if (maxTimeInvestment >= JobPriority::Normal) {
		if (!_normalPriorityTaskQueue.empty()) {
			value = _normalPriorityTaskQueue.front();
			_normalPriorityTaskQueue.pop();
			_modifyingThread.unlock();
			return value;
		}
	}

	if (maxTimeInvestment >= JobPriority::Low) {
		if (!_lowPriorityTaskQueue.empty()) {
			value = _lowPriorityTaskQueue.front();
			_lowPriorityTaskQueue.pop();
			_modifyingThread.unlock();
			return value;
		}
	}

	_modifyingThread.unlock();
	return value;
}

void JbSystem::JobSystemWorker::GiveJob(Job*& newJob, const JobPriority priority)
{
	const int jobId = newJob->GetId();

	if (!IsJobScheduled(jobId)) {
		_scheduledJobsMutex.lock();
		_scheduledJobs.emplace(jobId);
		_scheduledJobsMutex.unlock();
	}

	_modifyingThread.lock();

	if (priority == JobPriority::High) {
		_highPriorityTaskQueue.emplace(newJob);
	}

	else if (priority == JobPriority::Normal) {
		_normalPriorityTaskQueue.emplace(newJob);
	}

	else if (priority == JobPriority::Low) {
		_lowPriorityTaskQueue.emplace(newJob);
	}

	_modifyingThread.unlock();

	if (!Active)
		Start();
}

void JbSystem::JobSystemWorker::GiveFutureJob(int& jobId)
{
	_scheduledJobsMutex.lock();
	_scheduledJobs.emplace(jobId);
	_scheduledJobsMutex.unlock();
}

void JbSystem::JobSystemWorker::GiveFutureJobs(const std::vector<const Job*>& newjobs, int startIndex, int size)
{
	_scheduledJobsMutex.lock();
	for (size_t i = 0; i < size; i++)
	{
		_scheduledJobs.emplace(newjobs[startIndex + i]->GetId());
	}
	_scheduledJobsMutex.unlock();

}

void JbSystem::JobSystemWorker::FinishJob(Job*& job)
{
	const int jobId = job->GetId();
	job->Free();
	_completedJobsMutex.lock();
	_completedJobs.emplace(jobId);
	_completedJobsMutex.unlock();
	_scheduledJobsMutex.lock();
	_scheduledJobs.erase(jobId);
	_scheduledJobsMutex.unlock();
}

bool JbSystem::JobSystemWorker::IsJobScheduled(const int& jobId)
{
	_scheduledJobsMutex.lock();
	bool contains = _scheduledJobs.contains(jobId);
	_scheduledJobsMutex.unlock();
	return contains;
}

bool JbSystem::JobSystemWorker::IsJobFinished(const int& jobId)
{
	bool contains = false;
	_completedJobsMutex.lock();
	if (!_completedJobs.empty())
		contains = _completedJobs.contains(jobId);
	_completedJobsMutex.unlock();
	return contains;
}