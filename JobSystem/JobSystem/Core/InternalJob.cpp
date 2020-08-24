// JobSystem.cpp : Defines the entry point for the application.
//

#include "InternalJob.h"

#include <atomic>

using namespace JbSystem;

const int JbSystem::InternalJobBase::GetId() const
{
	return _id;
}

const JobTime JbSystem::InternalJobBase::GetTimeInvestment() const
{
	return _timeInvestment;
}

static std::atomic<int> Identifier; // Use atomic to ensure that value is only incremented once

JbSystem::InternalJobBase::InternalJobBase(JobTime timeInvestment)
{
	_id = Identifier++;
	_timeInvestment = timeInvestment;
}