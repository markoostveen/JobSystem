#pragma once

#include "Job.h"
#include "WorkerThread.h"

#include "boost/pool/singleton_pool.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <mutex>
#include <set>
#include <thread>
#include <type_traits>
#include <unordered_set>

namespace JbSystem
{

    class JobSystem
    {

        friend class JobSystemWorker;

        typedef void (*WorkerThreadLoop)(JobSystemWorker* worker);

      public:
        JobSystem(const JobSystem&)            = delete;
        JobSystem(JobSystem&&)                 = delete;
        JobSystem& operator=(JobSystem&&)      = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        explicit JobSystem(
            unsigned int threadCount    = std::thread::hardware_concurrency() - 1,
            WorkerThreadLoop workerLoop = [](JobSystemWorker* worker) { worker->ThreadLoop(); });
        ~JobSystem();

        /// <summary>
        /// Use to modify properties of the jobSystem
        /// Start or Restart the jobsystem, with the desired amount of workers
        /// Note, currently scheduled jobs will be rescheduled, during rescheduling, priority order isn't guarenteed
        /// </summary>
        /// <param name="threadCount"></param>
        void ReConfigure(unsigned int threadCount = std::thread::hardware_concurrency() - 1);

        // Single
        template<typename... Args>
        static Job* CreateJobWithParams(const auto& function, Args...);
        static Job* CreateJob(void (*function)());

        static void DestroyNonScheduledJob(Job*& job);

        // Parallel
        template <class... Args>
        static std::vector<Job*> CreateParallelJob(
            int startIndex, int endIndex, int batchSize, const auto& function,
            Args... args);
        static std::vector<Job*> CreateParallelJob(int startIndex, int endIndex, int batchSize, void (*function)(const int&));

        /// <summary>
        /// Gives the job to one of the workers for execution
        /// </summary>
        /// <param name="newjob"></param>
        /// <returns></returns>
        JobId Schedule(Job* const& newjob, const JobPriority& priority);
        /// <summary>
        /// Gives the job to one of the workers for execution
        /// </summary>
        /// <param name="newjob"></param>
        /// <returns></returns>
        template <typename... DependencyJobId>
        JobId Schedule(Job* const& job, const JobPriority& priority, const DependencyJobId... dependencies);
        /// <summary>
        /// Gives the job to one of the workers for execution
        /// </summary>
        /// <param name="newjob"></param>
        /// <returns></returns>
        JobId Schedule(Job* const& job, const JobPriority& priority, const std::vector<JobId>& dependencies);

        /// <summary>
        /// Gives the job to one of the workers for execution
        /// </summary>
        /// <param name="newjob"></param>
        /// <returns></returns>
        std::vector<JobId> Schedule(const std::vector<Job*>& newjobs, const JobPriority& priority);
        /// <summary>
        /// Gives the job to one of the workers for execution
        /// </summary>
        /// <param name="newjob"></param>
        /// <returns></returns>
        template <typename... DependencyJobId>
        std::vector<JobId> Schedule(const std::vector<Job*>& newjobs, const JobPriority& priority, const DependencyJobId... dependencies);
        /// <summary>
        /// Gives the job to one of the workers for execution
        /// </summary>
        /// <param name="newjob"></param>
        /// <returns></returns>
        std::vector<JobId> Schedule(const std::vector<Job*>& newjobs, const JobPriority& priority, const std::vector<JobId>& dependencies);

        /// <summary>
        ///
        /// </summary>
        /// <param name="jobId"></param>
        /// <returns>weather or not the job was completed</returns>
        bool IsJobCompleted(const JobId& jobId);

        /// <summary>
        /// Will check if jobs are completed, if they are they will be removed from the given vector
        /// </summary>
        /// <param name="jobIds"></param>
        /// <returns>weather or not the job was completed</returns>
        bool AreJobsCompleted(std::vector<JobId>& jobIds);

        /// <summary>
        /// Caller will help execute jobs until the job with specified Id is completed
        /// </summary>
        /// <param name="jobId"></param>
        /// <param name="maxMicroSecondsToWait">when elapsed function will return false if job hasn't been completed yet, 0 =
        /// infinity</param> <returns>weather or not the job was completed in time</returns>
        void WaitForJobCompletion(const JobId& jobId, JobPriority maximumHelpEffort = JobPriority::Low);

