// JobSystem.cpp : Defines the entry point for the application.
//

#include "Job.h"

#include <atomic>

using namespace JbSystem;

const int JbSystem::JobBase::GetId() const
{
	return _id;
}

static std::atomic<int> Identifier; // Use atomic to ensure that value is only incremented once
JbSystem::JobBase::JobBase()
{
	_id = Identifier++;
	_running = false;
}