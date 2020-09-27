#include "JobSystem.h"

#include <functional>
#include <iostream>
#include <algorithm>

namespace JbSystem {
	JobSystem::JobSystem(int threadCount) {
		ReConfigure(threadCount);
	}

	JobSystem::~JobSystem()
	{
		Shutdown();
	}

	void JobSystem::ReConfigure(const int threadCount)
	{
		if (threadCount <= 1) {
			std::cout << "JobSystem cannot start with 0-1 workers" << std::endl;
			return;
		}

		bool firstStartup = _workerCount == 0;

		std::vector<Job*>* jobs;
		if (!firstStartup) {
			//std::cout << "JobSystem is shutting down" << std::endl;

			//Shut down workers safely, and extract scheduled jobs
			jobs = Shutdown();
		}
		_jobsMutex.lock();

		// Grab last batch of jobs that were created in the time that lock was released in shutdown methon till now
		if (!firstStartup) {
			auto lastBatchOfJobs = StealAllJobsFromWorkers();
			jobs->insert(jobs->begin(), lastBatchOfJobs->begin(), lastBatchOfJobs->end());
		}

		//std::cout << "JobSystem is starting" << std::endl;

		//Change amount of worker threads
		_workerCount = threadCount;
		_workers.reserve(threadCount);
		for (int i = 0; i < _workerCount; i++)
		{
			// set callback function for worker threads to call the execute job on the job system
			_workers.emplace_back(this);
		}

		for (int i = 0; i < _workerCount; i++)
		{
			// set callback function for worker threads to call the execute job on the job system
			_workers[i].Start();
		}

		_jobsMutex.unlock();

		//Reschedule saved jobs

		if (!firstStartup) {
			auto rescheduleJobFunction = [](auto jobSystem, auto jobs) {
				const size_t totalJobs = jobs->size();
				size_t iteration = 0;
				while (iteration < totalJobs) {
					for (int i = 0; i < jobSystem->_workerCount && iteration < totalJobs; i++)
					{
						jobSystem->_workers[i].GiveJob(jobs->at(iteration));
						iteration++;
					}
				};
			};

			const int rescheduleJob = Schedule(CreateJob(JobPriority::Low, rescheduleJobFunction, this, jobs));

			//wait for rescheduling to be done, then return the caller
			WaitForJobCompletion(rescheduleJob);
			delete jobs;
		}

		//std::cout << "JobSystem started with " << threadCount << " workers!" << std::endl;
	}

	std::vector<Job*>* JbSystem::JobSystem::Shutdown()
	{
		//Wait for jobsystem to finish remaining jobs
		size_t realScheduledJobs = 0;
		while (realScheduledJobs > 0) {
			realScheduledJobs = 0;
			for (int i = 0; i < _workerCount; i++)
			{
				realScheduledJobs += _workers[i]._highPriorityTaskQueue.size();
				realScheduledJobs += _workers[i]._normalPriorityTaskQueue.size();
				realScheduledJobs += _workers[i]._lowPriorityTaskQueue.size();
			}
		}

		//Let worker safely shutdown and complete active last job
		for (int i = 0; i < _workerCount; i++)
		{
			_workers[i].Active = false;
			_workers[i].WaitForShutdown();
		}

		_jobsMutex.lock();

		//Get jobs finished while threads were stopping
		auto allJobs = StealAllJobsFromWorkers();

		_workerCount = 0;
		_workers.clear();
		_schedulesTillMaintainance = _maxSchedulesTillMaintainance;
		_jobsMutex.unlock();
		return allJobs;
	}

	void JobSystem::ExecuteJob(const JobPriority maxTimeInvestment)
	{
		ExecuteJobFromWorker(maxTimeInvestment);
	}

	static std::shared_ptr<JobSystem> JobSystemSingleton;
	std::shared_ptr<JobSystem> JbSystem::JobSystem::GetInstance()
	{
		if (JobSystemSingleton == nullptr) {
			JobSystemSingleton = std::make_shared<JobSystem>();
		}
		return JobSystemSingleton;
	}

