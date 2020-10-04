#define CATCH_CONFIG_MAIN
#include "TestingFramework/catch.hpp"

#include "JobSystem/JobSystem.h"

#include "TestUtils.hpp"

#include <iostream>
#include <vector>
#include <memory>

using namespace JbSystem;

int StartIncremenJob(std::vector<int>* data) {
	auto jobSystem = JobSystem::GetInstance();

	auto job = JobSystem::CreateJob(
		[](auto data)
		{
			std::cout << "Executing Job" << std::endl;
			for (size_t i = 0; i < data->size(); i++)
			{
				data->emplace_back(i);
			}
		}, data);

	return jobSystem->Schedule(job);
}

bool RunSimpleJob() {
	auto jobSystem = JbSystem::JobSystem::GetInstance();

	auto data = new std::vector<int>();
	data->reserve(10);
	int jobId = StartIncremenJob(data);

	DoStuff();
	DoStuff();

	jobSystem->WaitForJobCompletion(jobId);
	for (size_t i = 0; i < data->size(); i++)
	{
		//If this fails or is not valid test is considered a falliure
		if (data->at(i) != i)
			return false;
	}

	return true;
}

bool RunManyDependencyJob() {
	auto jobSystem = JobSystem::GetInstance();

	auto emptyJobFunction = []() { std::cout << " Running job " << std::endl; };

	int jobId0 = jobSystem->Schedule(JobSystem::CreateJob(emptyJobFunction));
	int jobId1 = jobSystem->Schedule(JobSystem::CreateJob(JobPriority::High, emptyJobFunction));
	int jobId2 = jobSystem->Schedule(JobSystem::CreateJob(emptyJobFunction));

	DoStuff();
	int jobId3 = jobSystem->Schedule(JobSystem::CreateJob(emptyJobFunction), jobId2, jobId1);
	int jobId4 = jobSystem->Schedule(JobSystem::CreateJob(emptyJobFunction), jobId3, jobId0);

	DoStuff();

	//Wait for 4 seconds then check if everything is completed
	//Doing this without a single check is blocking
	jobSystem->WaitForJobCompletion(jobId4);
	return true;
}

bool RunDependencyJob() {
	auto jobSystem = JobSystem::GetInstance();

	std::vector<int> jobIds;
	jobIds.reserve(4);
	jobIds.emplace_back(jobSystem->Schedule(JobSystem::CreateJob([]() {})));
	jobIds.emplace_back(jobSystem->Schedule(JobSystem::CreateJob(JobPriority::High, []() {})));
	DoStuff();
	jobIds.emplace_back(jobSystem->Schedule(JobSystem::CreateJob([]() {})));
	jobIds.emplace_back(jobSystem->Schedule(JobSystem::CreateJob([]() {})));
	jobIds.emplace_back(jobSystem->Schedule(JobSystem::CreateJob(JobPriority::Low, []() {})));
	int jobId4 = jobSystem->Schedule(JobSystem::CreateJob([]() {}), jobIds);
	DoStuff();

	//Wait for 4 seconds then check if everything is completed
	//Doing this without a single check is blocking
	jobSystem->WaitForJobCompletion(jobId4);
	return true;
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