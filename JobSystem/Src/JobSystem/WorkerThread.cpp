#include "WorkerThread.h"

#include "JobSystem.h"

#include <iostream>
#include <string>
#include <chrono>

using namespace JbSystem;

void JobSystemWorker::ThreadLoop() {
	std::unique_lock ul(_isRunningMutex);
	//std::cout << "Worker has started" << std::endl;

	std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
	bool wasJobCompleted = false;
	int noWork = 0;
	while (true) {

		_isBusy.store(true);

		// In case shutdown of shutdown of jobsystem
		if (_shutdownRequested.load()) {
			_isBusy.store(false);
			break;
		}

		Job* job = nullptr;
		for (size_t i = 0; i < 3; i++)
		{
			job = TryTakeJob(JobPriority::Low);
			if (job != nullptr)
				break;
		}


		if (job != nullptr) {
			_jobsystem->RunJob(*this, job);
			_isBusy.store(false);
			wasJobCompleted = true;
			continue;
		}

		noWork++;

		if (noWork < 100)
			continue;

		// Take a possible job from a random worker
		JobSystemWorker& randomWorker = _jobsystem->_workers.at(_jobsystem->GetRandomWorker());

		for (size_t i = 0; i < 5; i++)
		{
			job = _jobsystem->TakeJobFromWorker(randomWorker, JobPriority::Low);
			if (job != nullptr)
				break;
		}

		// In case shutdown of shutdown of jobsystem
		if (_shutdownRequested.load()) {
			_isBusy.store(false);
			break;
		}

		if (job != nullptr)
		{
			assert(randomWorker.IsJobScheduled(job->GetId()));
			_jobsystem->RunJob(randomWorker, job);
			_isBusy.store(false);
			wasJobCompleted = true;
		}
			
		if (wasJobCompleted)
		{
			noWork = 0;
			wasJobCompleted = false;
			startTime = std::chrono::high_resolution_clock::now();
			continue;
		}

		if ((std::chrono::high_resolution_clock::now() - startTime) <= std::chrono::milliseconds(25))
		{
			noWork = 0;
			continue;
		}

		_isBusy.store(false);
		
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
	return _isBusy.load();
}

JobSystemWorker::JobSystemWorker(JobSystem* jobsystem)
	: _jobsystem(jobsystem), Active(false), _shutdownRequested(false),
	_modifyingThread(), _highPriorityTaskQueue(), _normalPriorityTaskQueue(), _lowPriorityTaskQueue(),
	_scheduledJobsMutex(), _scheduledJobs(),
	_isRunningMutex(), _worker(),
	_isBusy(false)
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

	_shutdownRequested.store(false);
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
	if (!_modifyingThread.try_lock())
		return nullptr;


	if (maxTimeInvestment >= JobPriority::High) {
		if (!_highPriorityTaskQueue.empty()) {
			Job* value = _highPriorityTaskQueue.front();
			_highPriorityTaskQueue.pop_front();
			assert(IsJobScheduled(value->GetId()));
			_modifyingThread.unlock();
			return value;
		}
	}

	if (maxTimeInvestment >= JobPriority::Normal) {
		if (!_normalPriorityTaskQueue.empty()) {
			Job* value = _normalPriorityTaskQueue.front();
			_normalPriorityTaskQueue.pop_front();
			assert(IsJobScheduled(value->GetId()));
			_modifyingThread.unlock();
			return value;
		}
	}

	if (maxTimeInvestment >= JobPriority::Low) {
		if (!_lowPriorityTaskQueue.empty()) {
			Job* value = _lowPriorityTaskQueue.front();
			_lowPriorityTaskQueue.pop_front();
			assert(IsJobScheduled(value->GetId()));
			_modifyingThread.unlock();
			return value;
		}
	}

	_modifyingThread.unlock();
	return nullptr;
}

bool JbSystem::JobSystemWorker::IsJobInQueue(const JobId& jobId) {

	const int& id = jobId.ID();
	std::scoped_lock<JbSystem::mutex> lock(_modifyingThread);
	for (const auto& highPriorityJob : _highPriorityTaskQueue)
	{
		if (highPriorityJob->GetId().ID() == id)
			return true;
	}

	for (const auto& normalPriorityJob : _normalPriorityTaskQueue)
	{
		if (normalPriorityJob->GetId().ID() == id)
			return true;
	}

	for (const auto& lowPriorityJob : _lowPriorityTaskQueue)
	{
		if (lowPriorityJob->GetId().ID() == id)
			return true;
	}

	return false;
}

size_t JbSystem::JobSystemWorker::ScheduledJobCount()
{
	_scheduledJobsMutex.lock();
	size_t scheduledCount = _scheduledJobs.size();
	_scheduledJobsMutex.unlock();
	return scheduledCount;
}

void JbSystem::JobSystemWorker::UnScheduleJob(const JobId& previouslyScheduledJob)
{
	const int& id = previouslyScheduledJob.ID();
	assert(!IsJobInQueue(id)); // In case the task is still scheduled then it wasn't removed properly

	_modifyingThread.lock();
	_scheduledJobsMutex.lock();
	assert(_scheduledJobs.contains(id));
	_scheduledJobs.erase(id);
	_scheduledJobsMutex.unlock();
	_modifyingThread.unlock();
}

void JbSystem::JobSystemWorker::ScheduleJob(const JobId& jobId)
{
	const int& id = jobId.ID();

	_modifyingThread.lock();
	_scheduledJobsMutex.lock();
	assert(!_scheduledJobs.contains(id));
	_scheduledJobs.emplace(id);
	_scheduledJobsMutex.unlock();
	_modifyingThread.unlock();
}

bool JbSystem::JobSystemWorker::GiveJob(Job* const& newJob, const JobPriority priority)
{
	if (!IsRunning()) {
		return false;
	}

	_modifyingThread.lock();
	assert(_scheduledJobs.contains(newJob->GetId().ID()));

	if (priority == JobPriority::High) {
		_highPriorityTaskQueue.emplace_back(newJob);
	}

	else if (priority == JobPriority::Normal) {
		_normalPriorityTaskQueue.emplace_back(newJob);
	}

	else if (priority == JobPriority::Low) {
		_lowPriorityTaskQueue.emplace_back(newJob);
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
	const JobId& jobId = job->GetId();
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

JbSystem::JobSystemWorker::PausedJob::PausedJob(Job* affectedJob, JobSystemWorker& worker)
	: AffectedJob(affectedJob), Worker(worker)
{
}