	const int JobSystem::Schedule(Job* newjob)
	{
		const int jobId = newjob->GetId();

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
				Schedule(CreateJob(JobPriority::High, [](JobSystem* JobSystem) -> void { JobSystem->Cleanup(); }, this));
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

	const int JobSystem::Schedule(Job* job, const std::vector<int> dependencies)
	{
		//Schedule jobs in the future, then when completed, schedule them for inside workers
		int jobId = ScheduleFutureJob(job);
		WaitForJobCompletion(dependencies,
			[](auto jobsystem, auto job)
			{
				jobsystem->Schedule(job);
			}, this, job);
		return jobId;
	}

	const std::vector<int> JobSystem::Schedule(std::shared_ptr<std::vector<Job*>> newjobs)
	{
		auto returnValue = BatchScheduleJob(newjobs.get());
		return returnValue;
	}

	Job* JobSystem::CreateJob(const JobPriority priority, void(*function)())
	{
		struct VoidJobTag {};
		void* location = boost::singleton_pool<VoidJobTag, sizeof(JobSystemVoidJob)>::malloc();
		auto deconstructorCallback = [](JobSystemVoidJob* job) { boost::singleton_pool<VoidJobTag, sizeof(JobSystemVoidJob)>::free(job); };

		return new(location) JobSystemVoidJob(priority, function, deconstructorCallback);
	}

	std::shared_ptr<std::vector<Job*>> JobSystem::CreateParallelJob(const JobPriority priority, int startIndex, int endIndex, int batchSize, void(*function)(int))
	{
		auto jobs = std::make_shared<std::vector<Job*>>();

		auto parallelFunction = [](auto callback, int startIndex, int endIndex)
		{
			for (int i = startIndex; i < endIndex; i++)
			{
				callback(i);
			}
		};

		int jobStartIndex;
		int jobEndIndex;

		//Schedule and create lambda for all job kinds
		int totalBatches = 0;
		int endOfRange = endIndex - startIndex;
		int CurrentBatchEnd = endOfRange;
		while (CurrentBatchEnd > batchSize) {
			CurrentBatchEnd -= batchSize;

			jobStartIndex = startIndex + endOfRange - ((totalBatches + 1) * batchSize);
			jobEndIndex = startIndex + endOfRange - (totalBatches * batchSize);

			jobs->emplace_back(CreateJob(priority, parallelFunction, function, jobStartIndex, jobEndIndex));
			totalBatches++;
		}

		jobStartIndex = startIndex;
		jobEndIndex = startIndex + endOfRange - (totalBatches * batchSize);

		//Create last job
		jobs->emplace_back(CreateJob(priority, parallelFunction, function, jobStartIndex, jobEndIndex));

		return jobs;
	}

	int JobSystem::ScheduleFutureJob(const Job* newFutureJob)
	{
		const int jobId = newFutureJob->GetId();
		_jobsMutex.lock();
		_scheduledJobs.insert(jobId);
		_jobsMutex.unlock();
		return jobId;
	}

	std::vector<int> JobSystem::BatchScheduleJob(const std::vector<Job*>* newjobs)
	{
#ifdef DEBUG
		if (_workerCount != 0) {
#endif
			std::vector<int> jobIds = BatchScheduleFutureJob(newjobs);

			_jobsMutex.lock();
			_scheduledJobs.insert(jobIds.begin(), jobIds.end());
			_jobsMutex.unlock();

			for (size_t i = 0; i < newjobs->size(); i++)
			{
				int worker = rand() % _workerCount;
				_workers[worker].GiveJob(newjobs->at(i));

				// Make sure that all workers are still running correctly
				_schedulesTillMaintainance--;
				if (_schedulesTillMaintainance == 0) {
					//std::cout << "Validating Jobsystem threads" << std::endl;
					_schedulesTillMaintainance = _maxSchedulesTillMaintainance;

					Schedule(CreateJob(JobPriority::Normal, [](JobSystem* jobsystem) { jobsystem->Cleanup(); }, this));
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

	std::vector<int> JobSystem::BatchScheduleFutureJob(const std::vector<Job*>* newjobs)
	{
		std::vector<int> jobIds;
		const size_t totalAmountOfJobs = newjobs->size();
		jobIds.reserve(totalAmountOfJobs);

		for (size_t i = 0; i < totalAmountOfJobs; i++)
		{
			jobIds.emplace_back(newjobs->at(i)->GetId());
		}

		_jobsMutex.lock();
		if (totalAmountOfJobs > _scheduledJobs.size())
			_scheduledJobs.reserve(totalAmountOfJobs);
		for (size_t i = 0; i < totalAmountOfJobs; i++)
		{
			int jobId = jobIds[i];
			if (!_scheduledJobs.contains(jobId))
				_scheduledJobs.insert(jobId);
		}
		_jobsMutex.unlock();
		return jobIds;
	}

	const std::vector<int> JobSystem::Schedule(std::shared_ptr<std::vector<Job*>> newjobs, const std::vector<int> dependencies)
	{
		//Schedule jobs in the future, then when completed, schedule them for inside workers
		auto jobIds = BatchScheduleFutureJob(newjobs.get());

		WaitForJobCompletion(dependencies,
			[](auto jobSystem, auto callbackJobs)
			{
				jobSystem->Schedule(callbackJobs);
			}, this, newjobs);

		return jobIds;
	}

	bool JobSystem::IsJobCompleted(const int jobId)
	{
		for (int i = 0; i < _workerCount; i++)
		{
			// check if a worker has finished the job
			if (_workers[i].IsJobFinished(jobId)) {
				return true;
			}
		}
		_jobsMutex.lock();
		bool contains = !_scheduledJobs.contains(jobId);
		_jobsMutex.unlock();
		return contains;
	}

	bool JobSystem::WaitForJobCompletion(int jobId, const JobPriority maximumHelpEffort)
	{
		bool jobFinished = IsJobCompleted(jobId);

		while (!jobFinished) {
			ExecuteJob(maximumHelpEffort);// use resources to aid workers instead of sleeping

			jobFinished = IsJobCompleted(jobId);
		}

		return jobFinished;
	}

	bool JobSystem::WaitForJobCompletion(int jobId, int maxMicroSecondsToWait, const JobPriority maximumHelpEffort)
	{
		bool jobFinished = IsJobCompleted(jobId);

		std::chrono::time_point start = std::chrono::steady_clock::now();

		while (!jobFinished) {
			ExecuteJob(maximumHelpEffort);// use resources to aid workers instead of sleeping

			int passedMicroSeconds = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());

			if (passedMicroSeconds < maxMicroSecondsToWait)
				continue;

			jobFinished = IsJobCompleted(jobId);

			if (maxMicroSecondsToWait != 0 && passedMicroSeconds > maxMicroSecondsToWait && !jobFinished)
				return false;
		}

		return jobFinished;
	}

	void JobSystem::WaitForJobCompletion(std::vector<int>& jobIds, JobPriority maximumHelpEffort)
	{
		// Wait for task to complete, allocate boolean on the heap because it's possible that we do not have access to our stack
		bool* finished = new bool(false);
		auto waitLambda = [](bool* finished)
		{
			*finished = true;
		};
		WaitForJobCompletion(jobIds, waitLambda, finished);
		while (!*finished) { ExecuteJob(maximumHelpEffort); }
		delete finished;
	}

	void JobSystem::ExecuteJobFromWorker(const JobPriority maxTimeInvestment)
	{
		int worker = rand() % _workerCount;
		Job* job = _workers[worker].TryTakeJob(maxTimeInvestment);
		if (job != nullptr) {
			job->Run();
			_workers[worker].FinishJob(job);
		}
	}

	void JobSystem::Cleanup()
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

		for (int i = 0; i < _workerCount; i++)
		{
			if (!_workers[i].IsRunning() && _workers[i].Active == true)
				_workers[i].Start();
		}
	}

	std::vector<Job*>* JobSystem::StealAllJobsFromWorkers()
	{
		auto jobs = new std::vector<Job*>();
		jobs->reserve(10000);
		for (int i = 0; i < _workerCount; i++)
		{
			auto job = _workers[i].TryTakeJob();
			while (job != nullptr) {
				jobs->emplace_back(job);
				job = _workers[i].TryTakeJob();
			}
		}
		return jobs;
	}
}