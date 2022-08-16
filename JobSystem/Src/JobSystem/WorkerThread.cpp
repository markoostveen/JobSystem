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

	try {

		while (true) {

			_busyLock.lock();
			Job* job = TryTakeJob();

			if (job == nullptr) {
				job = _jobsystem->TakeJobFromWorker(JobPriority::Low);
			}

			_busyJob.store(job);
			_busyLock.unlock();

			if (job != nullptr) {
				job->Run();
				FinishJob(job);
				_busyJob.store(nullptr);
				noWork = 0;
				continue;
			}

			noWork++;

			if (_shutdownRequested.load())
				break;

			if (noWork > 500000) {

				// Check if other works are active
				bool otherWorkersActive = false;
				for (size_t i = 0; i < _jobsystem->_activeWorkerCount.load(); i++)
				{
					JobSystemWorker& worker = _jobsystem->_workers[i];
					if (worker.IsRunning() && this != &worker) {
						otherWorkersActive = true;
						continue;
					}
				}

				// Do not shutdown in case there are no other workers
				if (otherWorkersActive || !_jobsystem->Active) {
					break;
				}

				std::this_thread::yield();
			}
		}
	}
	catch (std::exception& e) {
		assert(false);
		throw e;
	}

	Active.store(false);
	//std::cout << "Worker has exited!" << std::endl;
}

void JbSystem::JobSystemWorker::RequestShutdown()
{
	_shutdownRequested.store(true);
}

bool JbSystem::JobSystemWorker::Busy()
{
	_busyLock.lock();
	bool result = _busyJob.load() != nullptr;
	_busyLock.unlock();
	return result;
}

JobSystemWorker::JobSystemWorker(JobSystem* jobsystem)
	: _jobsystem(jobsystem), Active(false), _shutdownRequested(false),
	_modifyingThread(), _highPriorityTaskQueue(), _normalPriorityTaskQueue(), _lowPriorityTaskQueue(),
	_completedJobsMutex(), _completedJobs(), _scheduledJobsMutex(), _scheduledJobs(),
	_isRunningMutex(), _worker(),
	_busyLock(), _busyJob()
{
}

JbSystem::JobSystemWorker::JobSystemWorker(const JobSystemWorker& worker)
{
	_jobsystem = worker._jobsystem;
	Active.store(false);
}

JbSystem::JobSystemWorker::~JobSystemWorker()
{
	if (_worker.joinable())
		_worker.join();
}

bool JobSystemWorker::IsRunning()
{
	return Active.load();
}

void JbSystem::JobSystemWorker::WaitForShutdown()
{
	std::unique_lock ul(_isRunningMutex);
}

void JobSystemWorker::Start()
{
	_jobsystem->OptimizePerformance(); // Determin best scaling options

	_modifyingThread.lock();
	if (IsRunning()) {
		_modifyingThread.unlock();
		return;
	}

	Active.store(true);


	if (_worker.get_id() != std::thread::id()) {
		if (_worker.joinable())
			_worker.join();
		else
			_worker.detach();
	}
	_worker = std::thread([](JobSystemWorker* worker){ worker->_jobsystem->WorkerLoop(worker); }, this);
	_modifyingThread.unlock();
}

int JbSystem::JobSystemWorker::WorkerId()
{
	return _jobsystem->GetWorkerId(this);
}

Job* JbSystem::JobSystemWorker::TryTakeJob(const JobPriority& maxTimeInvestment)
{
	bool locked = _modifyingThread.try_lock();
	if (!locked)
		return nullptr;

	if (maxTimeInvestment >= JobPriority::High) {
		if (!_highPriorityTaskQueue.empty()) {
			Job* value = _highPriorityTaskQueue.front();
			_highPriorityTaskQueue.pop();
			_modifyingThread.unlock();
			return value;
		}
	}

	if (maxTimeInvestment >= JobPriority::Normal) {
		if (!_normalPriorityTaskQueue.empty()) {
			Job* value = _normalPriorityTaskQueue.front();
			_normalPriorityTaskQueue.pop();
			_modifyingThread.unlock();
			return value;
		}
	}

	if (maxTimeInvestment >= JobPriority::Low) {
		if (!_lowPriorityTaskQueue.empty()) {
			Job* value = _lowPriorityTaskQueue.front();
			_lowPriorityTaskQueue.pop();
			_modifyingThread.unlock();
			return value;
		}
	}

	_modifyingThread.unlock();
	return nullptr;
}

void JbSystem::JobSystemWorker::UnScheduleJob(const JobId& previouslyScheduledJob)
{
	const int& id = previouslyScheduledJob.ID();
	_scheduledJobsMutex.lock();
	if(_scheduledJobs.contains(id))
		_scheduledJobs.erase(id);
	_scheduledJobsMutex.unlock();
}

bool JbSystem::JobSystemWorker::GiveJob(Job* const& newJob, const JobPriority priority)
{
	if (!IsRunning()) {
		return false;
	}

	const JobId& jobId = newJob->GetId();

	_scheduledJobsMutex.lock();
	_scheduledJobs.emplace(jobId.ID());
	_scheduledJobsMutex.unlock();

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

	return true;

}

void JbSystem::JobSystemWorker::GiveFutureJob(const JobId& jobId)
{
	_scheduledJobsMutex.lock();
	_scheduledJobs.emplace(jobId.ID());
	_scheduledJobsMutex.unlock();
}

void JbSystem::JobSystemWorker::GiveFutureJobs(const std::vector<Job*>& newjobs, int startIndex, int size)
{
	_scheduledJobsMutex.lock();
	for (size_t i = 0; i < size; i++)
	{
		_scheduledJobs.emplace(newjobs[startIndex + i]->GetId().ID());
	}
	_scheduledJobsMutex.unlock();

}

void JbSystem::JobSystemWorker::FinishJob(Job*& job)
{
	const JobId jobId = job->GetId();
	const int& id = jobId.ID();
	_completedJobsMutex.lock();
	_completedJobs.emplace(id);
	_completedJobsMutex.unlock();
	UnScheduleJob(jobId);
	job->Free();
}

bool JbSystem::JobSystemWorker::IsJobScheduled(const JobId& jobId)
{
	_scheduledJobsMutex.lock();
	bool contains = _scheduledJobs.contains(jobId.ID());
	_scheduledJobsMutex.unlock();
	return contains;
}

bool JbSystem::JobSystemWorker::IsJobFinished(const JobId& jobId)
{
	bool contains = false;
	_completedJobsMutex.lock();
	if (!_completedJobs.empty())
		contains = _completedJobs.contains(jobId.ID());
	_completedJobsMutex.unlock();
	return contains;
}