#include "JobSystem.h"

#include <iostream>

using namespace std;

int main()
{
	cout << "Hello JobSystem." << endl;
	const int iterationSize = 100;
	int** arrays = new int* [iterationSize];

	std::vector<const JbSystem::JobBase*> allWorkJobs;
	allWorkJobs.reserve(iterationSize);
	const int size = 500;
	for (int j = 0; j < iterationSize; j++)
	{
		arrays[j] = new int[size];

		auto job1 = [myarray = arrays[j], size]() {
			for (int i = 0; i < size; i++)
			{
				myarray[i] += 5;
			}

			std::cout << "Job 1 completed" << std::endl;
		};

		auto job2 = [myarray = arrays[j], size]() {
			for (int i = 0; i < size; i++)
			{
				myarray[i] -= 3 - i;
			}

			std::cout << "Job 2 completed" << std::endl;
		};

		auto newJob1 = JbSystem::JobSystem::Schedule(job1);

		auto newJob2 = JbSystem::JobSystem::Schedule(job2, newJob1);
		allWorkJobs.push_back(newJob2);
	}

	auto deleteJob = [&]() {
		for (int j = 0; j < iterationSize; j++) {
			delete[] arrays[j];
		}
		delete[] arrays;
		std::cout << "All tasks have finished";
	};

	auto motherjob = JbSystem::JobSystem::Schedule(deleteJob, allWorkJobs);
	while (true) {
	}

	return 0;
}