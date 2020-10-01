#define CATCH_CONFIG_MAIN
#include "TestingFramework/catch.hpp"
#include "JobSystem/JobSystem.h"

#include <iostream>
#include <vector>

using namespace JbSystem;

bool RunSimpleParallelJobs() {
	auto jobSystem = JobSystem::GetInstance();

	auto job = JobSystem::CreateParallelJob(JobPriority::High, 0, 10000, 100, [](const int& index) {});
	auto jobIds = jobSystem->Schedule(job);

	jobSystem->WaitForJobCompletion(jobIds);
	return true;
}

bool RunDependendParallelJobs() {
	auto jobSystem = JbSystem::JobSystem::GetInstance();

	auto job1 = JobSystem::CreateParallelJob(JobPriority::High, 0, 10000, 100, [](const int& index) {});
	auto jobIds = jobSystem->Schedule(job1);

	auto job2 = JobSystem::CreateParallelJob(0, 10000, 100, [](const int& index) {});
	auto jobIds2 = jobSystem->Schedule(job2);

	auto singleJob = JobSystem::CreateJob([]() {});
	int singleJobId = jobSystem->Schedule(singleJob, jobIds);
	jobIds2.emplace_back(singleJobId);

	auto job3 = JobSystem::CreateParallelJob(0, 10000, 100, [](const int& index) {});
	auto jobIds3 = jobSystem->Schedule(job3, jobIds2);

	auto job4 = JobSystem::CreateJob(JobPriority::Low, []() {});
	auto controlJobId = jobSystem->Schedule(job4, jobIds3);

	jobSystem->WaitForJobCompletion(controlJobId);
	return true;
}

TEST_CASE("Run jobs in parallel") {
	REQUIRE(RunSimpleParallelJobs());
	REQUIRE(RunDependendParallelJobs());
}