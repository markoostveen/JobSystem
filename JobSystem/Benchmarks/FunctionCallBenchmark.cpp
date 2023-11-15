#include "JobSystem/JobSystem.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>

using namespace JbSystem;
constexpr int totalIterations   = 1000;
constexpr int repeatBeforeValid = 100;

void TestCall()
{
    volatile int x = 0;
    for (size_t i = 0; i < 1; i++)
    {
        x += i;
    }
}

double CallDirect()
{
    std::chrono::high_resolution_clock::time_point time(std::chrono::high_resolution_clock::duration(0));

    for (size_t i = 0; i < totalIterations; i++)
    {
        std::chrono::time_point start = std::chrono::high_resolution_clock::now();
        TestCall();
        std::chrono::time_point intermediateEnd = std::chrono::high_resolution_clock::now();
        time += intermediateEnd - start;
    }

    return std::chrono::nanoseconds(time.time_since_epoch()).count();
}

double CallJobStack()
{
    std::chrono::high_resolution_clock::time_point time(std::chrono::high_resolution_clock::duration(0));

    std::chrono::high_resolution_clock::time_point intermediateEnd;
    for (size_t i = 0; i < totalIterations; i++)
    {

        auto function = [](std::chrono::high_resolution_clock::time_point* timepoint) {
                TestCall();
                *timepoint = std::chrono::high_resolution_clock::now();
            };
        using functionType = decltype(function);
        std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
        auto job = JobWithParameters<functionType, std::chrono::high_resolution_clock::time_point*>(JobWithParameters<functionType, std::chrono::high_resolution_clock::time_point*>::JobSpecificFunction(function), &intermediateEnd);
        job.Run();
        time += intermediateEnd - start;

    }

    return std::chrono::nanoseconds(time.time_since_epoch()).count();
}

double CallJobHeap()
{

    std::chrono::high_resolution_clock::time_point time(std::chrono::high_resolution_clock::duration(0));
    std::chrono::high_resolution_clock::time_point intermediateEnd;
    for (size_t i = 0; i < totalIterations; i++)
    {
        auto function = [](std::chrono::high_resolution_clock::time_point* timepoint) {
            TestCall();
            *timepoint = std::chrono::high_resolution_clock::now();
            };

        std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
        Job* job = JobSystem::CreateJobWithParams(function, &intermediateEnd);
        job->Run();
        JobSystem::DestroyNonScheduledJob(job);
        time += intermediateEnd - start;
    }

    return std::chrono::nanoseconds(time.time_since_epoch()).count();
}

double CallJobHeapWorker()
{


    JobSystem* jobsystem = JobSystem::GetInstance();
    std::vector<std::chrono::high_resolution_clock::time_point> startTimes;
    std::vector<std::chrono::high_resolution_clock::time_point> endTimes;
    std::vector<JobId> jobs;
    jobs.resize(totalIterations, JobId(-1));
    startTimes.resize(totalIterations);
    endTimes.resize(totalIterations);

    for (size_t i = 0; i < totalIterations; i++) // No not multithread instead schedule on random worker
    {
        auto function = [](std::chrono::high_resolution_clock::time_point* timepoint) {
            TestCall();
            *timepoint = std::chrono::high_resolution_clock::now();
            };
        startTimes.at(i) = std::chrono::high_resolution_clock::now();
        Job* job             = JobSystem::CreateJobWithParams(function, &endTimes.at(i));
        jobs.at(i) = jobsystem->Schedule(job, JobPriority::High);
    }

    jobsystem->WaitForJobCompletion(jobs, JobPriority::High);

    std::chrono::high_resolution_clock::time_point time(std::chrono::high_resolution_clock::duration(0));
    for (size_t i = 0; i < totalIterations; i++)
    {
        time += endTimes.at(i) - startTimes.at(i);
    }

    return std::chrono::nanoseconds(time.time_since_epoch()).count();
}

double CallMultiJobHeapWorker()
{

    int batchSize = 10;

    std::vector<std::chrono::high_resolution_clock::time_point> endTimes;
    endTimes.resize(totalIterations);

    auto function = [](std::chrono::high_resolution_clock::time_point* timepoint) {
        TestCall();
        *timepoint = std::chrono::high_resolution_clock::now();
        };

    std::chrono::time_point start = std::chrono::high_resolution_clock::now();
    auto job = JobSystem::CreateParallelJob(0, totalIterations, batchSize, [&endTimes, function](const int& index) { function(&endTimes.at(index)); });

    JobSystem* jobsystem = JobSystem::GetInstance();

    auto id              = jobsystem->Schedule(job, JobPriority::High);
    jobsystem->WaitForJobCompletion(id, JobPriority::High);

    std::chrono::high_resolution_clock::time_point time(std::chrono::high_resolution_clock::duration(0));

    for (size_t i = 0; i < totalIterations; i++)
    {
        time += endTimes.at(i) - start;
    }

    return std::chrono::nanoseconds(time.time_since_epoch()).count();
}

