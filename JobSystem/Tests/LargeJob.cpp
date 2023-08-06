#include "JobSystem/JobSystem.h"

#include <vector>
#include <memory>
#include <iostream>
#include <chrono>

using namespace JbSystem;

int main()
{
	auto jobSystem = JobSystem::GetInstance();


	std::this_thread::sleep_for(std::chrono::seconds(2));

	const int parallelSize = 1000000;
	const int batchSize = 40;
	int iterations = 10;

	auto collection = new int*[parallelSize];

	auto largeJob = JobSystem::CreateParallelJob(0, parallelSize, batchSize,
		[](const int& index, int** collection, int iterations) {
			
			// allocate new array in the collection
			auto& array = collection[index];
			array = new int[iterations];

		}, collection, iterations);

	auto jobIds = jobSystem->Schedule(largeJob, JobPriority::High);

	auto largeJob2 = JobSystem::CreateParallelJob(0, parallelSize, batchSize,
		[](const int& index, int** collection, int iterations) {

			// initializing the array -> this example is quite stupid but serves just as some work
			auto& array = collection[index];
			for (int i = 0; i < iterations; i++)
			{
				array[i] = std::rand();
			}
		}, collection, iterations);
	auto jobIds2 = jobSystem->Schedule(largeJob2, JobPriority::High, jobIds);

	auto largeJob3 = JobSystem::CreateParallelJob(0, parallelSize, batchSize,
		[](const int& index, int** collection, int iterations) {

			// modifying the array
			auto& array = collection[index];
			for (int i = 0; i < iterations; i++)
			{
				array[i] += std::rand();
			}
		}, collection, iterations);

	auto largeJob3Ids = jobSystem->Schedule(largeJob3, JobPriority::High, jobIds2);

	jobSystem->WaitForJobCompletion(largeJob3Ids);

	std::cout << "Cleaning up jobs have finished!" << std::endl;

	for (int i = 0; i < parallelSize; i++)
	{
		delete[] collection[i];
	}

	delete[] collection;
	return 0;
}