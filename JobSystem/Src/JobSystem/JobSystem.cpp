#include "JobSystem.h"

#include <functional>
#include <iostream>
#include <algorithm>

#include "boost/container/small_vector.hpp"

namespace JbSystem {
	const int MaxThreadDepth = 20;
	static thread_local int threadDepth = 0; // recursion guard, threads must not be able to infinitely go into scopes
	static thread_local bool unwind = false; // when true we can't help with any new jobs, instead we finish all our previous jobs
	static thread_local boost::container::small_vector<const Job*, sizeof(Job*)* MaxThreadDepth> jobStack; // stack of all jobs our current thread is executing
	bool JobInStack(int JobId) {
		for (int i = 0; i < jobStack.size(); i++)
		{
			if (jobStack[i]->GetId() == JobId)
				return true;
		}
		return false;
	}

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
			std::cout << "JobSystem cannot start with 0-1 workers..." << std::endl;
			std::cout << "Therefor it has been started with 2 workers" << std::endl;
			ReConfigure(2);
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
						Job* newJob = jobs->at(iteration);

						jobSystem->_workers[i].GiveJob(newJob, JobPriority::High);
						iteration++;
					}
				};
				delete jobs;
			};

			const Job* rescheduleJob = CreateJobWithParams(rescheduleJobFunction, this, jobs);
			const int rescheduleJobId = Schedule(rescheduleJob, JobPriority::High);

			//wait for rescheduling to be done, then return the caller
			WaitForJobCompletion(rescheduleJobId);
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
		if (threadDepth > MaxThreadDepth && !unwind) { // allow a maximum recursion depth of x
			unwind = true;
			return;
		}

		if (unwind && threadDepth > 0) {
			return;
		}

		unwind = false;
		threadDepth++;
		ExecuteJobFromWorker(maxTimeInvestment);
		threadDepth--;
	}

	static JobSystem* JobSystemSingleton;
	JobSystem* JbSystem::JobSystem::GetInstance()
	{
		if (JobSystemSingleton == nullptr) {
			JobSystemSingleton = new JobSystem();
		}
		return JobSystemSingleton;
	}

	const int JobSystem::Schedule(const Job* newjob, const JobPriority priority)
	{
		const int jobId = newjob->GetId();

		Schedule(GetRandomWorker(), newjob, priority);

		// Make sure that all workers are still running correctly
		_schedulesTillMaintainance--;
		if (_schedulesTillMaintainance <= 0) {
			//std::cout << "Validating Jobsystem threads" << std::endl;
			_schedulesTillMaintainance = _maxSchedulesTillMaintainance;
			Cleanup();
		}

		return jobId;
	}

	const int JobSystem::Schedule(const Job* job, const JobPriority priority, const std::vector<int> dependencies)
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

	const std::vector<int> JobSystem::Schedule(std::vector<const Job*> newjobs, const JobPriority priority)
	{
		return BatchScheduleJob(newjobs, priority);
	}

	const Job* JobSystem::CreateJob(void(*function)())
	{
		struct VoidJobTag {};
		void* location = boost::singleton_pool<VoidJobTag, sizeof(JobSystemVoidJob)>::malloc();
		auto deconstructorCallback = [](JobSystemVoidJob* job) { boost::singleton_pool<VoidJobTag, sizeof(JobSystemVoidJob)>::free(job); };

		return new(location) JobSystemVoidJob(function, deconstructorCallback);
	}

	void JobSystem::DestroyNonScheduledJob(const Job*& job)
	{
		const_cast<Job*&>(job)->Free();
	}

	std::vector<const Job*> JobSystem::CreateParallelJob(int startIndex, int endIndex, int batchSize, void(*function)(const int&))
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

		auto jobs = std::vector<const Job*>();
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

	int JobSystem::ScheduleFutureJob(const Job*& newFutureJob)
	{
		const int workerId = GetRandomWorker();
		int jobId = newFutureJob->GetId();
		_workers[workerId].GiveFutureJob(jobId);
		return workerId;
	}

	std::vector<int> JobSystem::BatchScheduleJob(const std::vector<const Job*>& newjobs, const JobPriority priority)
	{
		auto workerIds = BatchScheduleFutureJob(newjobs);

		for (size_t i = 0; i < workerIds.size(); i++)
		{
			const Job* newJob = newjobs[i];
			Schedule(workerIds[i], newJob, priority);

			// Make sure that all workers are still running correctly
			_schedulesTillMaintainance--;
		}

		if (_schedulesTillMaintainance <= 0) {
			//std::cout << "Validating Jobsystem threads" << std::endl;
			_schedulesTillMaintainance = _maxSchedulesTillMaintainance;

			Cleanup();
		}

		std::vector<int> jobIds;
		size_t jobCount = newjobs.size();
		jobIds.reserve(jobCount);
		for (size_t i = 0; i < jobCount; i++)
		{
			jobIds.emplace_back(newjobs[i]->GetId());
		}

		return jobIds;
	}

	std::vector<int> JobSystem::BatchScheduleFutureJob(const std::vector<const Job*>& newjobs)
	{
		std::vector<int> workerIds;
		const int totalAmountOfJobs = newjobs.size();
		workerIds.resize(totalAmountOfJobs);

		int jobsPerWorker = totalAmountOfJobs / _workerCount;
		int remainer = totalAmountOfJobs % _workerCount;

		for (int i = 0; i < _workerCount; i++)
		{
			for (int j = 0; j < jobsPerWorker; j++)
			{
				workerIds[j + (i * jobsPerWorker)] = i;
			}
		}

		for (int i = 0; i < _workerCount; i++)
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
			int jobId = newjobs[totalAmountOfJobs - i - 1]->GetId();
			_workers[workerId].GiveFutureJob(jobId);
		}

		return workerIds;
	}

	const std::vector<int> JobSystem::Schedule(std::vector<const Job*> newjobs, const JobPriority priority, const std::vector<int> dependencies)
	{
		//Schedule jobs in the future, then when completed, schedule them for inside workers
		std::vector<int> workerIds = BatchScheduleFutureJob(newjobs);

		std::vector<int> jobIds;
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
			JobData(std::vector<int> workerIds, std::vector<const Job*> newjobs)
				: WorkerIds(workerIds.data(), workerIds.data() + workerIds.size()), Newjobs(newjobs.data(), newjobs.data() + newjobs.size()) {
			}

			std::vector<int> WorkerIds;
			std::vector<const Job*> Newjobs;
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

	bool JobSystem::IsJobCompleted(const int jobId)
	{
		for (int i = 0; i < _workerCount; i++)
		{
			// check if a worker has finished the job
			if (_workers[i].IsJobFinished(jobId))
				continue;

			if (_workers[i].IsJobScheduled(jobId))
				return false;
		}
		return true;
	}

	bool JobSystem::WaitForJobCompletion(int jobId, const JobPriority maximumHelpEffort)
	{
		assert(!JobInStack(jobId));  //Job inside workers stack, deadlock encountered!

		bool jobFinished = IsJobCompleted(jobId);

		threadDepth--; // allow waiting job to always execute atleast one recursive task to prevent deadlock
		while (!jobFinished) {
			ExecuteJob(maximumHelpEffort);// use resources to aid workers instead of sleeping

			jobFinished = IsJobCompleted(jobId);
		}
		threadDepth++;

		return jobFinished;
	}

	bool JobSystem::WaitForJobCompletion(int jobId, int maxMicroSecondsToWait, const JobPriority maximumHelpEffort)
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

	void JobSystem::WaitForJobCompletion(std::vector<int>& jobIds, JobPriority maximumHelpEffort)
	{
		for (int i = 0; i < jobIds.size(); i++)
		{
			assert(!JobInStack(jobIds[i]), "Jobs we are waiting for were not given as dependencies and running on the calling thread"); // Job inside workers stack, deadlock encountered!
		}

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
		Job* threadJob = workerThread.TryTakeJob(maxTimeInvestment);
		if (threadJob != nullptr) {

			if (JobInStack(threadJob->GetId())) { // future deadlock encountered, do not execute this job!
				Job* rescheduleJob = const_cast<Job*>(threadJob);
				workerThread.GiveJob(rescheduleJob, JobPriority::High);
				return;
			}

			jobStack.emplace_back(threadJob);
			threadJob->Run();
			auto it = std::find(jobStack.begin(), jobStack.end(), threadJob);
			jobStack.erase(it);
			workerThread.FinishJob(threadJob);
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

	const int JobSystem::Schedule(const int& workerId, const Job*& newjob, const JobPriority priority)
	{
		_workers[workerId].GiveJob(const_cast<Job*&>(newjob), priority); // Job is no longer our possession

		return newjob->GetId();
	}

	const std::vector<int> JobSystem::Schedule(const std::vector<int>& workerIds, const JobPriority priority, std::vector<const Job*> newjobs)
	{
		std::vector<int> jobIds;
		size_t jobCount = newjobs.size();
		jobIds.reserve(jobCount);
		for (size_t i = 0; i < jobCount; i++)
		{
			jobIds.emplace_back(Schedule(workerIds[i], newjobs[i], priority));
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
				jobs->emplace_back(const_cast<Job*>(job)); // Take ownership again
				job = _workers[i].TryTakeJob();
			}
		}
		return jobs;
	}
}