int runIndex = 0;
JbSystem::mutex printMutex;

void SimpleCallBenchmark()
{
    auto totalTimeDirect             = std::make_shared<double>();
    auto totalTimeJob                = std::make_shared<double>();
    auto totalTimeJobHeap            = std::make_shared<double>();
    auto totalTimeJobHeapWorker      = std::make_shared<double>();
    auto totalTimeJobHeapWorkerMulti = std::make_shared<double>();

    double time = 0;
    for (size_t i = 0; i < repeatBeforeValid; i++)
    {
        time = CallDirect();
        *totalTimeDirect += time;
    }
    for (size_t i = 0; i < repeatBeforeValid; i++)
    {
        time = CallJobStack();
        *totalTimeJob += time;
    }

    for (size_t i = 0; i < repeatBeforeValid; i++)
    {
        time = CallJobHeap();
        *totalTimeJobHeap += time;
    }

    for (size_t i = 0; i < repeatBeforeValid; i++)
    {
        time = CallJobHeapWorker();
        *totalTimeJobHeapWorker += time;
    }

    for (size_t i = 0; i < repeatBeforeValid; i++)
    {
        time = CallMultiJobHeapWorker();
        *totalTimeJobHeapWorkerMulti += time;
    }

    double directAverageTime      = *totalTimeDirect / repeatBeforeValid;
    double jobAverageTime         = *totalTimeJob / repeatBeforeValid;
    double heapAverageTime        = *totalTimeJobHeap / repeatBeforeValid;
    double workerAverageTime      = *totalTimeJobHeapWorker / repeatBeforeValid;
    double multiWorkerAverageTime = *totalTimeJobHeapWorkerMulti / repeatBeforeValid;

    std::stringstream ss;
    ss << "\n Run #" << runIndex << "\n";
    runIndex++;

    ss << "direct calls per " << totalIterations << " times took " << directAverageTime / totalIterations << "ns on average\n"
             ;
    ss << "job on stack calls per " << totalIterations << " times took " << jobAverageTime / totalIterations << "ns  on average\n"
             ;
    ss << "job on heap calls per " << totalIterations << " times took " << heapAverageTime / totalIterations << "ns  on average\n"
             ;
    ss << "job on Worker calls per " << totalIterations << " times took " << workerAverageTime / totalIterations << "ns  on average\n"
             ;
    ss << "job on multiple Worker calls per " << totalIterations << " times took " << multiWorkerAverageTime / totalIterations
              << "ns  on average\n";

    ss << "Average over " << repeatBeforeValid << " runs\n";

    ss << "\n";
    ss << "Stack average vs direct calls difference per call " << (jobAverageTime - directAverageTime) / totalIterations << "ns "
              << jobAverageTime / directAverageTime << " times slower\n";
    ss << "Heap average vs Stack average difference per call " << (heapAverageTime - jobAverageTime) / totalIterations << "ns "
              << heapAverageTime / jobAverageTime << " times slower\n";
    ss << "Worker average vs Heap average difference per call " << (workerAverageTime - heapAverageTime) / totalIterations << "ns "
              << workerAverageTime / heapAverageTime << " times slower\n";
    ss << "Parallel job Worker average vs Heap average difference per call "
              << (multiWorkerAverageTime - heapAverageTime) / totalIterations << "ns " << multiWorkerAverageTime / heapAverageTime
              << " times slower\n";
    ss << "Typical usage of JobSystem calling heap allocated jobs is "
              << workerAverageTime / directAverageTime
              << " times slower than calling functions directly\n";
    ss << "=======\n";
    printMutex.lock();
    std::cout << ss.str();
    printMutex.unlock();
}

