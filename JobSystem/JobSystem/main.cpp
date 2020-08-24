#include "Core/JobSystem.h"

#include <iostream>
#include <memory>

using namespace std;
using namespace JbSystem;

int main()
{
	cout << "Hello JobSystem." << endl;
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

	std::cout << "Current remaining Jobs: " << JobSystem::GetInstance()->ActiveJobCount() << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(5));
	std::cout << "Current remaining Jobs: " << JobSystem::GetInstance()->ActiveJobCount() << std::endl;
	JobSystem::ReConfigure(1);
	std::cout << "Current remaining Jobs: " << JobSystem::GetInstance()->ActiveJobCount() << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(5));
	std::cout << "Current remaining Jobs: " << JobSystem::GetInstance()->ActiveJobCount() << std::endl;
	JobSystem::ReConfigure(8);
	std::cout << "Current remaining Jobs: " << JobSystem::GetInstance()->ActiveJobCount() << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(5));
	std::cout << "Current remaining Jobs: " << JobSystem::GetInstance()->ActiveJobCount() << std::endl;
	JobSystem::ReConfigure(31);
	std::cout << "Current remaining Jobs: " << JobSystem::GetInstance()->ActiveJobCount() << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(5));
	std::cout << "Current remaining Jobs: " << JobSystem::GetInstance()->ActiveJobCount() << std::endl;
	JobSystem::ReConfigure();

	int currentJobs = JobSystem::GetInstance()->ActiveJobCount();
	while (currentJobs != 0) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		currentJobs = JobSystem::GetInstance()->ActiveJobCount();
		std::cout << "Current remaining Jobs: " << currentJobs << std::endl;
	}

	auto exitJob = []() {
		std::cout << "All Jobs completed" << std::endl;
	};
	int finalJobId = JobSystem::Schedule(exitJob, jobIds, JobTime::Long);
	JobSystem::WaitForJobCompletion(finalJobId);

	jobIds.clear();
	jobIds.reserve(0);

	JobSystem::ReConfigure(1);

	return 0;
}