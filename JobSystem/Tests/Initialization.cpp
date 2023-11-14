#include "JobSystem/JobSystem.h"

#include <iostream>

// Make a custom jobsystem, and try to delete it.

int main()
{
    JbSystem::JobSystem* custom = new JbSystem::JobSystem(std::thread::hardware_concurrency() - 1);
    delete custom;
    return 0;
}