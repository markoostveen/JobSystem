#define CATCH_CONFIG_MAIN
#include "TestingFramework/catch.hpp"

#include "JobSystem/JobSystem.h"

#include "TestUtils.hpp"

#include <iostream>
#include <vector>
#include <memory>

using namespace JbSystem;

const JobId& StartIncremenJob(std::vector<int>* data) {
	auto jobSystem = JobSystem::GetInstance();

	auto job = JobSystem::CreateJobWithParams(
		[](auto data)
		{
			std::cout << "Executing Job" << std::endl;
			for (size_t i = 0; i < data->size(); i++)
			{
				data->emplace_back(i);
			}
		}, data);

	return jobSystem->Schedule(job, JobPriority::High);
}

bool RunSimpleJob() {
	auto jobSystem = JbSystem::JobSystem::GetInstance();

	auto data = new std::vector<int>();
	data->reserve(10);
	const JobId& jobId = StartIncremenJob(data);

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

	const JobId& jobId0 = jobSystem->Schedule(JobSystem::CreateJob(emptyJobFunction), JobPriority::High);
	const JobId& jobId1 = jobSystem->Schedule(JobSystem::CreateJob(emptyJobFunction), JobPriority::High);
	const JobId& jobId2 = jobSystem->Schedule(JobSystem::CreateJob(emptyJobFunction), JobPriority::High);

	DoStuff();
	const JobId& jobId3 = jobSystem->Schedule(JobSystem::CreateJob(emptyJobFunction), JobPriority::High, jobId2, jobId1);
	const JobId& jobId4 = jobSystem->Schedule(JobSystem::CreateJob(emptyJobFunction), JobPriority::High, jobId3, jobId0);

	DoStuff();

	//Wait for 4 seconds then check if everything is completed
	//Doing this without a single check is blocking
	jobSystem->WaitForJobCompletion(jobId4);
	return true;
}

bool RunDependencyJob() {
	auto jobSystem = JobSystem::GetInstance();

	std::vector<JobId> jobIds;
	jobIds.reserve(4);
	jobIds.emplace_back(jobSystem->Schedule(JobSystem::CreateJob([]() {}), JobPriority::High));
	jobIds.emplace_back(jobSystem->Schedule(JobSystem::CreateJob([]() {}), JobPriority::High));
	DoStuff();
	jobIds.emplace_back(jobSystem->Schedule(JobSystem::CreateJob([]() {}), JobPriority::High));
	jobIds.emplace_back(jobSystem->Schedule(JobSystem::CreateJob([]() {}), JobPriority::High));
	jobIds.emplace_back(jobSystem->Schedule(JobSystem::CreateJob([]() {}), JobPriority::Low));
	const JobId& jobId4 = jobSystem->Schedule(JobSystem::CreateJob([]() {}), JobPriority::High, jobIds);
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