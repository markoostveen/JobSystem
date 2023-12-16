#include "WorkerThread.h"

#include "JobSystem.h"

#include <chrono>

#include <iostream>

#include <string>

namespace JbSystem
{

    void JobSystemWorker::ThreadLoop()
    {
        // std::cout << "Worker has started" << std::endl;
        std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
        bool wasJobCompleted                                     = false;
        int noWork                                               = 0;
        while (true)
        {

            Job* job = nullptr;
            for (size_t i = 0; i < 3; i++)
            {
                job = TryTakeJob(JobPriority::Low);
                if (job != nullptr)
                {
                    break;
                }
            }

            if (job != nullptr)
            {
                _isBusy.store(true, std::memory_order_relaxed);
#ifdef JobSystem_Analytics_Enabled
                _timeSinceLastJob.store(std::chrono::nanoseconds(std::chrono::high_resolution_clock::now().time_since_epoch()), std::memory_order_relaxed);
#endif
                JobSystem::RunJob(*this, job);
                _isBusy.store(false, std::memory_order_relaxed);
                wasJobCompleted = true;
#ifdef JobSystem_Analytics_Enabled
                _timeSinceLastJob.store(std::chrono::nanoseconds(std::chrono::high_resolution_clock::now().time_since_epoch()), std::memory_order_relaxed);
#endif
                continue;
            }

            noWork++;

            if (noWork < 100)
            {
                continue;
            }

            // Take a possible job from a random worker
            JobSystemWorker& randomWorker = _jobsystem->_workers.at(_jobsystem->GetRandomWorker());

#ifdef JobSystem_WorkerStealToggle_Enabled
            if (_jobStealingEnabled.load(std::memory_order_relaxed)) {
                for (size_t i = 0; i < 5; i++)
                {
                    job = JobSystem::TakeJobFromWorker(randomWorker, JobPriority::Low);
                    if (job != nullptr)
                    {
                        break;
                    }
                }
            }
            else {
                job = nullptr;
            }
#else
            for (size_t i = 0; i < 5; i++)
            {
                job = JobSystem::TakeJobFromWorker(randomWorker, JobPriority::Low);
                if (job != nullptr)
                {
                    break;
                }
            }
#endif

            if (job != nullptr)
            {
                _isBusy.store(true, std::memory_order_relaxed);
                assert(randomWorker.IsJobScheduled(job->GetId()));
#ifdef JobSystem_Analytics_Enabled
                _timeSinceLastJob.store(std::chrono::nanoseconds(std::chrono::high_resolution_clock::now().time_since_epoch()), std::memory_order_relaxed);
#endif
                JobSystem::RunJob(randomWorker, job);
                _isBusy.store(false, std::memory_order_relaxed);
                wasJobCompleted = true;
            }

            if (wasJobCompleted)
            {
                noWork          = 0;
                wasJobCompleted = false;
                startTime       = std::chrono::high_resolution_clock::now();
#ifdef JobSystem_Analytics_Enabled
                _timeSinceLastJob.store(std::chrono::nanoseconds(std::chrono::high_resolution_clock::now().time_since_epoch()), std::memory_order_relaxed);
#endif
                continue;
            }

            if ((std::chrono::high_resolution_clock::now() - startTime) <= std::chrono::milliseconds(25))
            {
                noWork = 0;
                continue;
            }

            _isBusy.store(false, std::memory_order_relaxed);

            // Check if other works are active
            bool otherWorkersActive = false;
            for (int i = 0; i < _jobsystem->_activeWorkerCount.load(); i++)
            {
                JobSystemWorker& worker = _jobsystem->_workers[i];
                if (worker.IsActive() && this != &worker)
                {
                    otherWorkersActive = true;
                    continue;
                }
            }

            // Do not shutdown in case there are no other workers
            if (otherWorkersActive || !_jobsystem->Active.load())
            {
                break;
            }
        }

        // std::cout << "Worker has exited!" << std::endl;
    }

    std::chrono::nanoseconds JobSystemWorker::GetConsistentTimePoint() const
    {
#ifdef JobSystem_Analytics_Enabled
        if (Busy()) {
            return std::chrono::nanoseconds(0);
        }

        std::chrono::nanoseconds firstRead = _timeSinceLastJob.load(std::memory_order_acquire);

        return std::chrono::high_resolution_clock::now().time_since_epoch() - firstRead; // Return the read value that is consistent across reads
#else
        return std::chrono::nanoseconds(0);
#endif

    }

