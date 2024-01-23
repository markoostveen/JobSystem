#include "JobSystem.h"

#include <algorithm>
#include <functional>
#include <iostream>

#include "boost/container/small_vector.hpp"
#include <boost/range/adaptor/reversed.hpp>

#include <string>

namespace JbSystem
{

    // Thread locals
    static thread_local std::uint16_t randomWorkerIndex;

    // Prevent wild recursion patterns
    const int maxThreadDepth                     = 5;
    static thread_local int threadDepth = 0; // recursion guard, threads must not be able to infinitely go into scopes
    static thread_local boost::container::small_vector<const Job*, sizeof(const Job*) * maxThreadDepth>
        jobStack; // stack of all jobs our current thread is executing
    static thread_local bool allowedToLowerQueue         = true;
    static thread_local unsigned int maybeLowerWorkDepth = 0;

    // Control Optimization cycles
    const int maxOptimizeInCycles            = maxThreadDepth * 10;
    static thread_local int optimizeInCycles = 0;

    bool JobInStack(const JobId& jobId)
    {
        for (const auto& job : jobStack)
        {
            if (job->GetId() == jobId)
            {
                return true;
            }
        }
        return false;
    }

    bool IsProposedJobIgnoredByJobStack(const JobId& proposedJob)
    {
        for (const auto& job : jobStack)
        {
            if (job->GetIgnoreCallback() == nullptr)
            {
                continue;
            }

            if (job->GetIgnoreCallback()(proposedJob))
            {
                return true;
            }
        }
        return false;
    }

    JobSystem::JobSystem(unsigned int threadCount, WorkerThreadLoop workerLoop)
        : _showStats(true),
        _enablePeriodicOptimization(true)
#ifdef JobSystem_Analytics_Enabled
        ,_extraSpawnedThreadsCount(0)
#endif
    {
        if (threadCount < _minimumActiveWorkers)
        {
            threadCount = _minimumActiveWorkers;
        }
        WorkerLoop = workerLoop;
        ReConfigure(threadCount);
    }

    JobSystem::~JobSystem()
    {
        if (_workers.empty())
        {
            return;
        }

        for (Job*& leftOverJob : Shutdown())
        {
            leftOverJob->Free();
        }
    }

    void JobSystem::ReConfigure(unsigned int threadCount)
    {
        if (threadCount <= 1)
        {
            std::cout << "JobSystem cannot start with 0-1 workers..." << std::endl;
            std::cout << "Therefor it has been started with 2 workers" << std::endl;
            ReConfigure(_minimumActiveWorkers);
            return;
        }

        const bool firstStartup = _activeWorkerCount.load() == 0;

        std::vector<Job*> jobs;
        if (!firstStartup)
        {
            // std::cout << "JobSystem is shutting down" << std::endl;

            // Shut down workers safely, and extract scheduled jobs
            jobs = Shutdown();
        }

        // std::cout << "JobSystem is starting" << std::endl;

        // Change amount of worker threads
        _workerCount = threadCount;
        _activeWorkerCount.store(_minimumActiveWorkers);
        _workers.reserve(threadCount);
        _preventIncomingScheduleCalls.store(false);
        for (int i = 0; i < _workerCount; i++)
        {
            // set callback function for worker threads to call the execute job on the job system
            _workers.emplace_back(this);
        }

        // Start critical minimum workers, others will start when job queue grows
        for (uint32_t i = 0; i < _minimumActiveWorkers; i++)
        {
            // set callback function for worker threads to call the execute job on the job system
            _workers[i].Start();
        }

        Active.store(true);

        // Reschedule saved jobs

        if (!firstStartup)
        {
            auto rescheduleJobFunction = [this, jobs = std::move(jobs)]()
            {
                const size_t totalJobs = jobs.size();
                size_t iteration       = 0;
                while (iteration < totalJobs)
                {
                    for (int i = 0; i < _workerCount && iteration < totalJobs; i++)
                    {
                        Job* const& newJob = jobs.at(iteration);

                        JobSystemWorker& worker = _workers.at(i);
                        worker.ScheduleJob(newJob->GetId());
                        if (!worker.GiveJob(newJob, JobPriority::High))
                        {
                            JobSystem::SafeRescheduleJob(newJob, worker);
                        }
                        iteration++;
                    }
                };
            };

            Job* rescheduleJob           = CreateJobWithParams(rescheduleJobFunction);
            const JobId& rescheduleJobId = Schedule(rescheduleJob, JobPriority::High);

            // wait for rescheduling to be done, then return the caller
            WaitForJobCompletion(rescheduleJobId);
        }

        // std::cout << "JobSystem started with " << threadCount << " workers!" << std::endl;
    }

