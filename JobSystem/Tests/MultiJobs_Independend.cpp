#include "JobSystem/JobSystem.h"

#include <iostream>
#include <vector>

int main() {
	JbSystem::JobSystem* custom = new JbSystem::JobSystem(3);

	constexpr int totalJobSize = 50;

	std::vector<int> data[totalJobSize];
	std::vector<int> jobIds;
	jobIds.reserve(totalJobSize);
	for (size_t i = 0; i < totalJobSize; i++)
	{
		jobIds.emplace_back(custom->Schedule([&data, index = i]() {
			std::cout << "Executing Job" << std::endl;
			for (int i = 0; i < 10; i++)
			{
				std::cout << "emplacing item" << std::endl;
				data[index].emplace_back(i);
			}
			}));
	}

	std::cout << "Doing stuff during execution" << std::endl;

	std::this_thread::sleep_for(std::chrono::nanoseconds(100));

	std::cout << "Doing stuff during execution" << std::endl;

	custom->WaitForJobCompletion(jobIds);
	delete custom;
	return 0;
}