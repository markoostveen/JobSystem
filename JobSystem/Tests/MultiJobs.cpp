#define CATCH_CONFIG_MAIN
#include "TestingFramework/catch.hpp"
#include "JobSystem/JobSystem.h"

#include <iostream>
#include <vector>

//Test starting jobs, and then having other jobs wait on the starting jobs to complete before starting execution

using namespace JbSystem;

JobId StartJobs() {
	auto jobSystem = JbSystem::JobSystem::GetInstance();

	constexpr int totalJobSize = 5000;

	std::vector<int>* data = new std::vector<int>[totalJobSize];
	std::vector<JobId> jobIds;
	jobIds.reserve(totalJobSize);
	for (int i = 0; i < totalJobSize; i++)
	{
		auto jobFunction = [](auto data, int index) {
			//std::cout << "Executing Job" << std::endl;
			for (int i = 0; i < 10; i++)
			{
				data[index].emplace_back(i);
			}
		};

		auto job = JobSystem::CreateJobWithParams(jobFunction, data, i);

		jobIds.emplace_back(jobSystem->Schedule(job, JobPriority::High));
	}

	std::vector<int>* data2 = new std::vector<int>[totalJobSize];
	std::vector<JobId> jobIds2;
	jobIds2.reserve(totalJobSize);
	for (int i = 0; i < totalJobSize; i++)
	{
		auto job = jobSystem->CreateJobWithParams([](auto data, int index) {
			//std::cout << "Executing Job 2" << std::endl;
			for (int i = 0; i < 10; i++)
			{
				data[index].emplace_back(i);
			}
			}, data2, i);

		jobIds2.emplace_back(jobSystem->Schedule(job, JobPriority::High, jobIds[i]));
	}

	JobId controlJobId = jobSystem->Schedule(jobSystem->CreateJob([]() {}), JobPriority::Low, jobIds2);
	return controlJobId;
}

bool JobsUsingDataFromEachother() {
	constexpr int size = 50;

	std::vector<JobId> jobs;
	jobs.reserve(size);
	for (int i = 0; i < size; i++)
	{
		jobs.emplace_back(StartJobs());
	}

	auto jobSystem = JbSystem::JobSystem::GetInstance();
	jobSystem->WaitForJobCompletion(jobs);

	return true;
}

TEST_CASE("Jobs interacting with one another") {
	REQUIRE(JobsUsingDataFromEachother());
}