    std::vector<Job*> JbSystem::JobSystem::Shutdown()
    {
        // Wait for jobsystem to finish remaining jobs
        WaitForAllJobs();

        Active.store(false);

        bool wasActive = false;
        do
        {
            wasActive = false;
            for (JobSystemWorker& worker : _workers)
            {
                if (!worker._isRunning.load())
                {
                    continue;
                }

                worker.RequestShutdown();
                wasActive = true;
            }

            if (wasActive)
            {
                continue;
            }

            // Let worker safely shutdown and complete active last job
            for (JobSystemWorker& worker : _workers)
            {
                worker.WaitForShutdown();
            }

            // All extra workers must have exited
            _spawnedThreadsMutex.lock();
            for (auto& extraWorker : _spawnedThreadsExecutingIgnoredJobs)
            {
                if (extraWorker.second.joinable())
                {
                    extraWorker.second.join();
                    wasActive = true;
                }
            }
            _spawnedThreadsMutex.unlock();

        } while (wasActive);

        _spawnedThreadsMutex.lock();
        _spawnedThreadsExecutingIgnoredJobs.clear();
        _spawnedThreadsMutex.unlock();

        auto remainingJobs = StealAllJobsFromWorkers();

        for (const auto& job : remainingJobs)
        {
            for (auto& worker : _workers)
            {
                const JobId& id = job->GetId();
                if (worker.IsJobScheduled(id))
                    worker.UnScheduleJob(id);
            }
        }

        _activeWorkerCount.store(0);
        _workers.clear();
        return remainingJobs;
    }

    void JobSystem::WaitForAllJobs()
    {
        bool wasActive = false;
        do
        {
            ExecuteJob(JobPriority::Low); // Help complete the remaining jobs

            wasActive = false;
            for (JobSystemWorker& worker : boost::adaptors::reverse(_workers))
            {
                if (!worker.IsActive())
                {
                    continue;
                }

                if (worker.ScheduledJobCount() == 0)
                {
                    continue;
                }
                if (worker.Busy())
                {
                    wasActive = true;
                }
            }
        } while (wasActive);
    }

    void JobSystem::ExecuteJob(const JobPriority& maxTimeInvestment)
    {
        JobSystemWorker& worker = _workers.at(GetRandomWorker());

        if (worker.ExecutePausedJob())
        {
            return;
        }

        Job* primedJob          = TakeJobFromWorker(worker, maxTimeInvestment);

        if (primedJob == nullptr)
        {
            return;
        }

        if (threadDepth > maxThreadDepth)
        { // allow a maximum recursion depth of x

            // Stack was full we might be able to start additional workers
            StartAllWorkers();

            // In case all options are done start additional thread to prevent a deadlock senario
            RunJobInNewThread(worker, primedJob);
            return;
        }

        // Try and run a job
        if (CanWorkerRunJob(worker, primedJob)) {
            threadDepth++;
            RunJob(worker, primedJob);
            threadDepth--;
        }
        else if (primedJob != nullptr) {
            RunJobInNewThread(worker, primedJob);
        }

        MaybeOptimize();
    }

    void JobSystem::ExecuteJob()
    {
        StartAllWorkers();
        ExecuteJob(JobPriority::Low);
    }

    int JobSystem::GetWorkerCount() const
    {
        return _workerCount;
    }

    int JobSystem::GetActiveWorkerCount() const
    {
        return _activeWorkerCount.load();
    }

    int JobSystem::GetExtraThreadsCount() const
    {
#ifdef JobSystem_Analytics_Enabled
        return _extraSpawnedThreadsCount.load(std::memory_order_acquire);
#else
        return 0;
#endif
    }

    JobSystemWorker& JobSystem::GetWorker(const int& index)
    {
        return _workers.at(index);
    }

    const JobSystemWorker& JobSystem::GetWorker(const int& index) const
    {
        return _workers.at(index);
    }

