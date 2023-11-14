#include "JobSystem/JobSystem.h"

#include <iostream>
#include <memory>

using namespace std;
using namespace JbSystem;

void Job1Test()
{
}

void TestJobSystem(JobSystem*& jobSystem)
{
    std::chrono::high_resolution_clock::time_point start;
    std::chrono::high_resolution_clock::time_point end;

    auto job2 = [jobSystem]()
    {
        std::vector<int> parallelJob = jobSystem->Schedule(
            0, 10000, 100, [](int index) {}, JobPriority::High);

        jobSystem->WaitForJobCompletion(parallelJob);
    };

    auto job3 = []() {};

    auto job4 = []() {};

    start = std::chrono::high_resolution_clock::now();

    std::vector<int> job1Id = jobSystem->Schedule(1000, 5, Job1Test, JobPriority::High);
    int job2Id              = jobSystem->Schedule(job2, JobPriority::Low);
    int job3Id              = jobSystem->ScheduleDependend(job3, job2Id);
    job1Id.push_back(job3Id);
    int job4Id = jobSystem->ScheduleDependend(job4, JobPriority::Low, job1Id);
    while (!jobSystem->WaitForJobCompletion(job4Id))
    {
    }

    end = std::chrono::high_resolution_clock::now();
    std::cout << "Jobsystem took " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << " us ";
}

void TestNormal()
{
    std::chrono::high_resolution_clock::time_point start;
    std::chrono::high_resolution_clock::time_point end;

    start = std::chrono::high_resolution_clock::now();

    Job1Test();

    for (size_t i = 0; i < 1000; i++)
    {
        for (size_t i = 0; i < 100; i++)
        {
        }
    }

    end = std::chrono::high_resolution_clock::now();
    std::cout << "Normal took " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << " us" << std::endl;
}

void InsertManySmallJobs(JobSystem*& jobSystem)
{
    constexpr auto testLambda = []() {};

    int job1Id = jobSystem->Schedule(testLambda, JobPriority::High);

    jobSystem->ScheduleDependend(
        0, 10000, 1000,
        [](int index)
        {
            for (size_t i = 0; i < 3000; i++)
            {
            }
        },
        job1Id);

    std::vector<int> parallelJob = jobSystem->Schedule(
        0, 10000, 1000,
        [](int index)
        {
            for (size_t i = 0; i < 5000; i++)
            {
            }
        },
        JobPriority::High);

    vector<int> jobIds = jobSystem->ScheduleDependend(
        0, 10000, 1000,
        [](int index)
        {
            for (size_t i = 0; i < 2000; i++)
            {
            }
        },
        JobPriority::Low, parallelJob);

    jobSystem->WaitForJobCompletion(jobIds);
}

void test(JobSystem* jobSystem, bool insertRandomJobs)
{
    jobSystem->ReConfigure();

    const int totalIterations = 20;
    vector<int> jobIds;
    jobIds.reserve(totalIterations);

    if (insertRandomJobs)
        InsertManySmallJobs(jobSystem);

    jobSystem->ReConfigure(3);
    for (size_t i = 0; i < totalIterations; i++)
    {
        TestJobSystem(jobSystem);
        TestNormal();
    }

    if (insertRandomJobs)
        InsertManySmallJobs(jobSystem);

    jobSystem->ReConfigure(7);
    for (size_t i = 0; i < totalIterations; i++)
    {
        TestJobSystem(jobSystem);
        TestNormal();
    }

    if (insertRandomJobs)
        InsertManySmallJobs(jobSystem);

    jobSystem->ReConfigure(31);
    for (size_t i = 0; i < totalIterations; i++)
    {
        TestJobSystem(jobSystem);
        TestNormal();
    }

    jobSystem->ReConfigure();
    for (size_t i = 0; i < totalIterations; i++)
    {
        TestJobSystem(jobSystem);
        TestNormal();
    }

    auto exitJob = []() { std::cout << "All Jobs completed" << std::endl; };

    // test if jobsystem, can handle a task with zero dependencies
    int finalJobId = jobSystem->ScheduleDependend(exitJob, JobPriority::Low, jobIds);

    jobSystem->WaitForJobCompletion(finalJobId);
    jobIds.clear();
    jobIds.reserve(0);

    if (insertRandomJobs)
        InsertManySmallJobs(jobSystem);

    jobSystem->ReConfigure(3);
    for (size_t i = 0; i < totalIterations; i++)
    {
        TestJobSystem(jobSystem);
        TestNormal();
    }
}

int main()
{
    cout << "Hello JobSystem." << endl;

    bool insertRandomJobs = false;

    JobSystem* custom = new JobSystem();
    test(custom, insertRandomJobs);
    custom->ReConfigure();
    delete custom;

    test(JobSystem::GetInstance().get(), insertRandomJobs);

    JobSystem::GetInstance()->ReConfigure();

    // while (true) {}
    std::this_thread::sleep_for(std::chrono::seconds(5));

    return 0;
}