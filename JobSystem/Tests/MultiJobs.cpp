#define CATCH_CONFIG_MAIN
#include "TestingFramework/catch.hpp"
#include "JobSystem/JobSystem.h"

#include <iostream>
#include <vector>

//Test starting jobs, and then having other jobs wait on the starting jobs to complete before starting execution

bool JobsUsingDataFromEachother() {
	auto jobSystem = JbSystem::JobSystem::GetInstance();

	constexpr int totalJobSize = 50;

	std::vector<int> data[totalJobSize];
	std::vector<int> jobIds;
	jobIds.reserve(totalJobSize);
	for (size_t i = 0; i < totalJobSize; i++)
	{
		jobIds.emplace_back(jobSystem->Schedule([&data, index = i]() {
			std::cout << "Executing Job" << std::endl;
			for (int i = 0; i < 10; i++)
			{
				data[index].emplace_back(i);
			}
			}));
	}

	std::vector<int> data2[totalJobSize];
	std::vector<int> jobIds2;
	jobIds2.reserve(totalJobSize);
	for (size_t i = 0; i < totalJobSize; i++)
	{
		jobIds2.emplace_back(jobSystem->ScheduleDependend([&jobSystem, &jobIds, &data2, index = i]() {
			std::cout << "Executing Job 2" << std::endl;
			for (int i = 0; i < 10; i++)
			{
				data2[index].emplace_back(i);
			}
			}, jobIds[i]));
	}

	int controlJobId = jobSystem->ScheduleDependend([]() {}, JbSystem::JobPriority::Low, jobIds2);
	return jobSystem->WaitForJobCompletion(controlJobId, 1000 * 4);
}


TEST_CASE("Jobs interacting with one another") {
	REQUIRE(JobsUsingDataFromEachother());
}