    int JobSystem::GetWorkerId(JobSystemWorker* worker)
    {
        for (size_t i = 0; i < _workers.size(); i++)
        {
            if (&_workers.at(i) == worker)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void JobSystem::ShowStats(bool option)
    {
        _showStats.store(option);
    }

    void JobSystem::TogglePeriodicWorkerOptimization(bool option)
    {
        _enablePeriodicOptimization.store(option, std::memory_order_release);
    }

    void JobSystem::TogglePreventAcceptingNewSchedules(bool option)
    {
        _preventIncomingScheduleCalls.store(option, std::memory_order_release);
    }

    void JobSystem::IncreaseActiveWorkerCount()
    {
        uint32_t newActiveWorkerCount = _activeWorkerCount.load(std::memory_order_acquire) + 1;
        if (newActiveWorkerCount >= _workerCount)
            return;

        while(_activeWorkerCount.exchange(newActiveWorkerCount, std::memory_order_release) != newActiveWorkerCount);
        
        StartAllWorkers(true);
    }

    void JobSystem::DecreaseActiveWorkerCount()
    {
        uint32_t newActiveWorkerCount = _activeWorkerCount.load(std::memory_order_acquire) - 1;
        if (newActiveWorkerCount <= _minimumActiveWorkers)
            return;

        while (_activeWorkerCount.exchange(newActiveWorkerCount, std::memory_order_release) != newActiveWorkerCount);
    }

    bool JobSystem::IsUsingBuiltInOptimization() const
    {
        return _enablePeriodicOptimization.load(std::memory_order_acquire);
    }

    bool JobSystem::IsAcceptingNewJobs() const
    {
        return !_preventIncomingScheduleCalls.load(std::memory_order_acquire);
    }

    std::chrono::nanoseconds JobSystem::CompleteAnalyticsTick()
    {
#ifdef JobSystem_Analytics_Enabled
        for (auto& worker : _workers) {
            worker.CompleteAnalyticsTick();
        }

        auto newTimePoint = std::chrono::high_resolution_clock::now();
        auto analyticsDelta = newTimePoint - _analyticsTickStartTimePoint;
        _analyticsTickStartTimePoint = newTimePoint;
        return analyticsDelta;
#endif

        return std::chrono::nanoseconds(0);
    }

    static JobSystem* JobSystemSingleton;
    JobSystem* JbSystem::JobSystem::GetInstance()
    {
        if (JobSystemSingleton == nullptr)
        {
            JobSystemSingleton = new JobSystem();
        }
        return JobSystemSingleton;
    }

    void JobSystem::WaitForJobsAndShutdown()
    {
        Active.store(false);
        WaitForAllJobs();
        auto jobsScheduledAfterStoppingWorkers = Shutdown();
        for (Job*& leftOverJob : jobsScheduledAfterStoppingWorkers)
        {
            leftOverJob->Free();
        }
    }

    JobId JobSystem::Schedule(Job* const& newJob, const JobPriority& priority)
    {
        const JobId& jobId = newJob->GetId();

        JobSystemWorker& worker = _workers.at(GetRandomWorker());
        worker.GiveFutureJob(jobId);
        return Schedule(worker, newJob, priority);
    }

    JobId JobSystem::Schedule(Job* const& job, const JobPriority& priority, const std::vector<JobId>& dependencies)
    {
        // Lower job queue
        if (_preventIncomingScheduleCalls.load(std::memory_order_relaxed)) {
            MaybeHelpLowerQueue(priority);
        }

        // Schedule jobs in the future, then when completed, schedule them for inside workers
        const int workerId = ScheduleFutureJob(job);

        ScheduleAfterJobCompletion(
            dependencies, priority,
            [this, workerId, job, priority]()
            { Schedule(_workers.at(workerId), job, priority); });

        return job->GetId();
    }

    std::vector<JobId> JobSystem::Schedule(const std::vector<Job*>& newjobs, const JobPriority& priority)
    {
        return BatchScheduleJob(newjobs, priority);
    }

    Job* JobSystem::CreateJob(void (*function)())
    {
        struct VoidJobTag
        {
        };
        void* location          = boost::singleton_pool<VoidJobTag, sizeof(JobSystemVoidJob)>::malloc();
        auto destructorCallback = [](JobSystemVoidJob* const& job)
        { boost::singleton_pool<VoidJobTag, sizeof(JobSystemVoidJob)>::free(job); };

        return new (location) JobSystemVoidJob(function, destructorCallback);
    }

    void JobSystem::DestroyNonScheduledJob(Job*& job)
    {
        job->Free();
    }

    std::vector<Job*> JobSystem::CreateParallelJob(int startIndex, int endIndex, int batchSize, void (*function)(const int&))
    {
        if (batchSize < 1)
        {
            batchSize = 1;
        }

        auto parallelFunction = [](auto& callback, const int& loopStartIndex, const int& loopEndIndex)
        {
            for (int i = loopStartIndex; i < loopEndIndex; i++)
            {
                callback(i);
            }
        };

        int jobStartIndex = 0;
        int jobEndIndex   = 0;

        // Schedule and create lambda for all job kinds
        int totalBatches     = 0;
        const int endOfRange = endIndex - startIndex;
        int CurrentBatchEnd  = endOfRange;
        while (CurrentBatchEnd > batchSize)
        {
            CurrentBatchEnd -= batchSize;
            totalBatches++;
        }

        auto jobs = std::vector<Job*>();
        jobs.reserve(totalBatches + 1);

        for (int i = 0; i < totalBatches; i++)
        {
            jobStartIndex = startIndex + (i * batchSize);
            jobEndIndex   = startIndex + ((i + 1) * batchSize);

            auto invocationCallback = [parallelFunction, function, jobStartIndex, jobEndIndex]()
            {
                parallelFunction(function, jobStartIndex, jobEndIndex);
            };

            jobs.emplace_back(CreateJobWithParams([parallelFunction, function, jobStartIndex, jobEndIndex]()
                                                  { parallelFunction(function, jobStartIndex, jobEndIndex); }));
        }

        jobStartIndex = startIndex + (totalBatches * batchSize);
        jobEndIndex   = endIndex;

        // Create last job
        jobs.emplace_back(CreateJobWithParams(
            [parallelFunction, function, jobStartIndex, jobEndIndex]() { parallelFunction(function, jobStartIndex, jobEndIndex); }));

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

    std::vector<JobId> JobSystem::BatchScheduleJob(const std::vector<Job*>& newJobs, const JobPriority& priority)
    {
        constexpr int batchSize = 10;
        std::vector<JobId> jobIds(newJobs.size(), JobId(0));

        std::vector<int> workerIds(newJobs.size(), 0);
        auto selectWorkersJob = CreateParallelJob(
            0, static_cast<int>(newJobs.size()), batchSize,
            [](const int& jobIndex, std::vector<int>* workerIds, const std::vector<Job*>* newJobs, JobSystem* jobsystem)
            { workerIds->at(static_cast<size_t>(jobIndex)) = jobsystem->ScheduleFutureJob(newJobs->at(jobIndex)); },
            &workerIds, &newJobs, this);

        auto selectWorkerJobIds = std::vector<JobId>(selectWorkersJob.size(), JobId(0));
        for (size_t i = 0; i < selectWorkersJob.size(); i++)
        {
            selectWorkerJobIds.at(i) = Schedule(selectWorkersJob.at(i), JobPriority::High);
        }

        auto parallelJobs = CreateParallelJob(
            0, static_cast<int>(newJobs.size()), batchSize,
            [](const int& jobIndex, const std::vector<Job*>* newjobs, std::vector<JobId>* jobIds, std::vector<int>* workerIds,
               JobPriority schedulingPriority, JobSystem* jobSystem)
            {
                jobIds->at(jobIndex) = jobSystem->Schedule(
                    workerIds->at(static_cast<size_t>(jobIndex)), newjobs->at(static_cast<size_t>(jobIndex)), schedulingPriority);
            },
            &newJobs, &jobIds, &workerIds, priority, this);

        std::vector<JobId> schedulingJobIds(parallelJobs.size(), JobId(0));
        for (size_t i = 0; i < parallelJobs.size(); i++)
        {
            schedulingJobIds.at(i) = Schedule(parallelJobs.at(i), JobPriority::High, selectWorkerJobIds);
        }
        WaitForJobCompletion(schedulingJobIds, JobPriority::High);

        return jobIds;
    }

    std::vector<int> JobSystem::BatchScheduleFutureJob(const std::vector<Job*>& newjobs)
    {
        const int totalAmountOfJobs = static_cast<int>(newjobs.size());

        std::vector<int> workerIds;
        workerIds.resize(totalAmountOfJobs);

        const int workerCount   = _activeWorkerCount.load();
        const int jobsPerWorker = totalAmountOfJobs / workerCount;
        const int remainer      = totalAmountOfJobs % workerCount;

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
        }

        for (int i = 0; i < remainer; i++)
        {
            workerIds.at(totalAmountOfJobs - i - 1) = i;
        }

        for (int i = 0; i < remainer; i++)
        {
            const int& workerId                  = i;
            workerIds[totalAmountOfJobs - i - 1] = i;
            const JobId jobId                    = newjobs[totalAmountOfJobs - i - 1]->GetId();
            _workers[workerId].GiveFutureJob(jobId);
        }

        return workerIds;
    }

    std::vector<JobId>
    JobSystem::Schedule(const std::vector<Job*>& newjobs, const JobPriority& priority, const std::vector<JobId>& dependencies)
    {
        // Schedule jobs in the future, then when completed, schedule them for inside workers
        const std::vector<int> workerIds = BatchScheduleFutureJob(newjobs);

        std::vector<JobId> jobIds;
        const size_t jobCount = newjobs.size();
        jobIds.reserve(jobCount);
        for (size_t i = 0; i < jobCount; i++)
        {
            jobIds.emplace_back(newjobs[i]->GetId());
        }

        // internal struct to house data needed inside the job. these are housed here because of challanges.
        // These challanges were, having a data type that is copyable as parameters of the callback
        // lifetime of 'workerids' and 'newjobs' when executing the callback on another thread.
        struct JobData
        {
            JobData(std::vector<int> workerIds, const std::vector<Job*>& newjobs) :
                WorkerIds(workerIds.data(), workerIds.data() + workerIds.size()), Newjobs(newjobs.data(), newjobs.data() + newjobs.size())
            {
            }

            std::vector<int> WorkerIds;
            const std::vector<Job*> Newjobs;
        };

        auto* jobData          = new JobData(workerIds, newjobs);
        auto scheduleCallback = [this, jobData, priority]()
        {
            Schedule(jobData->WorkerIds, priority, jobData->Newjobs);
            delete jobData;
        };

        ScheduleAfterJobCompletion(dependencies, priority, scheduleCallback);

        return jobIds;
    }

    bool JobSystem::IsJobCompleted(const JobId& jobId)
    {
        JobSystemWorker* suggestedWorker = nullptr;
        return IsJobCompleted(jobId, suggestedWorker);
    }

    bool JobSystem::AreJobsCompleted(std::vector<JobId>& jobIds)
    {
        for (size_t i = 0; i < jobIds.size(); i++)
        {
            const JobId& jobId = jobIds.at(i);
            if (!IsJobCompleted(jobId))
            {
                jobIds.erase(jobIds.begin(), jobIds.begin() + i);
                return false;
            }
        }
        return true;
    }

    bool JobSystem::IsJobCompleted(const JobId& jobId, JobSystemWorker*& jobWorker)
    {
        // Try check suggested worker first
        if (jobWorker != nullptr)
        {
            if (jobWorker->IsJobScheduled(jobId))
            {
                return false;
            }
        }

        for (JobSystemWorker& currentWorker : _workers)
        {
            if (currentWorker.IsJobScheduled(jobId))
            {
                jobWorker = &currentWorker;
                return false;
            }
        }
        return true;
    }

    void JobSystem::WaitForJobCompletion(const JobId& jobId, JobPriority maximumHelpEffort)
    {
        assert(!JobInStack(jobId)); // Job inside worker stack, deadlock encountered!

        struct FinishedTag
        {
        };
        void* location = boost::singleton_pool<FinishedTag, sizeof(std::atomic<bool>)>::malloc();

        // Wait for task to complete, allocate boolean on the heap because it's possible that we do not have access to our stack
        auto* finished  = new (location) std::atomic<bool>(false);
        auto waitLambda = [finished]() { finished->store(true); };

        ScheduleAfterJobCompletion({jobId}, maximumHelpEffort, waitLambda);
        int waitingPeriod = 0;

        threadDepth--; // allow waiting job to always execute atleast one recursive task to prevent deadlock
        while (!finished->load())
        {

            JobSystemWorker& worker = _workers.at(GetRandomWorker());
            if (!worker.ExecutePausedJob()) {
                // When we can't execute a paused job then try to start a queued job from the worker
                Job* primedJob          = TakeJobFromWorker(worker, maximumHelpEffort);
                if (CanWorkerRunJob(worker, primedJob)) {
                    RunJob(worker, primedJob);
                }
                else if(primedJob != nullptr) {
                    RunJobInNewThread(worker, primedJob);
                }
            }

            waitingPeriod++;

            if (waitingPeriod > 500)
            {

                switch (maximumHelpEffort)
                {
                    case JbSystem::JobPriority::Unknown:
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
                StartAllWorkers();
            }
        }

        boost::singleton_pool<FinishedTag, sizeof(std::atomic<bool>)>::free(finished);
        threadDepth++;
    }

    bool JobSystem::WaitForJobCompletion(const JobId& jobId, int maxMicroSecondsToWait, JobPriority maximumHelpEffort)
    {
        assert(!JobInStack(jobId)); // Job inside workers stack, deadlock encountered!

        bool jobFinished = IsJobCompleted(jobId);

        const std::chrono::time_point start = std::chrono::steady_clock::now();

        threadDepth--; // allow waiting job to always execute atleast one recursive task to prevent deadlock
        while (!jobFinished)
        {
            ExecuteJob(maximumHelpEffort); // use resources to aid workers instead of sleeping

            const int passedMicroSeconds =
                static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());

            if (passedMicroSeconds < maxMicroSecondsToWait)
            {
                continue;
            }

            jobFinished = IsJobCompleted(jobId);

            if (maxMicroSecondsToWait != 0 && passedMicroSeconds > maxMicroSecondsToWait && !jobFinished)
            {
                return false;
            }
        }
        threadDepth++;

        return jobFinished;
    }

    void JobSystem::WaitForJobCompletion(const std::vector<JobId>& jobIds, JobPriority maximumHelpEffort)
    {
        for (const JobId& id : jobIds)
        {
            WaitForJobCompletion(id, maximumHelpEffort);
        }
    }

    Job* JobSystem::TakeJobFromWorker(JobSystemWorker& worker, JobPriority maxTimeInvestment)
    {
        Job* job = worker.TryTakeJob(maxTimeInvestment);
        if (job == nullptr)
        {
            // Was not able to take a job from specific worker
            return job;
        }

        assert(worker.IsJobScheduled(job->GetId()));
        assert(!JobInStack(job->GetId()));

        return job;
    }

    void JobSystem::OptimizePerformance()
    {
        if (!_optimizePerformance.try_lock())
        {
            return;
        }

        int votedWorkers = _workerCount;

        int totalJobs = 0;
        for (int i = 0; i < _workerCount; i++)
        {
            JobSystemWorker& worker = _workers[i];
            if (!worker.IsActive())
            {
                votedWorkers--;
                continue;
            }

            worker._scheduledJobsMutex.lock();
            totalJobs += static_cast<int>(worker._scheduledJobs.size());
            worker._scheduledJobsMutex.unlock();
        }

        votedWorkers++; // Increase to include main

        int extraKnownWorkerCount = 0;
        if (_spawnedThreadsMutex.try_lock())
        {
            extraKnownWorkerCount = static_cast<int>(_spawnedThreadsExecutingIgnoredJobs.size());
            _spawnedThreadsMutex.unlock();
        }
        const int workerCount           = extraKnownWorkerCount + votedWorkers;
        const auto averageJobsPerWorker = static_cast<double>(totalJobs / workerCount);

        if (averageJobsPerWorker > maxThreadDepth / 2 && _activeWorkerCount.load() >= _workerCount && extraKnownWorkerCount <= 1)
        {
            _preventIncomingScheduleCalls.store(true);
        }
        else
        {
            _preventIncomingScheduleCalls.store(false);
        }

        if (averageJobsPerWorker >= 1.0 && _activeWorkerCount < _workerCount && workerCount < _workerCount)
        {
            _activeWorkerCount.store(_activeWorkerCount.load() + 1);
        }
        else if (static_cast<uint32_t>(_activeWorkerCount.load()) > _minimumActiveWorkers)
        {
            _activeWorkerCount.store(_activeWorkerCount.load() - 1);
        }

        // Start workers that aren't active
        for (int i = 0; i < _activeWorkerCount.load(); i++)
        {
            JobSystemWorker& worker = _workers.at(i);

            if (!worker.IsActive())
            {
                worker.Start();
            }
        }

        if (_showStats.load())
        {
            const std::string outputString =
                "\33[2K \r JobSystem Workers: " + std::to_string(workerCount) +
                ", Accepting new jobs: " + std::to_string(static_cast<int>(!_preventIncomingScheduleCalls.load())) +
                ", total Jobs: " + std::to_string(totalJobs) + "  Average Jobs: " + std::to_string(averageJobsPerWorker) + "\r";
            std::cout << outputString;
        }

        // In case worker 0 or 1 has stopped make sure to restart it
        if (!_workers.at(0).IsActive())
        {
            _workers.at(0).Start();
        }
        if (!_workers.at(1).IsActive())
        {
            _workers.at(1).Start();
        }
        if (!_workers.at(2).IsActive())
        {
            _workers.at(2).Start();
        }

        // Reschedule jobs already inside inactive workers
        RescheduleWorkerJobsFromInActiveWorkers();
        _optimizePerformance.unlock();
    }

    void JobSystem::StartAllWorkers(bool activeWorkersOnly)
    {
        const int workerCount = activeWorkersOnly ? _activeWorkerCount.load() : _workerCount;
        for (int i = 0; i < workerCount; i++)
        {
            auto& worker = _workers.at(i);

            if (!worker.IsActive())
            {
                worker._shutdownRequested.store(false);
                worker.Start();
            }
        }
    }

    void JobSystem::RescheduleWorkerJobs(JobSystemWorker& worker)
    {
        JobSystemWorker& newWorker = _workers.at(GetRandomWorker());
        if (&worker == &newWorker)
        {
            return;
        }

        Job* job = nullptr;
        do {
            job = worker.TryTakeJob(JobPriority::Low);

            if (job == nullptr)
                break;

            newWorker.GiveFutureJob(job->GetId());
            worker.UnScheduleJob(job->GetId());
            Schedule(newWorker, job, JobPriority::High);
        } while (job != nullptr);
    }

    void JobSystem::RescheduleWorkerJobsFromInActiveWorkers()
    {
        // Reschedule jobs already inside inactive workers
        for (int i = _activeWorkerCount.load(); i < _workerCount; i++)
        {
            RescheduleWorkerJobs(_workers[i]);
        }
    }

    bool JobSystem::CanWorkerRunJob(JobSystemWorker& worker, Job*& currentJob)
    {

        if (currentJob == nullptr)
        {
            return false;
        }

        if (threadDepth > 0) {
            if (currentJob->GetEmptyStackRequired())
                return false;
        }

        if (IsProposedJobIgnoredByJobStack(currentJob->GetId()))
        {
            return false;
        }

        size_t workerIndex = 0;
        for (size_t i = 0; i < _workers.size(); i++)
        {
            const auto& currentWorker = _workers.at(i);
            if (&currentWorker == &worker)
            {
                workerIndex = i;
            }
        }

        for (size_t i = workerIndex; i < _workers.size(); i++)
        {
            auto& currentWorker = _workers.at(i);
            currentWorker._jobsRequiringIgnoringMutex.lock();
            for (const auto& jobWithIgnores : currentWorker._jobsRequiringIgnoring)
            {
                // Do not execute the proposed job if it's forbidden by other jobs currently being executed
                if (jobWithIgnores->GetIgnoreCallback()(currentJob->GetId()))
                {
                    currentWorker._jobsRequiringIgnoringMutex.unlock();
                    return false;
                }
            }
            currentWorker._jobsRequiringIgnoringMutex.unlock();
        }
        for (size_t i = 0; i < workerIndex; i++)
        {
            auto& currentWorker = _workers.at(i);
            currentWorker._jobsRequiringIgnoringMutex.lock();
            for (const auto& jobWithIgnores : currentWorker._jobsRequiringIgnoring)
            {
                // Do not execute the proposed job if it's forbidden by other jobs currently being executed
                if (jobWithIgnores->GetIgnoreCallback()(currentJob->GetId()))
                {
                    currentWorker._jobsRequiringIgnoringMutex.unlock();
                    return false;
                }
            }
            currentWorker._jobsRequiringIgnoringMutex.unlock();
        }

        return true;
    }

    void JobSystem::RunJob(JobSystemWorker& worker, Job*& currentJob)
    {
        assert(!JobInStack(currentJob->GetId()));

        jobStack.emplace_back(currentJob);

        const IgnoreJobCallback& callback = currentJob->GetIgnoreCallback();
        if (callback)
        {
            const std::scoped_lock<JbSystem::mutex> lock(worker._jobsRequiringIgnoringMutex);
            worker._jobsRequiringIgnoring.emplace(currentJob);
        }

        currentJob->Run();

        if (callback)
        {
            const std::scoped_lock<JbSystem::mutex> lock(worker._jobsRequiringIgnoringMutex);
            worker._jobsRequiringIgnoring.erase(currentJob);
        }

        for (size_t i = 0; i < jobStack.size(); i++)
        {
            if (jobStack.at(i)->GetId() == currentJob->GetId())
            {
                jobStack.erase(jobStack.begin() + i);
                break;
            }
        }

        worker.FinishJob(currentJob);
    }

    void JobSystem::RunJobInNewThread(JobSystemWorker& worker, Job*& currentJob)
    {
        const JobId jobId = currentJob->GetId();
        worker._pausedJobsMutex.lock();
        worker._pausedJobs.emplace(jobId, JobSystemWorker::PausedJob(currentJob, worker));
        worker._pausedJobsMutex.unlock();


        // Exit function when job was picked up in reasonal amount of time
        std::chrono::high_resolution_clock::time_point startTimePoint = std::chrono::high_resolution_clock::now();
        while(std::chrono::high_resolution_clock::now() - startTimePoint < std::chrono::microseconds(50))
        {
            worker._pausedJobsMutex.lock();
            if (!worker._pausedJobs.contains(jobId))
            {
                worker._pausedJobsMutex.unlock();
                return;
            }
            worker._pausedJobsMutex.unlock();
            std::this_thread::yield();
        }

        // Start a new thread to execute a job to prevent deadlock
        std::atomic<bool> startSign = false;
        JobSystemWorker* selectedWorker = &worker;
        auto emergencyWorker            = std::thread(
            [this, &startSign, selectedWorker]() mutable
            {

                while (!startSign.load())
                { /* Spinlock*/
                }
                startSign.store(false);


                allowedToLowerQueue = false;
                int currentWorkerIndex = _workerCount;

                // Continue running jobs that might also be scheduled that normal workers cannot run
                std::chrono::high_resolution_clock::time_point endpoint =
                    std::chrono::high_resolution_clock::now() + std::chrono::seconds(1);
                while (std::chrono::high_resolution_clock::now() < endpoint)
                {
                    for (size_t i = 0; i < 10; i++)
                    {
                        // Find new job, but when non are left exit
                        if (!selectedWorker->ExecutePausedJob()) {
                            // Select a new worker
                            currentWorkerIndex++;
                            if (currentWorkerIndex >= _workerCount)
                            {
                                currentWorkerIndex = 0;
                            }
                            selectedWorker = &_workers.at(currentWorkerIndex);
                            continue;
                        }

                        endpoint = std::chrono::high_resolution_clock::now() + std::chrono::seconds(1);
                    }
                }

                const std::thread::id currentThreadId = std::this_thread::get_id();
                auto removeThread                     = [this, currentThreadId ](auto thisFunction) -> void
                {
                    if (!_spawnedThreadsMutex.try_lock())
                    {
                        Job* destoryThreadJob = JobSystem::CreateJobWithParams(thisFunction, thisFunction);
                        Schedule(destoryThreadJob, JobPriority::Low);
                        return;
                    }

                    // When lock was aquired we can wait for the other thread to exit
                    if (!_spawnedThreadsExecutingIgnoredJobs.contains(currentThreadId))
                    {
                        _spawnedThreadsMutex.unlock();
                        return;
                    }

                    std::thread& threadToJoin = _spawnedThreadsExecutingIgnoredJobs.at(currentThreadId);
                    if (threadToJoin.joinable())
                    {
                        threadToJoin.join();
                    }
                    _spawnedThreadsExecutingIgnoredJobs.erase(currentThreadId);
                    _spawnedThreadsMutex.unlock();
#ifdef JobSystem_Analytics_Enabled
                    _extraSpawnedThreadsCount--;
#endif
                };

                Job* destoryThreadJob = JobSystem::CreateJobWithParams(removeThread, removeThread);

                Schedule(destoryThreadJob, JobPriority::Normal);
            });
        const std::thread::id workerThreadId = emergencyWorker.get_id();
#ifdef JobSystem_Analytics_Enabled
        _extraSpawnedThreadsCount++;
#endif
        _spawnedThreadsMutex.lock();
        _spawnedThreadsExecutingIgnoredJobs.emplace(workerThreadId, std::move(emergencyWorker));
        _spawnedThreadsMutex.unlock();

        startSign.store(true);
        while (startSign)
        { /*Wait for thread to acknowledge it has started*/
        }

    }

    int JobSystem::GetRandomWorker()
    {
        randomWorkerIndex++;
        if (randomWorkerIndex >= _activeWorkerCount.load())
        {
            randomWorkerIndex = 0;
        }
        return randomWorkerIndex;
    }

    JobId JobSystem::Schedule(JobSystemWorker& worker, Job* const& newJob, const JobPriority& priority)
    {
        // Lower job queue
        if (_preventIncomingScheduleCalls.load(std::memory_order_relaxed)) {
            MaybeHelpLowerQueue(priority);
        }

        const JobId& id = newJob->GetId();

        worker._modifyingThread.lock();
        worker._scheduledJobsMutex.lock();
        assert(worker._scheduledJobs.contains(id.ID()));

        if (priority == JobPriority::High)
        {
            worker._highPriorityTaskQueue.emplace_back(newJob);
        }

        else if (priority == JobPriority::Normal)
        {
            worker._normalPriorityTaskQueue.emplace_back(newJob);
        }

        else if (priority == JobPriority::Low)
        {
            worker._lowPriorityTaskQueue.emplace_back(newJob);
        }

        worker._modifyingThread.unlock();
        worker._scheduledJobsMutex.unlock();

        MaybeOptimize();

        return id;
    }

    void JobSystem::SafeRescheduleJob(Job* const& oldJob, JobSystemWorker& oldWorker)
    {
        const JobId& id = oldJob->GetId();

        while (true)
        {
            // Try to schedule in either one of the required threads, in case it's not possible throw error
            if (!oldWorker.IsActive())
            {
                oldWorker.Start();
            }

            assert(!oldWorker.IsJobInQueue(id));
            assert(oldWorker.IsJobScheduled(id));

            if (oldWorker.GiveJob(oldJob, JobPriority::High))
            {
                return;
            }
        }
    }

    std::vector<JobId> JobSystem::Schedule(const std::vector<int>& workerIds, const JobPriority& priority, const std::vector<Job*>& newjobs)
    {
        std::vector<JobId> jobIds;
        const size_t jobCount = newjobs.size();
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
            while (worker.ScheduledJobCount() > 0)
            {
                Job* job = worker.TryTakeJob(JobPriority::Low);
                if (job == nullptr)
                {
                    continue;
                }

                worker.UnScheduleJob(job->GetId());
                jobs.emplace_back(job);
            }
        }
        return jobs;
    }

    void JobSystem::MaybeOptimize()
    {
        if (!_enablePeriodicOptimization.load(std::memory_order_relaxed))
            return;

        optimizeInCycles--;
        if (optimizeInCycles > 0)
        {
            return;
        }
        optimizeInCycles = maxOptimizeInCycles;

        // Optimize performance once in a while
        const int remaining = _jobExecutionsTillOptimization.load();
        _jobExecutionsTillOptimization.store(_jobExecutionsTillOptimization.load() - 1);
        if (remaining < 1)
        {
            _jobExecutionsTillOptimization.store(_maxJobExecutionsBeforePerformanceOptimization);
            OptimizePerformance();
        }
    }
    void JobSystem::MaybeHelpLowerQueue(const JobPriority& priority)
    {
        if (!allowedToLowerQueue)
        {
            return;
        }

        if (!_preventIncomingScheduleCalls.load())
        {
            return;
        }

        maybeLowerWorkDepth++;
        if (maybeLowerWorkDepth % 2 == 1)
        {
            ExecuteJob(priority);
        }
        maybeLowerWorkDepth--;
    }

    JobId JobSystem::Schedule(const int& workerId, Job* const& newJob, const JobPriority& priority)
    {
        return Schedule(_workers.at(workerId), newJob, priority);
    }
} // namespace JbSystem