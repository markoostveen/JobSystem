#include "JobSystem/JobSystem.h"

#include <atomic>
#include <iostream>
#include <chrono>

using namespace JbSystem;
constexpr int totalIterations = 10000;
constexpr int repeatBeforeValid = 1000;

inline void TestCall() {
}

double CallDirect() {
	std::chrono::time_point start = std::chrono::high_resolution_clock::now();

	for (size_t i = 0; i < totalIterations; i++)
	{
		TestCall();
	}

	std::chrono::time_point end = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}

double CallJobStack() {
	std::chrono::time_point start = std::chrono::high_resolution_clock::now();

	for (size_t i = 0; i < totalIterations; i++)
	{
		JobVoid job(TestCall);
		job.Run();
	}

	std::chrono::time_point end = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}

double CallJobHeap() {
	std::chrono::time_point start = std::chrono::high_resolution_clock::now();

	for (size_t i = 0; i < totalIterations; i++)
	{
		const Job* job = JobSystem::CreateJob(TestCall);
		job->Run();
		JobSystem::DestroyNonScheduledJob(job);
	}

	std::chrono::time_point end = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}

void SimpleCallBenchmark() {
	double totalTimeDirect = 0;
	double totalTimeJob = 0;
	double totalTimeJobHeap = 0;

	for (size_t i = 0; i < repeatBeforeValid; i++)
	{
		totalTimeDirect += CallDirect();
		totalTimeJob += CallJobStack();
		totalTimeJobHeap += CallJobHeap();
	}

	double directAverageTime = totalTimeDirect / repeatBeforeValid;
	double jobAverageTime = totalTimeJob / repeatBeforeValid;
	double heapAverageTime = totalTimeJobHeap / repeatBeforeValid;

	std::cout << "direct calls per " << totalIterations << " times took " << directAverageTime / 1000 << "us" << std::endl;
	std::cout << "job on stack calls per " << totalIterations << " times took " << jobAverageTime / 1000 << "us" << std::endl;
	std::cout << "job on heap calls per " << totalIterations << " times took " << heapAverageTime / 1000 << "us" << std::endl;

	std::cout << "Average over " << repeatBeforeValid << " runs" << std::endl;

	std::cout << std::endl;
	std::cout << "direct calls vs Stack average difference per call " << (jobAverageTime - directAverageTime) / totalIterations << "ns" << std::endl;
	std::cout << "direct calls vs Heap average difference per call " << (heapAverageTime - directAverageTime) / totalIterations << "ns" << std::endl;
	std::cout << "=======" << std::endl;
}

int main() {
	for (size_t i = 0; i < 10; i++)
	{
		std::cout << "Run #" << i << std::endl;
		SimpleCallBenchmark();
	}
	return 0;
}