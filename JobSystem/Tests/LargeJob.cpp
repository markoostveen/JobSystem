#include "JobSystem/JobSystem.h"

#include <vector>
#include <memory>
#include <iostream>
#include <chrono>

using namespace JbSystem;

int main()
{
	auto jobSystem = std::make_unique<JobSystem>();


	std::this_thread::sleep_for(std::chrono::seconds(2));

	const int parallelSize = 10000000;
	const int batchSize = 100;
	int iterations = 20;

	auto collection = new int*[parallelSize];

	auto largeJob = JobSystem::CreateParallelJob(0, parallelSize, batchSize,
		[](const int& index, int** collection, int iterations) {
			//Do whatever you have too

			//std::cout << "Job 1" << std::endl;
			auto& array = collection[index];
			array = new int[iterations];
			for (int i = 0; i < iterations; i++)
			{
				array[i] = 1;
			}
		}, collection, iterations);
	auto largeJobIds = jobSystem->Schedule(largeJob, JobPriority::High);

	auto largeJob2 = JobSystem::CreateParallelJob(0, parallelSize, batchSize,
		[](const int& index, int** collection, int iterations) {
			//Do whatever you have too

			//std::cout << "Job 2" << std::endl;
			auto& array = collection[index];

			for (int i = 0; i < iterations; i++)
			{
				array[i] += 5;
			}
		}, collection, iterations);

	auto largeJob2Ids = jobSystem->Schedule(largeJob2, JobPriority::High, largeJobIds);

	jobSystem->WaitForJobCompletion(largeJob2Ids);

	for (int i = 0; i < parallelSize; i++)
	{
		delete[] collection[i];
	}

	delete[] collection;
	return 0;
}