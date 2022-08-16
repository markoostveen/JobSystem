// JobSystem.cpp : Defines the entry point for the application.
//

#include "Job.h"

#include <atomic>

using namespace JbSystem;

JobId JbSystem::Job::GetId() const
{
	return _id;
}

static std::atomic<int> Identifier;
const JobId JbSystem::Job::RequestUniqueID()
{
	return JobId{ Identifier++ };
}

JbSystem::Job::Job(const JobId& id, const Function& callback, const DestructorFunction& destructorfunction)
	: _basefunction(callback), _destructorfunction(destructorfunction), _id(id) {
}

JbSystem::JobId::JobId(const int& Id)
	: _id(Id)
{
}

const int& JbSystem::JobId::ID() const
{
	return _id;
}
