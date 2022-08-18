#include "JobSystem.h"

#include <functional>
#include <iostream>
#include <algorithm>

#include "boost/container/small_vector.hpp"
#include <boost/range/adaptor/reversed.hpp>

namespace JbSystem {
	const int MaxThreadDepth = 20;
	static thread_local int threadDepth = 0; // recursion guard, threads must not be able to infinitely go into scopes
	static thread_local bool unwind = false; // when true we can't help with any new jobs, instead we finish all our previous jobs
	static thread_local boost::container::small_vector<const Job*, sizeof(Job*)* MaxThreadDepth> jobStack; // stack of all jobs our current thread is executing
	bool JobInStack(const JobId& JobId) {
		for (int i = 0; i < jobStack.size(); i++)
		{
			if (jobStack[i]->GetId() == JobId)
				return true;
		}
		return false;
	}

	JobSystem::JobSystem(int threadCount, WorkerThreadLoop workerLoop) {
		if (threadCount < 2)
			threadCount = 2;
		WorkerLoop = workerLoop;
		ReConfigure(threadCount);
	}

	JobSystem::~JobSystem()
	{
		for (Job*& leftOverJob : *Shutdown()) {
			leftOverJob->Free();
		}
	}

	void JobSystem::ReConfigure(const int threadCount)
	{
		if (threadCount <= 1) {
			std::cout << "JobSystem cannot start with 0-1 workers..." << std::endl;
			std::cout << "Therefor it has been started with 2 workers" << std::endl;
			ReConfigure(2);
			return;
		}

		bool firstStartup = _activeWorkerCount.load() == 0;

		std::shared_ptr<std::vector<Job*>> jobs;
		if (!firstStartup) {
			//std::cout << "JobSystem is shutting down" << std::endl;

			//Shut down workers safely, and extract scheduled jobs
			jobs = Shutdown();
		}

		//std::cout << "JobSystem is starting" << std::endl;

		//Change amount of worker threads
		_workerCount = threadCount;
		_activeWorkerCount.store(_workerCount);
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
			auto rescheduleJobFunction = [](JobSystem* jobSystem, auto jobs) {
				const size_t totalJobs = jobs->size();
				size_t iteration = 0;
				while (iteration < totalJobs) {
					for (int i = 0; i < jobSystem->_workerCount && iteration < totalJobs; i++)
					{
						Job*& newJob = jobs->at(iteration);

						JobSystemWorker& worker = jobSystem->_workers.at(i);
						worker.ScheduleJob(newJob->GetId());
						if (!worker.GiveJob(newJob, JobPriority::High)) {
							jobSystem->SafeRescheduleJob(newJob, worker);
						}
						iteration++;
					}
				};
			};

			Job* rescheduleJob = CreateJobWithParams(rescheduleJobFunction, this, jobs);
			const JobId& rescheduleJobId = Schedule(rescheduleJob, JobPriority::High);

			//wait for rescheduling to be done, then return the caller
			WaitForJobCompletion(rescheduleJobId);
		}

