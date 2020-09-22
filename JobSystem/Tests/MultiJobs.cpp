#define CATCH_CONFIG_MAIN
#include "TestingFramework/catch.hpp"
#include "JobSystem/JobSystem.h"

#include <iostream>
#include <vector>

//Test starting jobs, and then having other jobs wait on the starting jobs to complete before starting execution

using namespace JbSystem;

bool JobsUsingDataFromEachother() {
	auto jobSystem = JbSystem::JobSystem::GetInstance();

	constexpr int totalJobSize = 50;

	std::vector<int>* data = new std::vector<int>[totalJobSize];
	std::vector<int> jobIds;
	jobIds.reserve(totalJobSize);
	for (int i = 0; i < totalJobSize; i++)
	{
		auto jobFunction = [](auto data, int index) {
			std::cout << "Executing Job" << std::endl;
			for (int i = 0; i < 10; i++)
			{
				data[index].emplace_back(i);
			}
		};

		auto job = JobSystem::CreateJob(JobPriority::High, jobFunction, data, i);

		jobIds.emplace_back(jobSystem->Schedule(job));
	}

	std::vector<int>* data2 = new std::vector<int>[totalJobSize];
	std::vector<int> jobIds2;
	jobIds2.reserve(totalJobSize);
	for (int i = 0; i < totalJobSize; i++)
	{
		auto job = jobSystem->CreateJob(JobPriority::High, [](auto data, int index) {
			std::cout << "Executing Job 2" << std::endl;
			for (int i = 0; i < 10; i++)
			{
				data[index].emplace_back(i);
			}
			}, data2, i);

		jobIds2.emplace_back(jobSystem->Schedule(job, jobIds[i]));
	}

	int controlJobId = jobSystem->Schedule(jobSystem->CreateJob(JbSystem::JobPriority::Low, []() {}), jobIds2);
	return jobSystem->WaitForJobCompletion(controlJobId);
}

TEST_CASE("Jobs interacting with one another") {
	REQUIRE(JobsUsingDataFromEachother());
}