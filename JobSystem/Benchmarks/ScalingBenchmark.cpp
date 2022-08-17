#include "JobSystem/JobSystem.h"

#include <iostream>
#include <memory>

using namespace JbSystem;

#pragma region Benchmark function
//Make sure that function loads the CPU enough because small tasks might have to much overhead to fully use the CPU

void CopyArrayValues(const int& index) {

	std::vector<int> test;
	test.reserve(1500);
	for (size_t i = 0; i < 1500; i++)
	{
		int number = index;
		if (number == 0)
			number++;

		test.emplace_back((number + 1) * 12);
		test.at(i) += number / test.at(i) * index;
	}
}
#pragma endregion

constexpr int JobCount = 10000;
constexpr int MasterJobs = 25;

JobId scheduleSmallJobs() {
	auto jobs = JobSystem::CreateParallelJob( 0, JobCount, 100, CopyArrayValues);
	auto JobIds = JobSystem::GetInstance()->Schedule(jobs, JobPriority::High);
	auto masterJob = JobSystem::CreateJob([]() {});
	return JobSystem::GetInstance()->Schedule(masterJob, JobPriority::Normal, JobIds);
}

JobId ScheduleJobs() {
	auto jobs = JobSystem::CreateParallelJob(0, JobCount, 100, CopyArrayValues);
	auto JobIds = JobSystem::GetInstance()->Schedule(jobs, JobPriority::Normal);
	auto masterJob = JobSystem::CreateJob([]() { scheduleSmallJobs(); });
	return JobSystem::GetInstance()->Schedule(masterJob, JobPriority::High, JobIds);
}

long long RunBenchmark() {

	std::chrono::time_point start = std::chrono::high_resolution_clock::now();

	auto masterJobs = std::make_shared<std::vector<JobId>>();
	masterJobs->reserve(MasterJobs);

	auto emplaceMutex = std::make_shared<JbSystem::mutex>();


	auto scheduleJobs = JobSystem::CreateParallelJob(0, MasterJobs, 1, [](const int& index, auto masterJobs, auto mutex){
		
		const JobId& masterJob = ScheduleJobs();
		mutex->lock();
		masterJobs->emplace_back(masterJob);
		mutex->unlock();
	}, masterJobs, emplaceMutex);

	auto scheduleJobIds = JobSystem::GetInstance()->Schedule(scheduleJobs, JobPriority::High);
	JobSystem::GetInstance()->WaitForJobCompletion(scheduleJobIds);
	JobSystem::GetInstance()->WaitForJobCompletion(*masterJobs);

	std::chrono::time_point end = std::chrono::high_resolution_clock::now();

	return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

constexpr int testRange = 35;
constexpr int startRange = 20;
constexpr int repeatBenchmarkTimes = 20;
int main() {
	int threadCount;
	long long time = 0;
	for (int i = startRange; i < testRange; i++)
	{
		threadCount = (i * 2) - 1;
		JobSystem::GetInstance()->ReConfigure(threadCount - 1);
		time = 0;
		for (int i = 0; i < repeatBenchmarkTimes; i++)
		{
			time += RunBenchmark();
		}

		std::cout << threadCount << " worker threads took " << time / repeatBenchmarkTimes << "us" << std::endl;
	}

	for (int i = testRange; i >= startRange; i--)
	{
		threadCount = (i * 2) - 1;
		JobSystem::GetInstance()->ReConfigure(threadCount - 1);
		time = 0;
		for (int i = 0; i < repeatBenchmarkTimes; i++)
		{
			time += RunBenchmark();
		}

		std::cout << threadCount << " worker threads took " << time / repeatBenchmarkTimes << "us" << std::endl;
	}
	return 0;
}