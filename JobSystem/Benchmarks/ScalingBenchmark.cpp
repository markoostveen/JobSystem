#include "JobSystem/JobSystem.h"

#include <iostream>

using namespace JbSystem;

#pragma region Benchmark function
//Make sure that function loads the CPU enough because small tasks might have to much overhead to fully use the CPU

void TestFunction(int& i) {
	i++;
}

constexpr int ArraySize = 100;
void CopyArrayValues(const int& index, void(*function)(int&)) {
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

	int value = index;

	function(value);
	function(value);
	function(value);
	function(value);
	function(value);
}
#pragma endregion

constexpr int JobCount = 1000000;
constexpr int MasterJobs = 25;

int scheduleSmallJobs(JobSystem*& jobsystem) {
	auto jobs = JobSystem::CreateParallelJob(JobPriority::High, 0, JobCount, 1000, CopyArrayValues, TestFunction);
	auto JobIds = jobsystem->Schedule(jobs);
	auto masterJob = JobSystem::CreateJob([]() {});
	return jobsystem->Schedule(masterJob, JobIds);
}

int ScheduleJobs(JobSystem*& jobsystem) {
	auto jobs = JobSystem::CreateParallelJob(0, JobCount, 10000, CopyArrayValues, TestFunction);
	auto JobIds = jobsystem->Schedule(jobs);
	auto masterJob = JobSystem::CreateJob([](JobSystem* jobsystem) { scheduleSmallJobs(jobsystem); }, jobsystem);
	return jobsystem->Schedule(masterJob, JobIds);
}

double RunBenchmark(int threadCount) {
	JobSystem* customJobSystem = new JobSystem(threadCount - 1);

	std::chrono::time_point start = std::chrono::high_resolution_clock::now();

	auto masterJobs = std::vector<int>();
	masterJobs.reserve(MasterJobs);

	for (size_t i = 0; i < MasterJobs; i++)
	{
		masterJobs.emplace_back(ScheduleJobs(customJobSystem));
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
	for (size_t i = 2; i < 20; i++)
	{
		threadCount = (i * 2) - 1;
		time = 0;
		for (size_t i = 0; i < repeatBenchmarkTimes; i++)
		{
			time += RunBenchmark(threadCount);
		}

		std::cout << threadCount << " worker threads took " << time / repeatBenchmarkTimes << "ms" << std::endl;
	}

	for (size_t i = 20; i > 1; i--)
	{
		threadCount = (i * 2) - 1;
		time = 0;
		for (size_t i = 0; i < repeatBenchmarkTimes; i++)
		{
			time += RunBenchmark(threadCount);
		}

		std::cout << threadCount << " worker threads took " << time / repeatBenchmarkTimes << "ms" << std::endl;
	}
	return 0;
}