    size_t JobSystemWorker::GetConsistentJobQueueSize() const
    {
        // Read data from deque, check values because of possible stale since we're not locking the data and might read bad memory
        auto getSizeFromDequePossiblyStale = [](const std::deque<Job*>& deque) -> size_t {

            // Read the size 3 times, and make sure that the compiler won't combine them into a single read, to have better chances to getting accurate values without locking
            size_t firstRead = deque.size();
            std::atomic_thread_fence(std::memory_order_acquire);
            size_t secondRead = deque.size();
            std::atomic_thread_fence(std::memory_order_acquire);
            size_t thirdRead = deque.size();

            if (firstRead == secondRead || firstRead == thirdRead) {
                return firstRead;
            }
            else if (secondRead == thirdRead) {
                return secondRead;
            }
            else {
                return 0; // Value is probably invalid
            }
        };

        size_t queueSize = getSizeFromDequePossiblyStale(_highPriorityTaskQueue);
        queueSize += getSizeFromDequePossiblyStale(_normalPriorityTaskQueue);
        queueSize += getSizeFromDequePossiblyStale(_lowPriorityTaskQueue);

        // When value is very large or to small return 0 to prevent UB risks from unsafe data
        if (queueSize < 0 || queueSize > INT16_MAX)
            return 0;

        return queueSize;
    }

    uint32_t JobSystemWorker::GetCompletedJobsThisTick() const
    {
#ifdef JobSystem_Analytics_Enabled
        return _JobsFinishedThisTick.load(std::memory_order_acquire);
#else
        return 0;
#endif
    }

    void JobSystemWorker::SetJobStealingToggle(bool enabled)
    {
#ifdef JobSystem_WorkerStealToggle_Enabled
        _jobStealingEnabled.store(enabled, std::memory_order_release);
#endif
    }

    void JobSystemWorker::CompleteAnalyticsTick()
    {
        while (_JobsFinishedThisTick.exchange(0, std::memory_order_relaxed) != 0) {}
        std::atomic_thread_fence(std::memory_order_acquire);
    }

