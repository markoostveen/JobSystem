// JobSystem.cpp : Defines the entry point for the application.
//

#include "Job.h"

#include <atomic>

using namespace JbSystem;

//JbSystem::Job::Job()
//	: _priority(JobPriority::None), _id(0), _function({})
//{
//}

const int JbSystem::Job::GetId() const
{
	return _id;
}

static std::atomic<int> Identifier;
const int JbSystem::Job::RequestUniqueID()
{
	return Identifier++;
}

JbSystem::Job::Job(const int id, const Function callback, const DestructorFunction destructorfunction)
	: _basefunction(callback), _destructorfunction(destructorfunction), _id(id) {
}