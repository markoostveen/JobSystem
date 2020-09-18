#include "JobSystem/JobSystem.h"

#include <iostream>

int main() {
	auto jobsystem = JbSystem::JobSystem::GetInstance();

	jobsystem->Schedule([]() {});
	jobsystem->Schedule([]() {});
	jobsystem->Schedule([]() {});

	jobsystem->ExecuteJob();
	jobsystem->ExecuteJob();
	jobsystem->ExecuteJob();

	return 0;
}