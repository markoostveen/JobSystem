#include "JobSystem/JobSystem.h"

#include <vector>
#include <memory>
#include <iostream>

using namespace JbSystem;

int main()
{
	auto jobSystem = std::make_unique<JobSystem>();

	constexpr int parallelSize = 5000000;
	int iterations = 20;

	//print expected result
	std::cout << parallelSize * iterations << std::endl;

	auto collection = new std::vector<int>[parallelSize];

	auto largeJob = JobSystem::CreateParallelJob(JobPriority::High, 0, parallelSize, 100,
		[](int index, std::vector<int>* collection, int iterations) {
			//Do whatever you have too

			collection[index].reserve(iterations);
			for (int i = 0; i < iterations; i++)
			{
				collection[index].emplace_back(i);
			}
		}, collection, iterations);
	auto largeJobId = jobSystem->Schedule(largeJob);

	auto largeJob2 = JobSystem::CreateParallelJob(JobPriority::High, 0, parallelSize, 100,
		[](int index, std::vector<int>* collection, int iterations) {
			//Do whatever you have too

			collection[index].reserve(iterations);
			for (int i = 0; i < iterations; i++)
			{
				collection[index][i] += 5;
			}
		}, collection, iterations);
	auto largeJob2Id = jobSystem->Schedule(largeJob2);

	jobSystem->WaitForJobCompletion(largeJob2Id);

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