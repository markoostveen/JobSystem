#include "WorkerThread.h"

#include "JobSystem.h"

#include <chrono>

#include <iostream>

#include <string>


namespace JbSystem {

	void JobSystemWorker::ThreadLoop() {
		//std::cout << "Worker has started" << std::endl;

		std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
		bool wasJobCompleted = false;
		int noWork = 0;
		while (true) {

			Job* job = nullptr;
			for (size_t i = 0; i < 3; i++)
			{
				job = TryTakeJob(JobPriority::Low);
				if (job != nullptr) {
					break;
				}
			}


			if (job != nullptr) {
				_isBusy.store(true);
				JobSystem::RunJob(*this, job);
				_isBusy.store(false);
				wasJobCompleted = true;
				continue;
			}

			noWork++;

			if (noWork < 100) {
				continue;
			}

			// Take a possible job from a random worker
			JobSystemWorker& randomWorker = _jobsystem->_workers.at(_jobsystem->GetRandomWorker());

			for (size_t i = 0; i < 5; i++)
			{
				job = JobSystem::TakeJobFromWorker(randomWorker, JobPriority::Low);
				if (job != nullptr) {
					break;
				}
			}

			if (job != nullptr)
			{
				_isBusy.store(true);
				assert(randomWorker.IsJobScheduled(job->GetId()));
				JobSystem::RunJob(randomWorker, job);
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
				if (worker.IsActive() && this != &worker) {
					otherWorkersActive = true;
					continue;
				}
			}

			// Do not shutdown in case there are no other workers
			if (otherWorkersActive || !_jobsystem->Active.load()) {
				break;
			}
		}

		//std::cout << "Worker has exited!" << std::endl;
	}

	void JobSystemWorker::KeepAliveLoop()
	{
		while (!_shutdownRequested.load()) {
			_jobsystem->WorkerLoop(this);

	#ifdef JBSYSTEM_KEEP_ALIVE
			std::this_thread::yield();
	#else
			break;
	#endif
		}
	}

	void JobSystemWorker::RequestShutdown()
	{
		_shutdownRequested.store(true);
	}

	bool JobSystemWorker::Busy()
	{
		return _isBusy.load();
	}

	JobSystemWorker::JobSystemWorker(JobSystem* jobsystem)
		: _jobsystem(jobsystem), Active(false), _isRunning(false), _shutdownRequested(false),
		_modifyingThread(), _highPriorityTaskQueue(), _normalPriorityTaskQueue(), _lowPriorityTaskQueue(),
		_scheduledJobsMutex(), _scheduledJobs(),
		_isRunningMutex(), _worker(),
		_isBusy(false)
	{
	}

	JobSystemWorker::JobSystemWorker(const JobSystemWorker& worker)
		: _jobsystem(worker._jobsystem)
	{
		Active.store(false);
	}

	JobSystemWorker::~JobSystemWorker()
	{
		if (_worker.get_id() != std::thread::id()) {
			_shutdownRequested.store(true);
			if (_worker.joinable()) {
				_worker.join();
			}
			else {
				_worker.detach();
			}
		}
	}

	bool JobSystemWorker::IsActive() const
	{
		return Active.load();
	}

	void JobSystemWorker::WaitForShutdown()
	{
		const std::unique_lock ul(_isRunningMutex);
	}

	void JobSystemWorker::Start()
	{
		if (!_jobsystem->Active.load()) {
			return;
		}

		_modifyingThread.lock();

	#ifndef JBSYSTEM_KEEP_ALIVE
		if (IsActive() || _shutdownRequested.load()) {
			_modifyingThread.unlock();
			return;
		}

		if (_worker.get_id() != std::thread::id()) {
			if (_worker.joinable())
				_worker.join();
			else
				_worker.detach();
		}

		_shutdownRequested.store(false);
		Active.store(true);
		_worker = std::thread([this](JobSystemWorker* worker) {
			std::unique_lock ul(_isRunningMutex);
			_isRunning.store(true);

			worker->KeepAliveLoop();
			_isRunning.store(false);
			Active.store(false);
		}, this);
	#else
		if(!_shutdownRequested.load()){
			Active.store(true);
			_worker = std::thread([this](JobSystemWorker* worker) {
				const std::unique_lock ul(_isRunningMutex);
				_isRunning.store(true);

				worker->KeepAliveLoop();
				_isRunning.store(false);
				Active.store(false);
			}, this);
		}
	#endif

		_modifyingThread.unlock();
		_jobsystem->OptimizePerformance(); // Determin best scaling options

	}

