#include "Core/JobSystem.h"

#include <iostream>
#include <memory>

using namespace std;
using namespace JbSystem;

void test() {
	const int totalIterations = 5000;

	std::vector<int> jobIds;
	jobIds.reserve(totalIterations);

	JobSystem::ReConfigure(15);

	for (size_t i = 0; i < totalIterations; i++)
	{
		auto job1 = []() {
			int x = 0;
			for (size_t i = 0; i < 50000; i++)
			{
				x++;
			}
		};

		auto job2 = []() {
			for (size_t i = 0; i < 50; i++)
			{
				int x = 0;
				for (size_t y = 0; y < 500000; y++)
				{
					x++;
				}

				for (size_t i = 0; i < 100; i++)
				{
					JobSystem::Schedule([] {
						int x = 0;
						for (size_t i = 0; i < 1000; i++)
						{
							x++;
						}
						}, JobTime::Short);
				}
			}
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
				int x = 0;
				for (size_t y = 0; y < 5000; y++)
				{
					x++;
				}
			}
		};

		int job1Id = JobSystem::Schedule(job1, JobTime::Short);
		int job2Id = JobSystem::Schedule(job2, JobTime::Long, job1Id);
		int job3Id = JobSystem::Schedule(job3, JobTime::Medium, job1Id);
		int job4Id = JobSystem::Schedule(job4, JobTime::Long, job3Id);
		jobIds.push_back(job2Id);
	}

	std::this_thread::sleep_for(std::chrono::seconds(5));
	JobSystem::ReConfigure(1);
	std::this_thread::sleep_for(std::chrono::seconds(5));
	JobSystem::ReConfigure(8);
	std::this_thread::sleep_for(std::chrono::seconds(5));
	JobSystem::ReConfigure(31);
	std::this_thread::sleep_for(std::chrono::seconds(5));
	JobSystem::ReConfigure();

	auto exitJob = []() {
		std::cout << "All Jobs completed" << std::endl;
	};
	int finalJobId = JobSystem::Schedule(exitJob, JobTime::Long, jobIds);

	int currentJobs = JobSystem::GetInstance()->ActiveJobCount();
	while (currentJobs != 0) {
		for (size_t i = 0; i < 1000; i++)
		{
			JobSystem::ExecuteJob();
		}
		currentJobs = JobSystem::GetInstance()->ActiveJobCount();
		std::cout << "Current remaining Jobs: " << currentJobs << std::endl;
	}

	JobSystem::WaitForJobCompletion(finalJobId);
	jobIds.clear();
	jobIds.reserve(0);

	JobSystem::ReConfigure(1);
}

int main()
{
	cout << "Hello JobSystem." << endl;

	JobSystem::ReConfigure();

	/*JobSystem::Schedule([]() {
		int currentJobs = JobSystem::GetInstance()->ActiveJobCount();
		while (currentJobs != 0) {
			for (size_t i = 0; i < 1000000; i++)
			{
				JobSystem::ExecuteJob();
			}
			currentJobs = JobSystem::GetInstance()->ActiveJobCount();
			std::cout << "Current remaining Jobs: " << currentJobs << std::endl;
		}
		}, JobTime::Long);*/

	std::vector<int> parallelJob = JobSystem::Schedule(0, 10000000000, 1000, JobTime::Medium, [](int index) {
		for (size_t i = 0; i < 5000; i++)
		{
		}
		});

	test();
	return 0;
}