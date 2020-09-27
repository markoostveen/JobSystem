#include "JobSystem/Jobsystem.h"

#include <iostream>

using namespace JbSystem;

void TestFunction() {
}

constexpr int ArraySize = 1000;
void CopyArrayValues(int index, void(*function)()) {
	int a[ArraySize];
	int b[ArraySize];
	for (size_t i = 0; i < ArraySize; i++)
	{
		a[i] = i * 21;
	}
	for (size_t i = 0; i < ArraySize; i++)
	{
		b[i] = a[i];
	}

	function();
}

constexpr int JobCount = 100000;
constexpr int MasterJobs = 10;
double RunBenchmark(int threadCount) {
	JobSystem* customJobSystem = new JobSystem(threadCount);

	std::chrono::time_point start = std::chrono::high_resolution_clock::now();

	auto masterJobs = std::vector<int>();
	masterJobs.reserve(MasterJobs);

	for (size_t i = 0; i < MasterJobs; i++)
	{
		auto jobs = JobSystem::CreateParallelJob(0, JobCount, 1000, CopyArrayValues, TestFunction);
		auto JobIds = customJobSystem->Schedule(jobs);
		auto masterJob = JobSystem::CreateJob([]() {});
		auto masterJobId = customJobSystem->Schedule(masterJob, JobIds);
		masterJobs.emplace_back(masterJobId);
	}

	customJobSystem->WaitForJobCompletion(masterJobs);

	std::chrono::time_point end = std::chrono::high_resolution_clock::now();

	delete customJobSystem;
	return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

constexpr int repeatBenchmarkTimes = 20;
int main() {
	int threadCount;
	double time = 0;
	for (size_t i = 1; i < 20; i++)
	{
		threadCount = i * 2;
		time = 0;
		for (size_t i = 0; i < repeatBenchmarkTimes; i++)
		{
			time += RunBenchmark(threadCount);
		}

		std::cout << threadCount << " took " << time / repeatBenchmarkTimes << "ms" << std::endl;
	}

	for (size_t i = 20; i > 1; i--)
	{
		threadCount = i * 2;
		time = 0;
		for (size_t i = 0; i < repeatBenchmarkTimes; i++)
		{
			time += RunBenchmark(threadCount);
		}

		std::cout << threadCount << " took " << time / repeatBenchmarkTimes << "ms" << std::endl;
	}
	return 0;
}