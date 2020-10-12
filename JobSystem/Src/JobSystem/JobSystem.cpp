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
		auto remainingJobs = Shutdown();
		delete remainingJobs;
	}

	void JobSystem::ReConfigure(const int threadCount)
	{
		if (threadCount <= 1) {
			std::cout << "JobSystem cannot start with 0-1 workers" << std::endl;
			return;
		}

		bool firstStartup = _workerCount == 0;

		std::vector<Job*>* jobs = nullptr;
		if (!firstStartup) {
			//std::cout << "JobSystem is shutting down" << std::endl;

			//Shut down workers safely, and extract scheduled jobs
			jobs = Shutdown();
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

		Active = true;

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
				delete jobs;
			};

			const int rescheduleJob = Schedule(CreateJob(JobPriority::Low, rescheduleJobFunction, this, jobs));

			//wait for rescheduling to be done, then return the caller
			WaitForJobCompletion(rescheduleJob);
		}

		//std::cout << "JobSystem started with " << threadCount << " workers!" << std::endl;
	}

	std::vector<Job*>* JbSystem::JobSystem::Shutdown()
	{
		//Wait for jobsystem to finish remaining jobs
		bool finished = false;
		while (!finished) {
			for (int i = 0; i < _workerCount; i++)
			{
				if (!_workers[i]._highPriorityTaskQueue.empty()) continue;
				if (!_workers[i]._normalPriorityTaskQueue.empty()) continue;
				if (!_workers[i]._lowPriorityTaskQueue.empty()) continue;
			}
			finished = true;
		}

		Active = false;

		//Let worker safely shutdown and complete active last job
		for (int i = 0; i < _workerCount; i++)
		{
			_workers[i].Active = false;
			_workers[i].WaitForShutdown();
		}

		//Get jobs finished while threads were stopping
		auto allJobs = StealAllJobsFromWorkers();

		_workerCount = 0;
		_workers.clear();
		_schedulesTillMaintainance = _maxSchedulesTillMaintainance;
		return allJobs;
	}

	void JobSystem::ExecuteJob(const JobPriority maxTimeInvestment)
	{
		ExecuteJobFromWorker(maxTimeInvestment);
	}

	static JobSystem* JobSystemSingleton;
	JobSystem* JbSystem::JobSystem::GetInstance()
	{
		if (JobSystemSingleton == nullptr) {
			JobSystemSingleton = new JobSystem();
		}
		return JobSystemSingleton;
	}

	const int JobSystem::Schedule(Job* newjob)
	{
		const int jobId = newjob->GetId();

		Schedule(GetRandomWorker(), newjob);

		// Make sure that all workers are still running correctly
		_schedulesTillMaintainance--;
		if (_schedulesTillMaintainance <= 0) {
			//std::cout << "Validating Jobsystem threads" << std::endl;
			_schedulesTillMaintainance = _maxSchedulesTillMaintainance;
			Cleanup();
		}

		return jobId;
	}

	const int JobSystem::Schedule(Job* job, const std::vector<int> dependencies)
	{
		//Schedule jobs in the future, then when completed, schedule them for inside workers
		int workerId = ScheduleFutureJob(job);
		WaitForJobCompletion(dependencies,
			[](auto jobsystem, auto workerId, auto job)
			{
				jobsystem->Schedule(workerId, job);
			}, this, workerId, job);
		return job->GetId();
	}

	const std::vector<int> JobSystem::Schedule(std::shared_ptr<std::vector<Job*>> newjobs)
	{
		return BatchScheduleJob(newjobs.get());
	}

	Job* JobSystem::CreateJob(const JobPriority priority, void(*function)())
	{
		struct VoidJobTag {};
		void* location = boost::singleton_pool<VoidJobTag, sizeof(JobSystemVoidJob)>::malloc();
		auto deconstructorCallback = [](JobSystemVoidJob* job) { boost::singleton_pool<VoidJobTag, sizeof(JobSystemVoidJob)>::free(job); };

		return new(location) JobSystemVoidJob(priority, function, deconstructorCallback);
	}

	std::shared_ptr<std::vector<Job*>> JobSystem::CreateParallelJob(const JobPriority priority, int startIndex, int endIndex, int batchSize, void(*function)(const int&))
	{
		auto jobs = std::make_shared<std::vector<Job*>>();

		auto parallelFunction = [](auto callback, int startIndex, int endIndex)
		{
			for (int& i = startIndex; i < endIndex; i++)
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
		const int workerId = GetRandomWorker();
		int jobId = newFutureJob->GetId();
		_workers[workerId].GiveFutureJob(jobId);
		return workerId;
	}

	std::vector<int> JobSystem::BatchScheduleJob(const std::vector<Job*>* newjobs)
	{
		auto workerIds = BatchScheduleFutureJob(newjobs);

		for (size_t i = 0; i < workerIds.size(); i++)
		{
			Schedule(workerIds[i], newjobs->at(i));

			// Make sure that all workers are still running correctly
			_schedulesTillMaintainance--;
		}

		if (_schedulesTillMaintainance <= 0) {
			//std::cout << "Validating Jobsystem threads" << std::endl;
			_schedulesTillMaintainance = _maxSchedulesTillMaintainance;

			Cleanup();
		}

		std::vector<int> jobIds;
		size_t jobCount = newjobs->size();
		jobIds.reserve(jobCount);
		for (size_t i = 0; i < jobCount; i++)
		{
			jobIds.emplace_back(newjobs->at(i)->GetId());
		}

		return jobIds;
	}

	std::vector<int> JobSystem::BatchScheduleFutureJob(const std::vector<Job*>* newjobs)
	{
		std::vector<int> workerIds;
		const size_t totalAmountOfJobs = newjobs->size();
		workerIds.reserve(totalAmountOfJobs);

		for (size_t i = 0; i < totalAmountOfJobs; i++)
		{
			int worker = GetRandomWorker();
			workerIds.emplace_back(worker);
		}

		for (size_t i = 0; i < totalAmountOfJobs; i++)
		{
			int workerId = workerIds[i];
			int jobId = newjobs->at(i)->GetId();
			_workers[workerId].GiveFutureJob(jobId);
		}
		return workerIds;
	}

	const std::vector<int> JobSystem::Schedule(std::shared_ptr<std::vector<Job*>> newjobs, const std::vector<int> dependencies)
	{
		//Schedule jobs in the future, then when completed, schedule them for inside workers
		auto workerIds = BatchScheduleFutureJob(newjobs.get());

		WaitForJobCompletion(dependencies,
			[](auto jobSystem, auto workerIds, auto callbackJobs)
			{
				jobSystem->Schedule(workerIds, callbackJobs);
			}, this, workerIds, newjobs);

		std::vector<int> jobIds;
		size_t jobCount = newjobs->size();
		jobIds.reserve(jobCount);
		for (size_t i = 0; i < jobCount; i++)
		{
			jobIds.emplace_back(newjobs->at(i)->GetId());
		}

		return jobIds;
	}

	bool JobSystem::IsJobCompleted(const int jobId)
	{
		for (int i = 0; i < _workerCount; i++)
		{
			// check if a worker has finished the job
			if (!_workers[i].IsJobFinished(jobId)) {
				if (_workers[i].IsJobScheduled(jobId))
					return false;
			}
		}
		return true;
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
		struct FinishedTag {};
		void* location = boost::singleton_pool<FinishedTag, sizeof(std::atomic<bool>)>::malloc();

		// Wait for task to complete, allocate boolean on the heap because it's possible that we do not have access to our stack
		std::atomic<bool>* finished = new(location) std::atomic<bool>(false);
		auto waitLambda = [](std::atomic<bool>* finished)
		{
			finished->store(true);
		};
		WaitForJobCompletion(jobIds, waitLambda, finished);
		while (!finished->load()) { ExecuteJob(maximumHelpEffort); }

		boost::singleton_pool<FinishedTag, sizeof(std::atomic<bool>)>::free(finished);
	}

	void JobSystem::ExecuteJobFromWorker(const JobPriority maxTimeInvestment)
	{
		JobSystemWorker& workerThread = _workers[GetRandomWorker()];
		Job* job = workerThread.TryTakeJob(maxTimeInvestment);
		if (job != nullptr) {
			job->Run();
			workerThread.FinishJob(job);
		}
	}

	void JobSystem::Cleanup()
	{
		//remove deleted jobs
		for (int i = 0; i < _workerCount; i++)
		{
			JobSystemWorker& worker = _workers[i];
			worker._completedJobsMutex.lock();
			worker._completedJobs.clear();
			worker._completedJobsMutex.unlock();
		}

		for (int i = 0; i < _workerCount; i++)
		{
			if (!_workers[i].IsRunning() && _workers[i].Active == true)
				_workers[i].Start();
		}
	}

	int JobSystem::GetRandomWorker()
	{
		return rand() % _workerCount;
	}

	const int JobSystem::Schedule(const int& workerId, Job* newjob)
	{
		_workers[workerId].GiveJob(newjob);

		return newjob->GetId();
	}

	const std::vector<int> JobSystem::Schedule(const std::vector<int>& workerIds, std::shared_ptr<std::vector<Job*>> newjobs)
	{
		std::vector<int> jobIds;
		size_t jobCount = newjobs->size();
		jobIds.reserve(jobCount);
		for (size_t i = 0; i < jobCount; i++)
		{
			jobIds.emplace_back(Schedule(workerIds[i], newjobs->at(i)));
		}

		return jobIds;
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