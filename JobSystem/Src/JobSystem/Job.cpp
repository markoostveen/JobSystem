// JobSystem.cpp : Defines the entry point for the application.
//

#include "Job.h"

#include <atomic>

using namespace JbSystem;

const int JbSystem::Job::GetId() const
{
	return _id;
}

const JobPriority JbSystem::Job::GetTimeInvestment() const
{
	return _timeInvestment;
}

void JbSystem::Job::Run() const
{
	_function();
}

static std::atomic<int> Identifier; // Use atomic to ensure that value is only incremented once

JbSystem::Job::Job(JobPriority timeInvestment)
	: _id(Identifier++), _timeInvestment(timeInvestment)
{}