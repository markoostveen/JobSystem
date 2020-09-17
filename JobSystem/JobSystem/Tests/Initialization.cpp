#include "JobSystem/JobSystem.h"

#include <iostream>

int main() {
	std::cout << "Initialization test" << std::endl;
	JbSystem::JobSystem* custom = new JbSystem::JobSystem();
	delete custom;
	std::cout << "Initialization test Completed" << std::endl;
	return 0;
}