    void JobSystemWorker::KeepAliveLoop()
    {
        while (!_shutdownRequested.load())
        {
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

    bool JobSystemWorker::Busy() const
    {
        return _isBusy.load(std::memory_order_acquire);
    }

    JobSystemWorker::JobSystemWorker(JobSystem* jobsystem) :
        Active(false),
        _jobsystem(jobsystem),
        _shutdownRequested(false),
        _isRunning(false),
        _isBusy(false),
        _timeSinceLastJob(std::chrono::nanoseconds(0))
#ifdef JobSystem_WorkerStealToggle_Enabled
        ,_jobStealingEnabled(true)
#endif
    {
    }

    JobSystemWorker::JobSystemWorker(const JobSystemWorker& worker) : _jobsystem(worker._jobsystem)
    {
        Active.store(false);
    }

    JobSystemWorker::JobSystemWorker(JobSystemWorker&& other) noexcept :
        Active(false),
        _jobsystem(other._jobsystem),
        _worker(std::move(other._worker)),
        _highPriorityTaskQueue(std::move(other._highPriorityTaskQueue)),
        _normalPriorityTaskQueue(std::move(other._normalPriorityTaskQueue)),
        _lowPriorityTaskQueue(std::move(other._lowPriorityTaskQueue)),
        _scheduledJobs(std::move(other._scheduledJobs)),
        _jobsRequiringIgnoring(std::move(other._jobsRequiringIgnoring)),
        _pausedJobs(std::move(other._pausedJobs)),
        _shutdownRequested(other._shutdownRequested.load()),
        _isRunning(other._isRunning.load()),
        _isBusy(other._isBusy.load())
#ifdef JobSystem_Analytics_Enabled
        ,_timeSinceLastJob(std::chrono::nanoseconds(0))
#endif

#ifdef JobSystem_WorkerStealToggle_Enabled
        , _jobStealingEnabled(other._jobStealingEnabled.load())
#endif

    {
        assert(!other.Active); // While moving threads should not be active

        other._jobsystem = nullptr;
        if (other._worker.joinable())
        {
            other._worker.detach();
        }
    }

    JobSystemWorker::~JobSystemWorker()
    {
        if (_worker.get_id() != std::thread::id())
        {
            _shutdownRequested.store(true);
            if (_worker.joinable())
            {
                _worker.join();
            }
            else
            {
                _worker.detach();
            }
        }
    }

    bool JobSystemWorker::IsActive() const
    {
        return Active.load(std::memory_order_acquire);
    }

    void JobSystemWorker::WaitForShutdown()
    {
        const std::unique_lock ul(_isRunningMutex);
    }

    void JobSystemWorker::Start()
    {
        if (!_jobsystem->Active.load())
        {
            return;
        }

        _modifyingThread.lock();

#ifndef JBSYSTEM_KEEP_ALIVE
        if (IsActive() || _shutdownRequested.load())
        {
            _modifyingThread.unlock();
            return;
        }

        if (_worker.get_id() != std::thread::id())
        {
            if (_worker.joinable())
                _worker.join();
            else
                _worker.detach();
        }

        _shutdownRequested.store(false);
        Active.store(true);
        _worker = std::thread(
            [this](JobSystemWorker* worker)
            {
                std::unique_lock ul(_isRunningMutex);
                _isRunning.store(true);

                worker->KeepAliveLoop();
                _isRunning.store(false);
                Active.store(false);
            },
            this);
#else
        if (!_shutdownRequested.load())
        {
            Active.store(true);
            if (_worker.joinable())
            {
                _worker.join();
            }
            _worker = std::thread(
                [this](JobSystemWorker* worker)
                {
                    const std::unique_lock ul(_isRunningMutex);
                    _isRunning.store(true);

                    worker->KeepAliveLoop();
                    _isRunning.store(false);
                    Active.store(false);
                },
                this);
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
        if (!_modifyingThread.try_lock())
        {
            return nullptr;
        }

        if (maxTimeInvestment >= JobPriority::High)
        {
            if (!_highPriorityTaskQueue.empty())
            {
                Job* value = _highPriorityTaskQueue.front();
                _highPriorityTaskQueue.pop_front();
                assert(IsJobScheduled(value->GetId()));
                _modifyingThread.unlock();
                return value;
            }
        }

        if (maxTimeInvestment >= JobPriority::Normal)
        {
            if (!_normalPriorityTaskQueue.empty())
            {
                Job* value = _normalPriorityTaskQueue.front();
                _normalPriorityTaskQueue.pop_front();
                assert(IsJobScheduled(value->GetId()));
                _modifyingThread.unlock();
                return value;
            }
        }

        if (maxTimeInvestment >= JobPriority::Low)
        {
            if (!_lowPriorityTaskQueue.empty())
            {
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

    bool JobSystemWorker::IsJobInQueue(const JobId& jobId)
    {

        const int& id = jobId.ID();
        const std::scoped_lock<JbSystem::mutex> lock(_modifyingThread);
        for (const auto& highPriorityJob : _highPriorityTaskQueue)
        {
            if (highPriorityJob->GetId().ID() == id)
            {
                return true;
            }
        }

        for (const auto& normalPriorityJob : _normalPriorityTaskQueue)
        {
            if (normalPriorityJob->GetId().ID() == id)
            {
                return true;
            }
        }

        for (const auto& lowPriorityJob : _lowPriorityTaskQueue)
        {
            if (lowPriorityJob->GetId().ID() == id)
            {
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
        if (!IsActive())
        {
            return false;
        }

        _modifyingThread.lock();
        assert(_scheduledJobs.contains(newJob->GetId().ID()));

        if (priority == JobPriority::High)
        {
            _highPriorityTaskQueue.emplace_back(newJob);
        }

        else if (priority == JobPriority::Normal)
        {
            _normalPriorityTaskQueue.emplace_back(newJob);
        }

        else if (priority == JobPriority::Low)
        {
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
        for (int i = 0; i < size; i++)
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

#ifdef JobSystem_Analytics_Enabled
        // Increase counter for completed jobs in this worker thread. Please note this data might be stale because we don't want to slow down worker thread
        _JobsFinishedThisTick.store(_JobsFinishedThisTick.load(std::memory_order_relaxed) + 1, std::memory_order_release);
#endif
    }

    bool JobSystemWorker::IsJobScheduled(const JobId& jobId)
    {
        _scheduledJobsMutex.lock();
        const bool contains = _scheduledJobs.contains(jobId.ID());
        _scheduledJobsMutex.unlock();
        return contains;
    }

    JobSystemWorker::PausedJob::PausedJob(Job* affectedJob, JobSystemWorker& worker) : AffectedJob(affectedJob), Worker(worker)
    {
    }

} // namespace JbSystem