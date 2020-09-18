#define CATCH_CONFIG_MAIN
#include "TestingFramework/catch.hpp"
#include "JobSystem/JobSystem.h"

#include <iostream>
#include <vector>

bool RunSimpleParallelJobs() {
	auto jobSystem = JbSystem::JobSystem::GetInstance();

	std::vector<int> jobIds = jobSystem->Schedule(0, 10000, 100, [](int index) {});
	
	int controlJobId = jobSystem->ScheduleDependend([]() {}, jobIds);
	return jobSystem->WaitForJobCompletion(controlJobId, 1000 * 4);
}

bool RunDependendParallelJobs() {
	auto jobSystem = JbSystem::JobSystem::GetInstance();

	std::vector<int> jobIds = jobSystem->Schedule(0, 10000, 100, [](int index) {});
	std::vector<int> jobIds2 = jobSystem->ScheduleDependend(0, 10000, 100, [](int index) {}, JbSystem::JobPriority::High);
	int singleJobId = jobSystem->ScheduleDependend([]() {}, jobIds);
	jobIds2.emplace_back(singleJobId);


	std::vector<int> jobIds3 = jobSystem->ScheduleDependend(0, 10000, 100, [](int index) {}, jobIds2);

	int controlJobId = jobSystem->ScheduleDependend([]() {}, jobIds3);

	return jobSystem->WaitForJobCompletion(controlJobId, 1000 * 4);
}

TEST_CASE("Run jobs in parallel") {
	REQUIRE(RunSimpleParallelJobs());
	REQUIRE(RunDependendParallelJobs());
}