		//std::cout << "JobSystem started with " << threadCount << " workers!" << std::endl;
	}

	std::shared_ptr<std::vector<Job*>> JbSystem::JobSystem::Shutdown()
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


		//Get jobs finished while threads were stopping
		auto allJobs = StealAllJobsFromWorkers();

		bool wasActive = false;
		do {
			wasActive = false;
			for (JobSystemWorker& worker : boost::adaptors::reverse(_workers)) {
				if (!worker.IsRunning())
					continue;

				if (!worker.Busy()) {
					worker.RequestShutdown();
				}
				wasActive = true;
			}
		} while (wasActive);

		//Let worker safely shutdown and complete active last job
		for (JobSystemWorker& worker : _workers) {
			worker.WaitForShutdown();
		}

		auto remainingJobs = StealAllJobsFromWorkers();
		allJobs->insert(allJobs->begin(), remainingJobs->begin(), remainingJobs->end());


		for (const auto& job : *allJobs) {
			for (auto& worker : _workers) {
				const JobId& id = job->GetId();
				if (worker.IsJobScheduled(id))
					worker.UnScheduleJob(id);
			}
		}

		_activeWorkerCount.store(0);
		_workers.clear();
		_schedulesTillMaintainance = _maxSchedulesTillMaintainance;
		return allJobs;
	}

	void JobSystem::ExecuteJob(const JobPriority maxTimeInvestment)
	{
		if (threadDepth > MaxThreadDepth && !unwind) { // allow a maximum recursion depth of x
			unwind = true;
			
			//Stack was full we might be able to start additional workers
			OptimizePerformance();
			StartAllWorkers();

			return;
		}

		if (unwind && threadDepth > 0) {
			return;
		}

		unwind = false;
		threadDepth++;
		{
			JobSystemWorker& worker = _workers[GetRandomWorker()];
			Job* primedJob = TakeJobFromWorker(worker, maxTimeInvestment);

			if (primedJob != nullptr) {
				jobStack.emplace_back(primedJob);
				primedJob->Run();
				auto it = std::find(jobStack.begin(), jobStack.end(), primedJob);
				jobStack.erase(it);
				worker.FinishJob(primedJob);
			}
		}
		threadDepth--;
	}

	int JobSystem::GetWorkerCount()
	{
		return _workerCount;
	}

	int JobSystem::GetActiveWorkerCount()
	{
		return _activeWorkerCount.load();
	}

	int JobSystem::GetWorkerId(JobSystemWorker* worker)
	{
		for (int i = 0; i < _workers.size(); i++)
		{
			if (&_workers.at(i) == worker)
				return i;
		}
		return -1;
	}

	static JobSystem* JobSystemSingleton;
	JobSystem* JbSystem::JobSystem::GetInstance()
	{
		if (JobSystemSingleton == nullptr) {
			JobSystemSingleton = new JobSystem();
		}
		return JobSystemSingleton;
	}

	JobId JobSystem::Schedule(Job* const& newjob, const JobPriority priority)
	{
		JobId jobId = newjob->GetId();

		Schedule(GetRandomWorker(), newjob, priority);

		// Make sure that all workers are still running correctly
		_schedulesTillMaintainance--;
		if (_schedulesTillMaintainance < 0) {
			//std::cout << "Validating Jobsystem threads" << std::endl;
			_schedulesTillMaintainance = _maxSchedulesTillMaintainance * _activeWorkerCount;

			OptimizePerformance();
			
			StartAllWorkers();

			Cleanup();

		}

		return jobId;
	}

	JobId JobSystem::Schedule(Job* const& job, const JobPriority priority, const std::vector<JobId>& dependencies)
	{
		//Schedule jobs in the future, then when completed, schedule them for inside workers
		int workerId = ScheduleFutureJob(job);
		WaitForJobCompletion(dependencies,
			[](auto jobsystem, auto workerId, auto job, auto priority)
			{
				jobsystem->Schedule(workerId, job, priority);
			}, this, workerId, job, priority);
		return job->GetId();
	}

	const std::vector<JobId> JobSystem::Schedule(const std::vector<Job*>& newjobs, const JobPriority priority)
	{
		return BatchScheduleJob(newjobs, priority);
	}

	Job* JobSystem::CreateJob(void(*function)())
	{
		struct VoidJobTag {};
		void* location = boost::singleton_pool<VoidJobTag, sizeof(JobSystemVoidJob)>::malloc();
		auto deconstructorCallback = [](JobSystemVoidJob* const& job) { boost::singleton_pool<VoidJobTag, sizeof(JobSystemVoidJob)>::free(job); };

		return new(location) JobSystemVoidJob(function, deconstructorCallback);
	}

	void JobSystem::DestroyNonScheduledJob(Job*& job)
	{
		job->Free();
	}

	std::vector<Job*> JobSystem::CreateParallelJob(int startIndex, int endIndex, int batchSize, void(*function)(const int&))
	{
		if (batchSize < 1)
			batchSize = 1;

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
			totalBatches++;
		}

		auto jobs = std::vector<Job*>();
		jobs.reserve(totalBatches + 1);

		for (int i = 0; i < totalBatches; i++)
		{
			jobStartIndex = startIndex + (i * batchSize);
			jobEndIndex = startIndex + ((i + 1) * batchSize);

			jobs.emplace_back(CreateJobWithParams(parallelFunction, function, jobStartIndex, jobEndIndex));
		}

		jobStartIndex = startIndex + (totalBatches * batchSize);
		jobEndIndex = endIndex;

		//Create last job
		jobs.emplace_back(CreateJobWithParams(parallelFunction, function, jobStartIndex, jobEndIndex));

		return jobs;
	}

	int JobSystem::ScheduleFutureJob(Job* const& newFutureJob)
	{
		const int workerId = GetRandomWorker();
		const JobId& jobId = newFutureJob->GetId();
		_workers[workerId].GiveFutureJob(jobId);
		return workerId;
	}

	const std::vector<JobId> JobSystem::BatchScheduleJob(const std::vector<Job*>& newjobs, const JobPriority priority)
	{
		auto workerIds = BatchScheduleFutureJob(newjobs);

		for (size_t i = 0; i < workerIds.size(); i++)
		{
			Job* const& newJob = newjobs.at(i);
			Schedule(workerIds.at(i), newJob, priority);

			// Make sure that all workers are still running correctly
			_schedulesTillMaintainance--;
		}

		if (_schedulesTillMaintainance <= 0) {
			//std::cout << "Validating Jobsystem threads" << std::endl;
			_schedulesTillMaintainance = _maxSchedulesTillMaintainance;

			Cleanup();
		}

		std::vector<JobId> jobIds;
		size_t jobCount = newjobs.size();
		jobIds.reserve(jobCount);
		for (size_t i = 0; i < jobCount; i++)
		{
			jobIds.emplace_back(newjobs.at(i)->GetId());
		}

		return jobIds;
	}

	const std::vector<int> JobSystem::BatchScheduleFutureJob(const std::vector<Job*>& newjobs)
	{
		std::vector<int> workerIds;
		const int totalAmountOfJobs = int(newjobs.size());
		workerIds.resize(totalAmountOfJobs);

		int workerCount = _activeWorkerCount.load();
		int jobsPerWorker = totalAmountOfJobs / workerCount;
		int remainer = totalAmountOfJobs % workerCount;

		for (int i = 0; i < workerCount; i++)
		{
			for (int j = 0; j < jobsPerWorker; j++)
			{
				workerIds[j + (i * jobsPerWorker)] = i;
			}
		}

		for (int i = 0; i < workerCount; i++)
		{
			_workers[i].GiveFutureJobs(newjobs, i * jobsPerWorker, jobsPerWorker);
		}

		for (int i = 0; i < remainer; i++)
		{
			workerIds[totalAmountOfJobs - i - 1] = i;
		}

		for (int i = 0; i < remainer; i++)
		{
			int workerId = i;
			workerIds[totalAmountOfJobs - i - 1] = i;
			const JobId jobId = newjobs[totalAmountOfJobs - i - 1]->GetId();
			_workers[workerId].GiveFutureJob(jobId);
		}

		return workerIds;
	}

	const std::vector<JobId> JobSystem::Schedule(const std::vector<Job*>& newjobs, const JobPriority priority, const std::vector<JobId>& dependencies)
	{
		//Schedule jobs in the future, then when completed, schedule them for inside workers
		std::vector<int> workerIds = BatchScheduleFutureJob(newjobs);

		std::vector<JobId> jobIds;
		size_t jobCount = newjobs.size();
		jobIds.reserve(jobCount);
		for (size_t i = 0; i < jobCount; i++)
		{
			jobIds.emplace_back(newjobs[i]->GetId());
		}

		// internal struct to house data needed inside the job. these are housed here because of challanges.
		// These challanges were, having a data type that is copyable as parameters of the callback
		// lifetime of 'workerids' and 'newjobs' when executing the callback on another thread.
		struct JobData {
			JobData(std::vector<int> workerIds, const std::vector<Job*>& newjobs)
				: WorkerIds(workerIds.data(), workerIds.data() + workerIds.size()), Newjobs(newjobs.data(), newjobs.data() + newjobs.size()) {
			}

			std::vector<int> WorkerIds;
			const std::vector<Job*> Newjobs;
		};

		auto scheduleCallback = [](JobSystem* jobSystem, JobData* jobData, const JobPriority priority)
		{
			jobSystem->Schedule(jobData->WorkerIds, priority, jobData->Newjobs);
			delete jobData;
		};

		WaitForJobCompletion(dependencies,
			scheduleCallback,
			this, new JobData(workerIds, newjobs), priority);

		return jobIds;
	}

	bool JobSystem::IsJobCompleted(const JobId& jobId)
	{
		for (int i = 0; i < _workerCount; i++)
		{
			// check if a worker has finished the job
			if (_workers.at(i).IsJobFinished(jobId))
				return true;

			if (_workers.at(i).IsJobScheduled(jobId))
				return false;
		}
		return true;
	}

	bool JobSystem::IsJobScheduled(const JobId& jobId)
	{
		for (int i = 0; i < _workerCount; i++)
		{
			if (_workers.at(i).IsJobScheduled(jobId))
				return true;
		}
		return false;
	}

	void JobSystem::WaitForJobCompletion(const JobId& jobId, JobPriority maximumHelpEffort)
	{
		assert(!JobInStack(jobId));  //Job inside workers stack, deadlock encountered!


		struct FinishedTag {};
		void* location = boost::singleton_pool<FinishedTag, sizeof(std::atomic<bool>)>::malloc();

		// Wait for task to complete, allocate boolean on the heap because it's possible that we do not have access to our stack
		std::atomic<bool>* finished = new(location) std::atomic<bool>(false);
		auto waitLambda = [](std::atomic<bool>* finished)
		{
			finished->store(true);
		};

		WaitForJobCompletion({ jobId }, waitLambda, finished);
		int waitingPeriod = 0;
		threadDepth--; // allow waiting job to always execute atleast one recursive task to prevent deadlock
		while (!finished->load()) {
			ExecuteJob(maximumHelpEffort);
			waitingPeriod++;

			if (waitingPeriod > 500) {

				switch (maximumHelpEffort)
				{
				case JbSystem::JobPriority::None:
					break;
				case JbSystem::JobPriority::High:
					maximumHelpEffort = JobPriority::Normal;
					break;
				case JbSystem::JobPriority::Normal:
					maximumHelpEffort = JobPriority::Low;
					break;
				case JbSystem::JobPriority::Low:
					break;
				default:
					assert(false);
					break;
				}

				waitingPeriod = 0;
				RescheduleWorkerJobsFromInActiveWorkers();
			}
		}

		boost::singleton_pool<FinishedTag, sizeof(std::atomic<bool>)>::free(finished);
		threadDepth++;
	}

	bool JobSystem::WaitForJobCompletion(const JobId& jobId, int maxMicroSecondsToWait, const JobPriority maximumHelpEffort)
	{
		assert(!JobInStack(jobId)); //Job inside workers stack, deadlock encountered!

		bool jobFinished = IsJobCompleted(jobId);

		std::chrono::time_point start = std::chrono::steady_clock::now();

		threadDepth--; // allow waiting job to always execute atleast one recursive task to prevent deadlock
		while (!jobFinished) {
			ExecuteJob(maximumHelpEffort); // use resources to aid workers instead of sleeping

			int passedMicroSeconds = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());

			if (passedMicroSeconds < maxMicroSecondsToWait)
				continue;

			jobFinished = IsJobCompleted(jobId);

			if (maxMicroSecondsToWait != 0 && passedMicroSeconds > maxMicroSecondsToWait && !jobFinished)
				return false;
		}
		threadDepth++;

		return jobFinished;
	}

	void JobSystem::WaitForJobCompletion(const std::vector<JobId>& jobIds, JobPriority maximumHelpEffort)
	{
		for (JobId id : jobIds) {
			WaitForJobCompletion(id, maximumHelpEffort);
		}
	}

	Job* JobSystem::TakeJobFromWorker(JobSystemWorker& worker, const JobPriority maxTimeInvestment)
	{
		Job* job = worker.TryTakeJob(maxTimeInvestment);
		if (job == nullptr) // Was not able to take a job from specific worker
			return nullptr;
		

		if (JobInStack(job->GetId())) { // future deadlock encountered, do not execute this job! (Try give it back, in case that isn't possible reschedule)
			SafeRescheduleJob(job, worker);

			return nullptr;
		}

		return job;
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
	}

	void JobSystem::OptimizePerformance()
	{
		if (!_optimizePerformance.try_lock())
			return;

		int activeWorkerCount = _activeWorkerCount.load();
		// Optimize performance, growing worker count or shrinking
		double favorShrink = 0;
		double favorGrow = 0;
		for (int i = 0; i < _workerCount; i++)
		{
			JobSystemWorker& worker = _workers[i];
			if (i > _activeWorkerCount - 1) {
				if(worker.IsRunning())
					favorShrink -= (activeWorkerCount / _workerCount) * 2; // delay shrink when inactive workers are still running, because this will lead to jobs being unbalanced
				continue;
			}

			if (!worker._modifyingThread.try_lock())
				continue;

			size_t lowPriority = worker._lowPriorityTaskQueue.size();
			size_t mediumPriority = worker._normalPriorityTaskQueue.size();
			size_t highPriority = worker._highPriorityTaskQueue.size();
			worker._modifyingThread.unlock();

			size_t bias = 1 + (highPriority * 3) + (mediumPriority * 10) + (lowPriority * 15);

			if (bias == 1)
				favorShrink++;

			if (bias > 10)
				favorGrow++;
		}
		
		if (favorGrow > activeWorkerCount * 0.4f && activeWorkerCount < _workerCount) {
			_activeWorkerCount.store(activeWorkerCount + 1);
			//std::cout << "Growing active workers\n";
		}


		if (favorShrink > activeWorkerCount * 0.4f && activeWorkerCount > 1) {
			_activeWorkerCount.store(activeWorkerCount - 1);
			//std::cout << "Shrinking active workers\n";
		}

		// In case worker 0 has stopped make sure to restart it
		if (!_workers.at(0).IsRunning())
			_workers.at(0).Start();

		// Reschedule jobs already inside inactive workers
		RescheduleWorkerJobsFromInActiveWorkers();
		_optimizePerformance.unlock();
	}

	void JobSystem::StartAllWorkers()
	{
		int workerCount = _activeWorkerCount.load();
		for (int i = 0; i < workerCount; i++)
		{
			if (!_workers[i].IsRunning())
				_workers[i].Start();
		}
	}

	bool JobSystem::RescheduleWorkerJobs(JobSystemWorker& worker)
	{
		Job* job = worker.TryTakeJob(JobPriority::Low);
		while (job != nullptr) {
			worker.UnScheduleJob(job->GetId());
			Schedule(GetRandomWorker(), job, JobPriority::High);
			job = worker.TryTakeJob(JobPriority::Low);
		};

		return true;
	}

	void JobSystem::RescheduleWorkerJobsFromInActiveWorkers()
	{
		// Reschedule jobs already inside inactive workers
		for (int i = _activeWorkerCount.load(); i < _workerCount; i++)
		{
			RescheduleWorkerJobs(_workers[i]);
		}
	}

	int JobSystem::GetRandomWorker()
	{
		return rand() % _activeWorkerCount.load();
	}

	JobId JobSystem::Schedule(const int& workerId, Job* const& newjob, const JobPriority priority)
	{
		JobSystemWorker& worker = _workers.at(workerId);

		const JobId& id = newjob->GetId();
		if (!worker.IsJobScheduled(id))
			worker.ScheduleJob(id);

		if (!worker.GiveJob(newjob, priority))
		{
			SafeRescheduleJob(newjob, worker);
		}

		return id;
	}

	void JobSystem::SafeRescheduleJob(Job* const& oldJob, JobSystemWorker& oldWorker)
	{
		const JobId& id = oldJob->GetId();

		for (size_t i = 0; i < 2; i++)
		{
			JobSystemWorker& worker = _workers.at(i);

			// Try to schedule in either one of the required threads, in case it's not possible throw error
			if (!worker.IsRunning())
				worker.Start();

			worker.ScheduleJob(id);
			if (worker.GiveJob(oldJob, JobPriority::High)) {
				oldWorker.UnScheduleJob(id);
				return;
			}
			worker.UnScheduleJob(id);
		}

		const char* errorMessage = "Jobsystem error, wasn't able to reschedule job due even with previous rescheduling falliure";
		std::cout << errorMessage;
		throw std::runtime_error(errorMessage);
	}

	const std::vector<JobId> JobSystem::Schedule(const std::vector<int>& workerIds, const JobPriority priority, const std::vector<Job*>& newjobs)
	{
		std::vector<JobId> jobIds;
		size_t jobCount = newjobs.size();
		jobIds.reserve(jobCount);
		for (size_t i = 0; i < jobCount; i++)
		{
			jobIds.emplace_back(Schedule(workerIds.at(i), newjobs.at(i), priority));
		}

		return jobIds;
	}

	std::shared_ptr<std::vector<Job*>> JobSystem::StealAllJobsFromWorkers()
	{
		auto jobs = std::make_shared<std::vector<Job*>>();
		jobs->reserve(10000);
		for (int i = 0; i < _workerCount; i++)
		{
			JobSystemWorker& worker = _workers.at(i);
			Job* job = worker.TryTakeJob();
			while (job != nullptr) {
				jobs->emplace_back(job);
				job = worker.TryTakeJob();
			}
		}
		return jobs;
	}
}