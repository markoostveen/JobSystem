#include "JobSystem/JobSystem.h"

#include <iostream>
#include <vector>

int main() {
	std::cout << "Single job test" << std::endl;
	JbSystem::JobSystem* custom = new JbSystem::JobSystem(3);

	std::vector<int> data;
	data.reserve(10);
	int jobId = custom->Schedule([&data]() {
		std::cout << "Executing Job" << std::endl;
		for (int i = 0; i < 10; i++)
		{
			std::cout << "emplacing item" << std::endl;
			data.emplace_back(i);
		}
		});

	std::cout << "Doing stuff during execution" << std::endl;

	std::this_thread::sleep_for(std::chrono::nanoseconds(100));

	std::cout << "Doing stuff during execution" << std::endl;

	custom->WaitForJobCompletion(jobId);
	for (size_t i = 0; i < data.size(); i++)
	{
		std::cout << i << std::endl;
	}
	delete custom;
	std::cout << "Single job test Completed" << std::endl;
	return 0;
}