        /// <summary>
        /// Caller will help execute jobs until the job with specified Id is completed
        /// </summary>
        /// <param name="jobId"></param>
        /// <param name="maxMicroSecondsToWait">when elapsed function will return false if job hasn't been completed yet, 0 =
        /// infinity</param> <returns>weather or not the job was completed in time</returns>
        bool WaitForJobCompletion(const JobId& jobId, int maxMicroSecondsToWait, JobPriority maximumHelpEffort = JobPriority::Low);

        /// <summary>
        /// Block execution until given jobs have been completed, this operation is blocking
        /// </summary>
        /// <param name="jobIds"></param>
        void WaitForJobCompletion(const std::vector<JobId>& jobIds, JobPriority maximumHelpEffort = JobPriority::Low);

        /// <summary>
        /// wait for jobs to complete, then execute function
        /// </summary>
        /// <param name="dependencies">jobs to wait for before scheduling the 'newJob'</param>
        /// <param name="callback">function to execute after jobs have been completed</param>
        /// <returns></returns>
        template <typename... Args>
        void ScheduleAfterJobCompletion(
            const std::vector<JobId>& dependencies, const JobPriority& dependencyPriority,
            const auto& function, Args... args);

        /// <summary>
        /// Wait until all jobs committed to the jobsystem have been completed
        /// </summary>
        void WaitForAllJobs();

        /// <summary>
        /// Executes a scheduled job
        /// </summary>
        void ExecuteJob();

        /// <summary>
        /// Executes a scheduled job
        /// </summary>
        void ExecuteJob(const JobPriority& maxTimeInvestment);

        /// <summary>
        /// Start all workers
        /// </summary>
        /// <param name="activeWorkersOnly">Workers proposed by jobsystem</param>
        void StartAllWorkers(bool activeWorkersOnly = true);

        int GetWorkerCount() const;
        int GetActiveWorkerCount() const;
        int GetExtraThreadsCount() const;

        // Thread Unsafe function to get a worker thread.
        JobSystemWorker& GetWorker(const int& index);
        const JobSystemWorker& GetWorker(const int& index) const;

        /// <summary>
        /// Enables or disable if states should be printed to the cout
        /// </summary>
        /// <param name="option"></param>
        void ShowStats(bool option = true);
        void TogglePeriodicWorkerOptimization(bool option = true);
        void TogglePreventAcceptingNewSchedules(bool option = true);
        void IncreaseActiveWorkerCount();
        void DecreaseActiveWorkerCount();

        bool IsUsingBuiltInOptimization() const;
        bool IsAcceptingNewJobs() const;

        std::chrono::nanoseconds CompleteAnalyticsTick();

        /// <summary>
        /// Get singleton instance
        /// </summary>
        /// <returns></returns>
        static JobSystem* GetInstance();

        void WaitForJobsAndShutdown();

        void OptimizePerformance();
        void MaybeOptimize();

        /// <summary>
        /// is the job system active or not
        /// </summary>
        std::atomic<bool> Active = false;
        WorkerThreadLoop WorkerLoop;

      private:
        /// <summary>
        /// Shutdown all worker threads
        /// </summary>
        /// <returns> vector of all remaining jobs </returns>
        std::vector<Job*> Shutdown();

        int ScheduleFutureJob(Job* const& newFutureJob);
        std::vector<JobId> BatchScheduleJob(const std::vector<Job*>& newjobs, const JobPriority& priority);
        std::vector<int> BatchScheduleFutureJob(const std::vector<Job*>& newjobs);

        static Job* TakeJobFromWorker(JobSystemWorker& worker, JobPriority maxTimeInvestment = JobPriority::Low);

        bool IsJobCompleted(const JobId& jobId, JobSystemWorker*& jobWorker);

        void RescheduleWorkerJobs(JobSystemWorker& worker);
        void RescheduleWorkerJobsFromInActiveWorkers();

        bool CanWorkerRunJob(JobSystemWorker& worker, Job*& currentJob);
        static void RunJob(JobSystemWorker& worker, Job*& currentJob);
        void RunJobInNewThread(JobSystemWorker& worker, Job*& currentJob);

        int GetRandomWorker();
        int GetWorkerId(JobSystemWorker* worker);

        /// <summary>
        /// Schedules a job in a specific worker
        /// </summary>
        /// <param name="workerId"></param>
        /// <param name="newjob"></param>
        /// <returns></returns>
        JobId Schedule(const int& workerId, Job* const& newJob, const JobPriority& priority);

