#define CATCH_CONFIG_MAIN
#include "TestingFramework/catch.hpp"

#include "JobSystem/JobSystem.h"

#include "TestUtils.hpp"

#include <iostream>
#include <vector>
#include <memory>

int StartIncremenJob(std::vector<int>& data) {
	auto jobSystem = JbSystem::JobSystem::GetInstance();
	return jobSystem->Schedule([&data]() {
		std::cout << "Executing Job" << std::endl;
		for (size_t i = 0; i < data.size(); i++)
		{
			data.emplace_back(i);
		}
		});
}

bool RunSimpleJob() {
	auto jobSystem = JbSystem::JobSystem::GetInstance();

	std::vector<int> data;
	data.reserve(10);
	int jobId = StartIncremenJob(data);

	DoStuff();
	DoStuff();

	jobSystem->WaitForJobCompletion(jobId);
	for (size_t i = 0; i < data.size(); i++)
	{
		//If this fails or is not valid test is considered a falliure
		if (data[i] != i)
			return false;
	}

	return true;
}

bool RunManyDependencyJob() {
	auto jobSystem = JbSystem::JobSystem::GetInstance();

	int jobId0 = jobSystem->Schedule([]() {});
	int jobId1 = jobSystem->Schedule([]() {}, JbSystem::JobPriority::High);
	int jobId2 = jobSystem->Schedule([]() {});

	DoStuff();
	int jobId3 = jobSystem->ScheduleDependend([]() {}, jobId2, jobId1);
	int jobId4 = jobSystem->ScheduleDependend([]() {}, jobId3, jobId0);

	DoStuff();

	//Wait for 4 seconds then check if everything is completed
	//Doing this without a single check is blocking
	return jobSystem->WaitForJobCompletion(jobId4, 1000 * 4);
}

bool RunDependencyJob() {
	auto jobSystem = JbSystem::JobSystem::GetInstance();

	std::vector<int> jobIds;
	jobIds.reserve(4);
	jobIds.emplace_back(jobSystem->Schedule([]() {}));
	jobIds.emplace_back(jobSystem->Schedule([]() {}, JbSystem::JobPriority::High));
	DoStuff();
	jobIds.emplace_back(jobSystem->Schedule([]() {}));
	jobIds.emplace_back(jobSystem->Schedule([]() {}));
	jobIds.emplace_back(jobSystem->Schedule([]() {}, JbSystem::JobPriority::Low));
	int jobId4 = jobSystem->ScheduleDependend([]() {}, jobIds);
	DoStuff();

	//Wait for 4 seconds then check if everything is completed
	//Doing this without a single check is blocking
	return jobSystem->WaitForJobCompletion(jobId4, 1000 * 4);
}

TEST_CASE("SingleJob") {
	REQUIRE(RunSimpleJob());
}

TEST_CASE("Job with many Dependencies") {
	REQUIRE(RunManyDependencyJob());
}

TEST_CASE("Job with dependencies") {
	REQUIRE(RunDependencyJob());
}