	int JobSystemWorker::WorkerId()
	{
		return _jobsystem->GetWorkerId(this);
	}

	Job* JobSystemWorker::TryTakeJob(const JobPriority& maxTimeInvestment)
	{
		if (!_modifyingThread.try_lock()) {
			return nullptr;
		}


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

	bool JobSystemWorker::IsJobInQueue(const JobId& jobId) {

		const int& id = jobId.ID();
		const std::scoped_lock<JbSystem::mutex> lock(_modifyingThread);
		for (const auto& highPriorityJob : _highPriorityTaskQueue)
		{
			if (highPriorityJob->GetId().ID() == id) {
				return true;
			}
		}

		for (const auto& normalPriorityJob : _normalPriorityTaskQueue)
		{
			if (normalPriorityJob->GetId().ID() == id) {
				return true;
			}
		}

		for (const auto& lowPriorityJob : _lowPriorityTaskQueue)
		{
			if (lowPriorityJob->GetId().ID() == id) {
				return true;
			}
		}

		return false;
	}

	size_t JobSystemWorker::ScheduledJobCount()
	{
		_scheduledJobsMutex.lock();
		const size_t scheduledCount = _scheduledJobs.size();
		_scheduledJobsMutex.unlock();
		return scheduledCount;
	}

	void JobSystemWorker::UnScheduleJob(const JobId& previouslyScheduledJob)
	{
		assert(!IsJobInQueue(previouslyScheduledJob)); // In case the task is still scheduled then it wasn't removed properly

		const int& id = previouslyScheduledJob.ID();
		_modifyingThread.lock();
		_scheduledJobsMutex.lock();
		assert(_scheduledJobs.contains(id));
		_scheduledJobs.erase(id);
		_scheduledJobsMutex.unlock();
		_modifyingThread.unlock();
	}

	void JobSystemWorker::ScheduleJob(const JobId& jobId)
	{
		const int& id = jobId.ID();

		_modifyingThread.lock();
		_scheduledJobsMutex.lock();
		assert(!_scheduledJobs.contains(id));
		_scheduledJobs.emplace(id);
		_scheduledJobsMutex.unlock();
		_modifyingThread.unlock();
	}

	bool JobSystemWorker::GiveJob(Job* const& newJob, const JobPriority& priority)
	{
		if (!IsActive()) {
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

	void JobSystemWorker::GiveFutureJob(const JobId& jobId)
	{
		_scheduledJobsMutex.lock();
		_scheduledJobs.emplace(jobId.ID());
		_scheduledJobsMutex.unlock();
	}

	void JobSystemWorker::GiveFutureJobs(const std::vector<Job*>& newjobs, int startIndex, int size)
	{
		_scheduledJobsMutex.lock();
		for (size_t i = 0; i < size; i++)
		{
			_scheduledJobs.emplace(newjobs[startIndex + i]->GetId().ID());
		}
		_scheduledJobsMutex.unlock();

	}

	void JobSystemWorker::FinishJob(Job*& job)
	{
		const JobId& jobId = job->GetId();
		UnScheduleJob(jobId);
		job->Free();
	}

	bool JobSystemWorker::IsJobScheduled(const JobId& jobId)
	{
		_scheduledJobsMutex.lock();
		const bool contains = _scheduledJobs.contains(jobId.ID());
		_scheduledJobsMutex.unlock();
		return contains;
	}

	JobSystemWorker::PausedJob::PausedJob(Job* affectedJob, JobSystemWorker& worker)
		: AffectedJob(affectedJob), Worker(worker)
	{
	}

}