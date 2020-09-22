// JobSystem.cpp : Defines the entry point for the application.
//

#include "Job.h"

#include <atomic>

using namespace JbSystem;

//JbSystem::Job::Job()
//	: _priority(JobPriority::None), _id(0), _function({})
//{
//}

const int JbSystem::JobBase::GetId() const
{
	return _id;
}

const JobPriority JbSystem::JobBase::GetPriority() const
{
	return _priority;
}

static std::atomic<int> Identifier; // Use atomic to ensure that value is only incremented once
JbSystem::JobBase::JobBase(const JobPriority priority, const Function callback)
	: _id(Identifier++), _basefunction(callback), _priority(priority)
{
}

JbSystem::JobBase::JobBase(const int id, const JobPriority priority, const Function callback)
	: _id(id), _basefunction(callback), _priority(priority) {
}