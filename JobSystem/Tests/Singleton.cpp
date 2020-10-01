#include "JobSystem/JobSystem.h"

#include <iostream>

using namespace JbSystem;

int main() {
	auto jobsystem = JobSystem::GetInstance();

	auto jobFunction = []() {};

	jobsystem->Schedule(JobSystem::CreateJob(jobFunction));
	jobsystem->Schedule(JobSystem::CreateJob(jobFunction));
	jobsystem->Schedule(JobSystem::CreateJob(jobFunction));

	jobsystem->ExecuteJob();
	jobsystem->ExecuteJob();
	jobsystem->ExecuteJob();

	return 0;
}