void JobSystemCallBenchmark()
{
    std::atomic<double> totalTimeDirect             = 0;
    std::atomic<double> totalTimeJob                = 0;
    std::atomic<double> totalTimeJobHeap            = 0;
    std::atomic<double> totalTimeJobHeapWorker      = 0;
    std::atomic<double> totalTimeJobHeapWorkerMulti = 0;

    auto job = JobSystem::CreateParallelJob(
        0, 32, 1,
        [&totalTimeDirect, &totalTimeJob, &totalTimeJobHeap, &totalTimeJobHeapWorker, &totalTimeJobHeapWorkerMulti](const int& index)
        {
            auto jobSystem = JobSystem::GetInstance();
            auto directCallJob = JobSystem::CreateParallelJob(0, repeatBeforeValid, 1, [&totalTimeDirect](const int& index) {
                totalTimeDirect += CallDirect();
                });

            auto stackJob = JobSystem::CreateParallelJob(0, repeatBeforeValid, 1, [&totalTimeJob](const int& index) {
                totalTimeJob += CallJobStack();
                });

            auto heapJob = JobSystem::CreateParallelJob(0, repeatBeforeValid, 1, [&totalTimeJobHeap](const int& index) {
                totalTimeJobHeap += CallJobHeap();
                });

            auto workerJob = JobSystem::CreateParallelJob(0, repeatBeforeValid, 1, [&totalTimeJobHeapWorker](const int& index) {
                totalTimeJobHeapWorker += CallJobHeapWorker();
                });

            auto multiWorkerJob = JobSystem::CreateParallelJob(0, repeatBeforeValid, 1, [&totalTimeJobHeapWorkerMulti](const int& index) {
                totalTimeJobHeapWorkerMulti += CallMultiJobHeapWorker();
                });

            jobSystem->WaitForJobCompletion(jobSystem->Schedule(directCallJob, JobPriority::Normal));
            jobSystem->WaitForJobCompletion(jobSystem->Schedule(stackJob, JobPriority::Normal));
            jobSystem->WaitForJobCompletion(jobSystem->Schedule(heapJob, JobPriority::Normal));
            jobSystem->WaitForJobCompletion(jobSystem->Schedule(workerJob, JobPriority::Normal));
            jobSystem->WaitForJobCompletion(jobSystem->Schedule(multiWorkerJob, JobPriority::Normal));

        });
    auto jobSystem = JobSystem::GetInstance();
    auto jobId     = jobSystem->Schedule(job, JobPriority::High);
    jobSystem->WaitForJobCompletion(jobId);

    double directAverageTime      = totalTimeDirect / repeatBeforeValid;
    double jobAverageTime         = totalTimeJob / repeatBeforeValid;
    double heapAverageTime        = totalTimeJobHeap / repeatBeforeValid;
    double workerAverageTime      = totalTimeJobHeapWorker / repeatBeforeValid;
    double multiWorkerAverageTime = totalTimeJobHeapWorkerMulti / repeatBeforeValid;

    std::stringstream ss;
    ss << "\n Run #" << runIndex << "\n";
    runIndex++;

    ss << "direct calls per " << totalIterations << " times took " << directAverageTime / totalIterations << "ns on average\n"
             ;
    ss << "job on stack calls per " << totalIterations << " times took " << jobAverageTime / totalIterations << "ns  on average\n"
             ;
    ss << "job on heap calls per " << totalIterations << " times took " << heapAverageTime / totalIterations << "ns  on average\n"
             ;
    ss << "job on Worker calls per " << totalIterations << " times took " << workerAverageTime / totalIterations << "ns  on average\n"
             ;
    ss << "job on multiple Worker calls per " << totalIterations << " times took " << multiWorkerAverageTime / totalIterations
              << "ns  on average\n";

    ss << "Average over " << repeatBeforeValid << " runs\n";

    ss << "\n";
    ss << "Stack average vs direct calls difference per call " << (jobAverageTime - directAverageTime) / totalIterations << "ns "
              << jobAverageTime / directAverageTime << " times slower\n";
    ss << "Heap average vs Stack average difference per call " << (heapAverageTime - jobAverageTime) / totalIterations << "ns "
              << heapAverageTime / jobAverageTime << " times slower\n";
    ss << "Worker average vs Heap average difference per call " << (workerAverageTime - heapAverageTime) / totalIterations << "ns "
              << workerAverageTime / heapAverageTime << " times slower\n";
    ss << "Parallel job Worker average vs Heap average difference per call "
              << (multiWorkerAverageTime - heapAverageTime) / totalIterations << "ns " << multiWorkerAverageTime / heapAverageTime
              << " times slower\n";
    ss << "Typical usage of JobSystem calling heap allocated jobs is "
              << workerAverageTime / directAverageTime
              << " times slower than calling functions directly\n";
    ss << "=======\n";
    printMutex.lock();
    std::cout << ss.str();
    printMutex.unlock();
}

void Benchmark()
{

    auto jobSystem = JobSystem::GetInstance();

    std::cout << "Started normal Run\n";
    SimpleCallBenchmark();

    std::cout << "==============================================\n";
    std::cout << "==============================================\n";
    std::cout << "==============================================\n";
    std::cout << "==============================================\n";
    std::cout << "==============================================\n";
    std::cout << "==============================================\n";
    std::cout << "==============================================\n";
    std::cout << "==============================================\n";
    std::cout << "==============================================\n";
    std::cout << "==============================================\n";

    std::cout << "\rJobSystem under load Run Started! Please wait for results\n\n";
    JobSystemCallBenchmark();

}

int main()
{
    Benchmark();
    return 0;
}