#include "JobSystem.h"

#include <functional>
#include <iostream>
#include <algorithm>

#include "boost/container/small_vector.hpp"
#include <boost/range/adaptor/reversed.hpp>

#include <format>

namespace JbSystem {


	// Prevent wild recursion patterns
	const int maxThreadDepth = 20;
	static thread_local int threadDepth = 0; // recursion guard, threads must not be able to infinitely go into scopes
	static thread_local boost::container::small_vector<const Job*, sizeof(const Job*)* maxThreadDepth> jobStack; // stack of all jobs our current thread is executing

	// Control Optimization cycles
	const int maxOptimizeInCycles = maxThreadDepth * 10;
	static thread_local int optimizeInCycles = 0;

	bool JobInStack(const JobId& jobId) {
		for (int i = 0; i < jobStack.size(); i++)
		{
			if (jobStack.at(i)->GetId() == jobId)
				return true;
		}
		return false;
	}

	bool IsProposedJobIgnoredByJobStack(const JobId& proposedJob) {
		for (int i = 0; i < jobStack.size(); i++)
		{
			const Job* const& job = jobStack.at(i);
			if (job->GetIgnoreCallback() == nullptr)
				continue;

			if (job->GetIgnoreCallback()(proposedJob))
				return true;
		}
		return false;
	}

	JobSystem::JobSystem(int threadCount, WorkerThreadLoop workerLoop)
	: _showStats(true) {
		if (threadCount < 2)
			threadCount = 2;
		WorkerLoop = workerLoop;
		ReConfigure(threadCount);
	}