        /// <summary>
        /// Schedules a job in a specific worker
        /// </summary>
        /// <param name="workerId"></param>
        /// <param name="newjob"></param>
        /// <returns></returns>
        JobId Schedule(JobSystemWorker& worker, Job* const& newJob, const JobPriority& priority);

        static void SafeRescheduleJob(Job* const& oldJob, JobSystemWorker& oldWorker);

        std::vector<JobId> Schedule(const std::vector<int>& workerIds, const JobPriority& priority, const std::vector<Job*>& newjobs);

        /// <summary>
        /// Take all scheduled jobs from all workers
        /// </summary>
        /// <returns></returns>
        std::vector<Job*> StealAllJobsFromWorkers();

        void MaybeHelpLowerQueue(const JobPriority& priority);

        std::atomic<int> _activeWorkerCount  = 0;
        const uint32_t _minimumActiveWorkers = 3;
        int _workerCount                     = 0;
        std::vector<JobSystemWorker> _workers;

        JbSystem::mutex _optimizePerformance;

        const int _maxJobExecutionsBeforePerformanceOptimization = 10;
        std::atomic<int> _jobExecutionsTillOptimization          = _maxJobExecutionsBeforePerformanceOptimization;

        std::atomic<bool> _preventIncomingScheduleCalls;

        std::atomic<bool> _showStats;
        std::atomic<bool> _enablePeriodicOptimization;

        // Deadlock prevention
        JbSystem::mutex _spawnedThreadsMutex;
        std::unordered_map<std::thread::id, std::thread> _spawnedThreadsExecutingIgnoredJobs;
#ifdef JobSystem_Analytics_Enabled
        std::atomic<uint32_t> _extraSpawnedThreadsCount;
        std::chrono::time_point<std::chrono::high_resolution_clock> _analyticsTickStartTimePoint;
#endif
    };

    template <typename... Args>
    inline Job*
    JobSystem::CreateJobWithParams(const auto& function, Args... args)
    {
        using FunctionType = std::remove_const_t<std::remove_reference_t<decltype(function)>>;

        void* location = boost::singleton_pool <
            typename JobSystemWithParametersJob<FunctionType, Args...>::Tag,
            sizeof(JobSystemWithParametersJob<FunctionType, Args...>)>::malloc();
        auto deconstructorCallback = [](JobSystemWithParametersJob<FunctionType, Args...>* const& job)
        {
            job->~JobSystemWithParametersJob();
            boost::singleton_pool<
                typename JobSystemWithParametersJob<FunctionType, Args...>::Tag,
                sizeof(JobSystemWithParametersJob<FunctionType, Args...>)>::free(
                job);
        };
        return new (location) JobSystemWithParametersJob<FunctionType, Args...>(
            typename JobSystemWithParametersJob<FunctionType, Args...>::JobSpecificFunction(function), deconstructorCallback, std::forward<Args>(args)...);
    }

    template <class... Args>
    inline std::vector<Job*> JobSystem::CreateParallelJob(
        int startIndex, int endIndex, int batchSize, const auto& function,
        Args... args)
    {
        if (batchSize < 1)
        {
            batchSize = 1;
        }

        auto parallelFunction = [](const auto& callback, const int& loopStartIndex,
                                   const int& loopEndIndex, Args... parallelArgs)
        {
            for (int i = loopStartIndex; i < loopEndIndex; i++)
            {
                callback(i, std::forward<Args>(parallelArgs)...);
            }
        };

        int jobStartIndex = 0;
        int jobEndIndex   = 0;

        // Schedule and create lambda for all job kinds
        int totalBatches     = 0;
        const int endOfRange = endIndex - startIndex;

        int CurrentBatchEnd = endOfRange;
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

            jobs.emplace_back(CreateJobWithParams(parallelFunction, function, jobStartIndex, jobEndIndex, args...));
        }

        jobStartIndex = startIndex + (totalBatches * batchSize);
        jobEndIndex   = endIndex;

        // Create last job
        jobs.emplace_back(CreateJobWithParams(parallelFunction, function, jobStartIndex, jobEndIndex, args...));

