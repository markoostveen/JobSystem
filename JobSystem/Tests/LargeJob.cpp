#include "JobSystem/JobSystem.h"

#include <vector>
#include <memory>
#include <iostream>

using namespace JbSystem;

int main()
{
	auto jobSystem = std::make_unique<JobSystem>();

	constexpr int parallelSize = 1000000;
	constexpr int batchSize = 100;
	int iterations = 20;

	//print expected result
	std::cout << parallelSize * iterations << std::endl;

	auto collection = new std::vector<int>[parallelSize];

	auto largeJob = JobSystem::CreateParallelJob(JobPriority::High, 0, parallelSize, batchSize,
		[](const int& index, std::vector<int>* collection, int iterations) {
			//Do whatever you have too

			collection[index].reserve(iterations);
			for (size_t i = 0; i < iterations; i++)
			{
				collection[index].emplace_back(1);
			}
		}, collection, iterations);
	auto largeJobIds = jobSystem->Schedule(largeJob);

	auto largeJob2 = JobSystem::CreateParallelJob(JobPriority::High, 0, parallelSize, batchSize,
		[](const int& index, std::vector<int>* collection, int iterations) {
			//Do whatever you have too

			auto container = collection[index];

			for (int i = 0; i < iterations; i++)
			{
				container[i] += 5;
			}
		}, collection, iterations);

	auto largeJob2Ids = jobSystem->Schedule(largeJob2, largeJobIds);

	jobSystem->WaitForJobCompletion(largeJob2Ids);

	//print result
	int totalSize = 0;
	for (size_t i = 0; i < parallelSize; i++)
	{
		totalSize += collection[i].size();
	}

	std::cout << totalSize << std::endl;

	delete[] collection;
	return 0;
}