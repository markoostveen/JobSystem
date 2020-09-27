#include "JobSystem/JobSystem.h"

#include <atomic>
#include <iostream>
#include <chrono>

using namespace JbSystem;
constexpr int totalIterations = 100000;
constexpr int repeatBeforeValid = 100;

int x;
void TestCall() {
	x++;
}

double CallDirect() {
	std::chrono::time_point start = std::chrono::high_resolution_clock::now();

	for (size_t i = 0; i < totalIterations; i++)
	{
		TestCall();
	}

	std::chrono::time_point end = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

double CallJobStack() {
	std::chrono::time_point start = std::chrono::high_resolution_clock::now();
	JobVoid test(JobPriority::None, TestCall);

	for (size_t i = 0; i < totalIterations; i++)
	{
		test.Run();
	}

	std::chrono::time_point end = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

double CallJobHeap() {
	std::chrono::time_point start = std::chrono::high_resolution_clock::now();
	Job* job = JobSystem::CreateJob(JobPriority::None, TestCall);

	for (size_t i = 0; i < totalIterations; i++)
	{
		job->Run();
	}

	std::chrono::time_point end = std::chrono::high_resolution_clock::now();
	job->Free();
	return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
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

	std::cout << "direct calls per " << totalIterations << " times took " << totalTimeDirect / repeatBeforeValid << "us" << std::endl;
	std::cout << "job on stack calls per " << totalIterations << " times took " << totalTimeJob / repeatBeforeValid << "us" << std::endl;
	std::cout << "job on heap calls per " << totalIterations << " times took " << totalTimeJobHeap / repeatBeforeValid << "us" << std::endl;
}

int main() {
	SimpleCallBenchmark();
	return 0;
}