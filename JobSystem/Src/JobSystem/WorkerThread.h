#pragma once
#include "Job.h"

#include "AtomicMutex.h"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>

namespace JbSystem
{
    class JobSystem;

    class JobSystemWorker
    {
        friend class JobSystem;

      public:
        JobSystemWorker() = delete;
        JobSystemWorker(JobSystemWorker&&) noexcept;
        JobSystemWorker& operator=(const JobSystemWorker&) = delete;
        JobSystemWorker& operator=(JobSystemWorker&&)      = delete;
        explicit JobSystemWorker(JobSystem* jobsystem);
        JobSystemWorker(const JobSystemWorker& worker);
        ~JobSystemWorker();

        /// <summary>
        /// returns weather or not worker is open to execute tasks
        /// there might be a delay in thread actually exiting be carefull
        /// </summary>
        /// <returns></returns>
        bool IsActive() const;
        void WaitForShutdown();
        void Start(); // Useful when thread became lost for some reason
        int WorkerId();
        std::thread::native_handle_type WorkerNativeThreadHandle() const;

        bool ExecutePausedJob();
        bool ExecutePausedJob(JobSystemWorker& executingWorker);
        Job* TryTakeJob(const JobPriority& maxTimeInvestment);
        void UnScheduleJob(const JobId& previouslyScheduledJob);
        void ScheduleJob(const JobId& jobId);

        bool IsJobInQueue(const JobId& jobId);

        size_t ScheduledJobCount();

        /// <summary>
        /// Give a job to the worker thread
        /// NOTE* This will take ownership over the job
        /// </summary>
        /// <param name="newJob"></param>
        /// <param name="priority"></param>
        bool GiveJob(Job* const& newJob, const JobPriority& priority);
        void GiveFutureJob(const JobId& jobId);
        void GiveFutureJobs(const std::vector<Job*>& newjobs, int startIndex, int size);

        /// <summary>
        /// Finishes job and cleans up after
        /// </summary>
        /// <param name="job"></param>
        void FinishJob(Job*& job);

        bool IsJobScheduled(const JobId& jobId);

        void ThreadLoop();

        /// <summary>
        /// <Requires JobSystem_Analytics_Enabled>
        /// Get the time passed since the last job was executed
        /// </summary>
        /// <returns></returns>
        std::chrono::nanoseconds GetConsistentTimePoint() const;
        size_t GetConsistentJobQueueSize() const;
        uint32_t GetCompletedJobsThisTick() const;

        void SetJobStealingToggle(bool enabled);

        void CompleteAnalyticsTick();

        void RequestShutdown();

        // Is the read suppost to be active
        std::atomic<bool> Active;

        bool Busy() const;

      private:
        struct PausedJob
        {
            PausedJob(Job* affectedJob, JobSystemWorker& worker);

            Job* AffectedJob;
            JobSystemWorker& Worker;
        };

        void KeepAliveLoop();

        JobSystem* _jobsystem;
        std::thread _worker;
        std::deque<Job*> _highPriorityTaskQueue;
        std::deque<Job*> _normalPriorityTaskQueue;
        std::deque<Job*> _lowPriorityTaskQueue;

        std::unordered_set<int> _scheduledJobs;
        std::unordered_set<Job*> _jobsRequiringIgnoring;  // DeadLock prevention
        std::unordered_map<JobId, PausedJob> _pausedJobs; // DeadLock prevention

        std::atomic<bool> _shutdownRequested;
        std::atomic<bool> _isRunning;
        std::atomic<bool> _isBusy;

        JbSystem::mutex _modifyingThread;
        JbSystem::mutex _scheduledJobsMutex;
        JbSystem::mutex _isRunningMutex;
        JbSystem::mutex _jobsRequiringIgnoringMutex; // DeadLock prevention
        JbSystem::mutex _pausedJobsMutex;            // DeadLock prevention

#ifdef JobSystem_WorkerStealToggle_Enabled
        std::atomic<bool> _jobStealingEnabled;
#endif

#ifdef JobSystem_Analytics_Enabled
        std::atomic<std::chrono::nanoseconds> _timeSinceLastJob;
        std::atomic<uint32_t> _JobsFinishedThisTick;
#endif
    };
} // namespace JbSystem