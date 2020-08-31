#include "Core/JobSystem.h"

#include <iostream>
#include <memory>

using namespace std;
using namespace JbSystem;

void TestJobSystem(JobSystem*& jobSystem) {
	std::chrono::high_resolution_clock::time_point start;
	std::chrono::high_resolution_clock::time_point end;

	start = std::chrono::high_resolution_clock::now();

	auto job1 = []() {
		int x = 0;
		for (size_t i = 0; i < 500; i++)
		{
			x++;
		}
	};

	auto job2 = [jobSystem]() {
		std::vector<int> parallelJob = jobSystem->Schedule(0, 1000, 100, JobPriority::High, [](int index) {
			for (size_t i = 0; i < 100000; i++)
			{
			}
			});

		constexpr auto waitJob = []() { /*std::cout << "All parallelJobs finished ";*/ };
		jobSystem->WaitForJobCompletion(parallelJob, waitJob);
	};

	auto job3 = []() {
		for (size_t i = 0; i < 50; i++)
		{
			int x = 0;
			for (size_t y = 0; y < 5000; y++)
			{
				x++;
			}
		}
	};

	auto job4 = []() {
		for (size_t i = 0; i < 500; i++)
		{
		}
	};

	std::vector<int> job1Id = jobSystem->Schedule(1000, 5, JobPriority::High, job1);
	int job2Id = jobSystem->Schedule(job2, JobPriority::Low);
	int job3Id = jobSystem->Schedule(job3, JobPriority::Normal, job2Id);
	job1Id.push_back(job3Id);
	int job4Id = jobSystem->Schedule(job4, JobPriority::Low, job1Id);
	while (!jobSystem->WaitForJobCompletion(job4Id)) {}

	end = std::chrono::high_resolution_clock::now();
	std::cout << "Jobsystem took " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms ";
}

void TestNormal() {
	std::chrono::high_resolution_clock::time_point start;
	std::chrono::high_resolution_clock::time_point end;

	start = std::chrono::high_resolution_clock::now();

	for (size_t i = 0; i < 1000; i++)
	{
		int x = 0;
		for (size_t i = 0; i < 500; i++)
		{
			x++;
		}
	}

	for (int i = 0; i < 1000; i++)
	{
		for (size_t i = 0; i < 100000; i++)
		{
		}
	}

	for (size_t i = 0; i < 50; i++)
	{
		int x = 0;
		for (size_t y = 0; y < 5000; y++)
		{
			x++;
		}
	}

	for (size_t i = 0; i < 500; i++)
	{
	}

	end = std::chrono::high_resolution_clock::now();
	std::cout << "Normal took " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms" << std::endl;
}

void InsertManySmallJobs(JobSystem*& jobSystem) {
	constexpr auto testLambda = []() {};

	int job1Id = jobSystem->Schedule(testLambda, JobPriority::High);

	jobSystem->Schedule(0, 1000000, 1000, JobPriority::Normal, [](int index) {
		for (size_t i = 0; i < 3000; i++)
		{
		}
		}, job1Id);

	std::vector<int> parallelJob = jobSystem->Schedule(0, 1000000, 1000, JobPriority::High, [](int index) {
		for (size_t i = 0; i < 5000; i++)
		{
		}
		});

	vector<int> jobIds = jobSystem->Schedule(0, 1000000, 1000, JobPriority::Low, [](int index) {
		for (size_t i = 0; i < 2000; i++)
		{
		}
		}, parallelJob);

	jobSystem->WaitForJobCompletion(jobIds);
}

void test(JobSystem* jobSystem, bool insertRandomJobs) {
	jobSystem->ReConfigure();

	const int totalIterations = 20;
	vector<int> jobIds;
	jobIds.reserve(totalIterations);
	std::chrono::high_resolution_clock::time_point start;
	std::chrono::high_resolution_clock::time_point end;

	if (insertRandomJobs)
		InsertManySmallJobs(jobSystem);

	jobSystem->ReConfigure(2);
	for (size_t i = 0; i < totalIterations; i++)
	{
		TestJobSystem(jobSystem);
		TestNormal();
	}

	if (insertRandomJobs)
		InsertManySmallJobs(jobSystem);

	jobSystem->ReConfigure(8);
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

	auto exitJob = []() {
		std::cout << "All Jobs completed" << std::endl;
	};

	//test if jobsystem, can handle a task with zero dependencies
	int finalJobId = jobSystem->Schedule(exitJob, JobPriority::Low, jobIds);

	jobSystem->WaitForJobCompletion(finalJobId);
	jobIds.clear();
	jobIds.reserve(0);

	if (insertRandomJobs)
		InsertManySmallJobs(jobSystem);

	jobSystem->ReConfigure(2);
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

	test(JobSystem::GetInstance(), insertRandomJobs);

	std::this_thread::sleep_for(std::chrono::seconds(5));

	return 0;
}