	JobSystem::~JobSystem()
	{
		for (Job*& leftOverJob : Shutdown()) {
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

		std::vector<Job*> jobs;
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
		_preventIncomingScheduleCalls.store(false);
		for (int i = 0; i < _workerCount; i++)
		{
			// set callback function for worker threads to call the execute job on the job system
			_workers.emplace_back(this);
		}

		// Start critical minimum workers, others will start when job queue grows
		for (int i = 0; i < 2; i++)
		{
			// set callback function for worker threads to call the execute job on the job system
			_workers[i].Start();
		}

		Active = true;

		//Reschedule saved jobs

		if (!firstStartup) {
			auto rescheduleJobFunction = [](JobSystem* jobSystem, auto jobs) {
				const size_t totalJobs = jobs.size();
				size_t iteration = 0;
				while (iteration < totalJobs) {
					for (int i = 0; i < jobSystem->_workerCount && iteration < totalJobs; i++)
					{
						Job*& newJob = jobs.at(iteration);

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

	std::vector<Job*> JbSystem::JobSystem::Shutdown()
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
		allJobs.insert(allJobs.begin(), remainingJobs.begin(), remainingJobs.end());


		for (const auto& job : allJobs) {
			for (auto& worker : _workers) {
				const JobId& id = job->GetId();
				if (worker.IsJobScheduled(id))
					worker.UnScheduleJob(id);
			}
		}

		_activeWorkerCount.store(0);
		_workers.clear();
		return allJobs;
	}

	void JobSystem::WaitForAllJobs()
	{
		bool wasActive = false;
		do {
			ExecuteJob(); // Help complete the remaining jobs

			wasActive = false;
			for (JobSystemWorker& worker : boost::adaptors::reverse(_workers)) {
				if (!worker.IsRunning())
					continue;

				if (!worker.Busy()) {
					if (worker.ScheduledJobCount() == 0) {
						continue;
					}
				}
				wasActive = true;
			}
		} while (wasActive);
	}

	void JobSystem::ExecuteJob(const JobPriority maxTimeInvestment)
	{
		if (threadDepth > maxThreadDepth) { // allow a maximum recursion depth of x

			//Stack was full we might be able to start additional workers
			StartAllWorkers();

			// In case all options are done start additional thread to prevent a deadlock senario
			std::jthread emergencyWorker = std::jthread([&]() { ExecuteJob(); });
			return;
		}

		// Try and run a job
		threadDepth++;
		JobSystemWorker& worker = _workers.at(GetRandomWorker());
		Job* primedJob = TakeJobFromWorker(worker, maxTimeInvestment);
		TryRunJob(worker, primedJob);
		threadDepth--;

		MaybeOptimize();
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

	void JobSystem::ShowStats(bool option)
	{
		_showStats.store(option);
	}

	static JobSystem* JobSystemSingleton;
	JobSystem* JbSystem::JobSystem::GetInstance()
	{
		if (JobSystemSingleton == nullptr) {
			JobSystemSingleton = new JobSystem();
		}
		return JobSystemSingleton;
	}

	JobId JobSystem::Schedule(Job* const& newJob, const JobPriority priority)
	{
		JobId jobId = newJob->GetId();

		JobSystemWorker& worker = _workers.at(GetRandomWorker());
		worker.GiveFutureJob(jobId);
		Schedule(worker, newJob, priority);
		return jobId;
	}

	JobId JobSystem::Schedule(Job* const& job, const JobPriority priority, const std::vector<JobId>& dependencies)
	{
		//Schedule jobs in the future, then when completed, schedule them for inside workers
		int workerId = ScheduleFutureJob(job);
		ScheduleAfterJobCompletion(dependencies,
			[](auto jobsystem, auto workerId, auto job, auto priority)
			{
				jobsystem->Schedule(jobsystem->_workers.at(workerId), job, priority);
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

		
		MaybeHelpLowerQueue(JobPriority::Normal);
		
		return workerId;
	}

	const std::vector<JobId> JobSystem::BatchScheduleJob(const std::vector<Job*>& newjobs, const JobPriority priority)
	{
		auto workerIds = BatchScheduleFutureJob(newjobs);

		for (size_t i = 0; i < workerIds.size(); i++)
		{
			Job* const& newJob = newjobs.at(i);
			Schedule(_workers.at(workerIds.at(i)), newJob, priority);
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
		const int totalAmountOfJobs = int(newjobs.size());
		
		std::vector<int> workerIds;
		workerIds.resize(totalAmountOfJobs);

		int workerCount = _activeWorkerCount.load();
		int jobsPerWorker = totalAmountOfJobs / workerCount;
		int remainer = totalAmountOfJobs % workerCount;

		for (int i = 0; i < workerCount; i++)
		{
			for (int j = 0; j < jobsPerWorker; j++)
			{
				workerIds.at(j + (i * jobsPerWorker)) = i;
			}
		}

		for (int i = 0; i < workerCount; i++)
		{
			_workers[i].GiveFutureJobs(newjobs, i * jobsPerWorker, jobsPerWorker);
			MaybeHelpLowerQueue(JobPriority::Normal);
		}

		for (int i = 0; i < remainer; i++)
		{
			workerIds.at(totalAmountOfJobs - i - 1) = i;
		}

		for (int i = 0; i < remainer; i++)
		{
			int workerId = i;
			workerIds[totalAmountOfJobs - i - 1] = i;
			const JobId jobId = newjobs[totalAmountOfJobs - i - 1]->GetId();
			_workers[workerId].GiveFutureJob(jobId);
			MaybeHelpLowerQueue(JobPriority::Normal);
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

		ScheduleAfterJobCompletion(dependencies,
			scheduleCallback,
			this, new JobData(workerIds, newjobs), priority);

		return jobIds;
	}

	bool JobSystem::IsJobCompleted(const JobId& jobId)
	{
		JobSystemWorker* suggestedWorker = nullptr;
		return IsJobCompleted(jobId, suggestedWorker);
	}

	bool JobSystem::IsJobCompleted(const JobId& jobId, JobSystemWorker*& jobWorker)
	{
		// Try check suggested worker first
		if (jobWorker != nullptr) {
			if (jobWorker->IsJobScheduled(jobId))
				return false;
		}

		for(JobSystemWorker& currentWorker : _workers)
		{
			if (currentWorker.IsJobScheduled(jobId)) {
				jobWorker = &currentWorker;
				return false;
			}
		}
		return true;
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

		ScheduleAfterJobCompletion({ jobId }, waitLambda, finished);
		int waitingPeriod = 0;
		threadDepth--; // allow waiting job to always execute atleast one recursive task to prevent deadlock
		while (!finished->load()) {

			waitingPeriod++;

			if (waitingPeriod > 500) {
				if (_preventIncomingScheduleCalls.load())
					continue; // When we prevent new jobs from being created we must first finish existing jobs

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

			JobSystemWorker& worker = _workers.at(GetRandomWorker());
			Job* primedJob = TakeJobFromWorker(worker, maximumHelpEffort);
			TryRunJob(worker, primedJob);
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

		assert(worker.IsJobScheduled(job->GetId()));

		if (JobInStack(job->GetId()) || IsProposedJobIgnoredByJobStack(job->GetId())) { // future deadlock encountered, do not execute this job! (Try give it back, in case that isn't possible reschedule)
			SafeRescheduleJob(job, worker);

			return nullptr;
		}

		return job;
	}

	void JobSystem::OptimizePerformance()
	{
		if (!_optimizePerformance.try_lock())
			return;

		int votedWorkers = _workerCount;

		int totalJobs = 0;
		for (int i = 0; i < _workerCount; i++)
		{
			JobSystemWorker& worker = _workers[i];
			if (!worker.IsRunning()) {
				votedWorkers--;
				continue;
			}

			worker._scheduledJobsMutex.lock();
			totalJobs += static_cast<int>(worker._scheduledJobs.size());
			worker._scheduledJobsMutex.unlock();
		}

		votedWorkers++; // Increase to include main
		int averageJobsPerWorker = totalJobs / votedWorkers;


		if (averageJobsPerWorker > maxThreadDepth / 2 && _activeWorkerCount.load() >= _workerCount)
			_preventIncomingScheduleCalls.store(true);
		else
			_preventIncomingScheduleCalls.store(false);

		if (averageJobsPerWorker > 1.0) {
			if (_activeWorkerCount < _workerCount) {
				_activeWorkerCount.store(_activeWorkerCount.load() + 1);
			}
		}
		else if (_activeWorkerCount > 2) {
			_activeWorkerCount.store(_activeWorkerCount.load() - 1);
		}


		// Start workers that aren't active
		for (int i = 0; i < _activeWorkerCount.load(); i++)
		{
			JobSystemWorker& worker = _workers.at(i);

			if (!worker.IsRunning())
				worker.Start();
		}

		if (_showStats.load())
		{
			std::string outputString = std::format("\33[2K \r JobSystem Workers: {}, Accepting new jobs: {}, total Jobs: {}  Average Jobs: {}\r", votedWorkers, int(!_preventIncomingScheduleCalls.load()), totalJobs, averageJobsPerWorker);
			std::cout << outputString;
		}
		
		// In case worker 0 or 1 has stopped make sure to restart it
		if (!_workers.at(0).IsRunning())
			_workers.at(0).Start();
		if (!_workers.at(1).IsRunning())
			_workers.at(1).Start();

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
			JobSystemWorker& newWorker = _workers.at(GetRandomWorker());
			newWorker.GiveFutureJob(job->GetId());
			Schedule(newWorker, job, JobPriority::High);
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

	bool JobSystem::TryRunJob(JobSystemWorker& worker, Job*& currentJob)
	{
		if (currentJob == nullptr)
			return false;

		for (auto& currentWorker : _workers)
		{
			std::scoped_lock<JbSystem::mutex> lock(currentWorker._jobsRequiringIgnoringMutex);
			for (const auto& jobWithIgnores : currentWorker._jobsRequiringIgnoring)
			{
				// Do not execute the proposed job if it's forbidden by other jobs currently being executed
				if (jobWithIgnores->GetIgnoreCallback()(currentJob->GetId())) {
					SafeRescheduleJob(currentJob, worker);
					return false;
				}
			}
		}

		RunJob(worker, currentJob);
		return true;
	}

	void JobSystem::RunJob(JobSystemWorker& worker, Job*& currentJob)
	{
		assert(!JobInStack(currentJob->GetId()));

		jobStack.emplace_back(currentJob);

		const IgnoreJobCallback& callback = currentJob->GetIgnoreCallback();
		if (callback)
		{
			std::scoped_lock<JbSystem::mutex> lock(worker._jobsRequiringIgnoringMutex);
			worker._jobsRequiringIgnoring.emplace(currentJob);
		}

		currentJob->Run();

		if (callback)
		{
			std::scoped_lock<JbSystem::mutex> lock(worker._jobsRequiringIgnoringMutex);
			worker._jobsRequiringIgnoring.erase(currentJob);
		}

		for (size_t i = 0; i < jobStack.size(); i++)
		{
			if (jobStack.at(i)->GetId() == currentJob->GetId()) {
				jobStack.erase(jobStack.begin() + i);
				break;
			}

		}

		worker.FinishJob(currentJob);
	}

	int JobSystem::GetRandomWorker()
	{
		return rand() % _activeWorkerCount.load();
	}

	JobId JobSystem::Schedule(JobSystemWorker& worker, Job* const& newJob, const JobPriority priority)
	{
		const JobId& id = newJob->GetId();

		worker._modifyingThread.lock();
		worker._scheduledJobsMutex.lock();
		assert(worker._scheduledJobs.contains(id.ID()));

		if (priority == JobPriority::High) {
			worker._highPriorityTaskQueue.emplace_back(newJob);
		}

		else if (priority == JobPriority::Normal) {
			worker._normalPriorityTaskQueue.emplace_back(newJob);
		}

		else if (priority == JobPriority::Low) {
			worker._lowPriorityTaskQueue.emplace_back(newJob);
		}

		worker._modifyingThread.unlock();
		worker._scheduledJobsMutex.unlock();

		MaybeHelpLowerQueue(priority == JobPriority::High ? JobPriority::Normal : JobPriority::Low);

		MaybeOptimize();

		return id;
	}

	void JobSystem::SafeRescheduleJob(Job* const& oldJob, JobSystemWorker& oldWorker)
	{
		const JobId& id = oldJob->GetId();

		while (true) {
			// Try to schedule in either one of the required threads, in case it's not possible throw error
			if (!oldWorker.IsRunning())
				oldWorker.Start();

			assert(!oldWorker.IsJobInQueue(id));
			assert(oldWorker.IsJobScheduled(id));

			if (oldWorker.GiveJob(oldJob, JobPriority::High)) {
				return;
			}
		}
	}

	const std::vector<JobId> JobSystem::Schedule(const std::vector<int>& workerIds, const JobPriority priority, const std::vector<Job*>& newjobs)
	{
		std::vector<JobId> jobIds;
		size_t jobCount = newjobs.size();
		jobIds.reserve(jobCount);
		for (size_t i = 0; i < jobCount; i++)
		{
			jobIds.emplace_back(Schedule(_workers.at(workerIds.at(i)), newjobs.at(i), priority));
		}

		return jobIds;
	}

	std::vector<Job*> JobSystem::StealAllJobsFromWorkers()
	{
		std::vector<Job*> jobs;
		jobs.reserve(10000);
		for (int i = 0; i < _workerCount; i++)
		{
			JobSystemWorker& worker = _workers.at(i);
			while (worker.ScheduledJobCount() > 0) {
				Job* job = worker.TryTakeJob();
				if (job == nullptr)
					continue;

				jobs.emplace_back(job);
				job = worker.TryTakeJob();

			}
		}
		return jobs;
	}

	void JobSystem::MaybeOptimize()
	{
		optimizeInCycles--;
		if (optimizeInCycles > 0)
			return;
		optimizeInCycles = maxOptimizeInCycles;

		// Optimize performance once in a while
		int remaining = _jobExecutionsTillOptimization.load();
		_jobExecutionsTillOptimization.store(_jobExecutionsTillOptimization.load() - 1);
		if (remaining < 1) {
			_jobExecutionsTillOptimization.store(_maxJobExecutionsBeforePerformanceOptimization);
			OptimizePerformance();
		}
	}
	void JobSystem::MaybeHelpLowerQueue(const JobPriority& priority)
	{
		if (_preventIncomingScheduleCalls.load())
			ExecuteJob(priority);
	}
}