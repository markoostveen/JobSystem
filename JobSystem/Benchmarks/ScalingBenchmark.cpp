#include "JobSystem/JobSystem.h"

#include <iostream>

using namespace JbSystem;

#pragma region Benchmark function
//Make sure that function loads the CPU enough because small tasks might have to much overhead to fully use the CPU

constexpr int ArraySize = 100;
void CopyArrayValues(const int& index) {
	size_t test = 100;
	size_t volatile& vv = test;
	vv = 0;
	for (size_t i = 0; i != test; ++i) {
		vv += 1;
	}
}
#pragma endregion

constexpr int JobCount = 1000000;
constexpr int MasterJobs = 25;

int scheduleSmallJobs(JobSystem*& jobsystem) {
	auto jobs = JobSystem::CreateParallelJob( 0, JobCount, 1000, CopyArrayValues);
	auto JobIds = jobsystem->Schedule(jobs, JobPriority::High);
	auto masterJob = JobSystem::CreateJob([]() {});
	return jobsystem->Schedule(masterJob, JobPriority::Normal, JobIds);
}

int ScheduleJobs(JobSystem*& jobsystem) {
	auto jobs = JobSystem::CreateParallelJob(0, JobCount, 10000, CopyArrayValues);
	auto JobIds = jobsystem->Schedule(jobs, JobPriority::Normal);
	auto masterJob = JobSystem::CreateJobWithParams([](JobSystem* jobsystem) { scheduleSmallJobs(jobsystem); }, jobsystem);
	return jobsystem->Schedule(masterJob, JobPriority::High, JobIds);
}

double RunBenchmark(int threadCount) {
	JobSystem* customJobSystem = new JobSystem(threadCount - 1);

	std::chrono::time_point start = std::chrono::high_resolution_clock::now();

	auto masterJobs = std::vector<int>();
	masterJobs.reserve(MasterJobs);

	JbSystem::mutex emplaceMutex;


	auto scheduleJobs = JobSystem::CreateParallelJob(0, MasterJobs, 1, [](const int& index, std::vector<int>* masterJobs, JbSystem::mutex* mutex, JobSystem* JobSystem){
		
		int masterJob = ScheduleJobs(JobSystem);
		mutex->lock();
		masterJobs->emplace_back(masterJob);
		mutex->unlock();
	}, &masterJobs, &emplaceMutex, customJobSystem);

	auto scheduleJobIds = customJobSystem->Schedule(scheduleJobs, JobPriority::High);
	customJobSystem->WaitForJobCompletion(scheduleJobIds);
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