        return jobs;
    }

    template <typename... DependencyJobId>
    inline JobId JobSystem::Schedule(Job* const& job, const JobPriority& priority, const DependencyJobId... dependencies)
    {
        const std::vector<JobId> dependencyArray = {dependencies...};

        // Schedule jobs in the future, then when completed, schedule them for inside workers
        const int workerId = ScheduleFutureJob(job);

        ScheduleAfterJobCompletion(
            dependencyArray, priority,
            [](JobSystem* jobSystem, int scheduleWorkerId, Job* callbackJob, JobPriority reschedulePriority)
            { jobSystem->Schedule(scheduleWorkerId, callbackJob, reschedulePriority); },
            this, workerId, job, priority);
        return job->GetId();
    }

    template <typename... DependencyJobId>
    inline std::vector<JobId>
    JobSystem::Schedule(const std::vector<Job*>& newjobs, const JobPriority& priority, const DependencyJobId... dependencies)
    {
        const std::vector<JobId> dependencyArray = {dependencies...};

        // Schedule jobs in the future, then when completed, schedule them for inside workers
        auto workerIds = BatchScheduleFutureJob(newjobs);

        WaitForJobCompletion(
            dependencyArray,
            [](auto jobSystem, auto scheduleWorkerIds, auto callbackJobs, JobPriority schedulePriority)
            { jobSystem->Schedule(scheduleWorkerIds, callbackJobs, schedulePriority); },
            this, workerIds, newjobs, priority);

        std::vector<JobId> jobIds;
        size_t jobCount = newjobs.size();
        jobIds.reserve(jobCount);
        for (size_t i = 0; i < jobCount; i++)
        {
            jobIds.emplace_back(newjobs[i]->GetId());
        }

        return jobIds;
    }

    template <typename... Args>
    inline void JobSystem::ScheduleAfterJobCompletion(
        const std::vector<JobId>& dependencies, const JobPriority& dependencyPriority,
        const auto& function, Args... args)
    {
        assert(!dependencies.empty());

        struct DependenciesTag
        {
        };

        auto rescheduleJob = [](auto& rescheduleCallback, auto& retryCallback, Job*& callback, const JobPriority& reschedulePriority,
                                JobSystem*& jobSystem, std::vector<JobId>*& dependencies, JobSystemWorker*& suggestedWorker)
        {
            // Prerequsites not met, queueing async job to check back later
            Job* rescheduleJob = JobSystem::CreateJobWithParams(
                retryCallback, rescheduleCallback, retryCallback, callback, reschedulePriority, jobSystem, dependencies, suggestedWorker);
            rescheduleJob->SetEmptyStackRequired(true);

            jobSystem->RescheduleWorkerJobsFromInActiveWorkers();
            jobSystem->Schedule(rescheduleJob, JobPriority::Low);
        };

        auto jobScheduler = [](auto rescheduleCallback, auto retryCallback, Job* callback, JobPriority reschedulePriority,
                               JobSystem* jobSystem, std::vector<JobId>* dependencies, JobSystemWorker* suggestedJobWorker) -> void
        {
            for (size_t i = 0; i < dependencies->size(); i++)
            {
                if (!jobSystem->IsJobCompleted(dependencies->at(i), suggestedJobWorker))
                {

                    rescheduleCallback(
                        rescheduleCallback, retryCallback, callback, reschedulePriority, jobSystem, dependencies, suggestedJobWorker);
                    return;
                }
                else{

                    // Erase dependency when it is completed
                    dependencies->erase(dependencies->begin() + i);
                    i--;
                }
            }

            // When all dependencies are completed
            jobSystem->Schedule(callback, reschedulePriority);
            dependencies->~vector();
            boost::singleton_pool<DependenciesTag, sizeof(std::vector<JobId>)>::free(dependencies);
        };

        void* location        = boost::singleton_pool<DependenciesTag, sizeof(std::vector<JobId>)>::malloc();
        auto* jobDependencies = new (location) std::vector<JobId>({dependencies});

        Job* callbackJob = JobSystem::CreateJobWithParams(function, std::forward<Args>(args)...);
        callbackJob->SetIgnoreCallback(
            [jobDependencies = std::vector<JobId>({dependencies})](const JobId& proposedJobId)
            {
                for (const auto& dependencyId : jobDependencies)
                {
                    if (dependencyId == proposedJobId)
                    {
                        return true;
                    }
                }
                return false;
            });

        // run in sync, if dependencies have already completed we can immediatly schedule it
        jobScheduler(rescheduleJob, jobScheduler, callbackJob, dependencyPriority, this, jobDependencies, nullptr);
    }
} // namespace JbSystem
