#include "JobSystem/JobSystem.h"

#include <atomic>
#include <iostream>
#include <chrono>
#include <memory>

using namespace JbSystem;
constexpr int totalIterations = 1000;
constexpr int repeatBeforeValid = 10;

void TestCall() {
	//volatile int x = 0;
	//for (size_t i = 0; i < 1000; i++)
	//{
	//	x += i;
	//}
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
		Job* job = JobSystem::CreateJob(TestCall);
		job->Run();
		JobSystem::DestroyNonScheduledJob(job);
	}

	std::chrono::time_point end = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}

double CallJobHeapWorker() {
	std::chrono::time_point start = std::chrono::high_resolution_clock::now();

	for (size_t i = 0; i < totalIterations; i++) // No not multithread instead schedule on random worker
	{
		Job* job = JobSystem::CreateJob(TestCall);
		JobSystem* jobsystem = JobSystem::GetInstance();
		JobId id = jobsystem->Schedule(job, JobPriority::High);
		jobsystem->WaitForJobCompletion(id, JobPriority::High);
	}

	std::chrono::time_point end = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}

double CallMultiJobHeapWorker() {
	std::chrono::time_point start = std::chrono::high_resolution_clock::now();

	auto job = JobSystem::CreateParallelJob(0, totalIterations, 10, [](const int& index) {
			TestCall();
		});

	JobSystem* jobsystem = JobSystem::GetInstance();
	auto id = jobsystem->Schedule(job, JobPriority::High);
	jobsystem->WaitForJobCompletion(id, JobPriority::High);

	std::chrono::time_point end = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}

int runIndex = 0;
JbSystem::mutex printMutex;

void SimpleCallBenchmark() {
	auto totalTimeDirect = std::make_shared<double>();
	auto totalTimeJob = std::make_shared<double>();
	auto totalTimeJobHeap = std::make_shared<double>();
	auto totalTimeJobHeapWorker = std::make_shared<double>();
	auto totalTimeJobHeapWorkerMulti = std::make_shared<double>();

	double time = 0;
	for (size_t i = 0; i < repeatBeforeValid; i++)
	{
			time = CallDirect();
			*totalTimeDirect += time;
	}
	for (size_t i = 0; i < repeatBeforeValid; i++)
	{
		time = CallJobStack();
		*totalTimeJob += time;
	}

	for (size_t i = 0; i < repeatBeforeValid; i++)
	{
		time = CallJobHeap();
		*totalTimeJobHeap += time;
	}

	for (size_t i = 0; i < repeatBeforeValid; i++)
	{
		time = CallJobHeapWorker();
		*totalTimeJobHeapWorker += time;
	}

	for (size_t i = 0; i < repeatBeforeValid; i++)
	{
		time = CallMultiJobHeapWorker();
		*totalTimeJobHeapWorkerMulti += time;
	}

	double directAverageTime = *totalTimeDirect / repeatBeforeValid;
	double jobAverageTime = *totalTimeJob / repeatBeforeValid;
	double heapAverageTime = *totalTimeJobHeap / repeatBeforeValid;
	double workerAverageTime = *totalTimeJobHeapWorker / repeatBeforeValid;
	double multiWorkerAverageTime = *totalTimeJobHeapWorkerMulti / repeatBeforeValid;

	printMutex.lock();
	std::cout << "\n Run #" << runIndex << std::endl;
	runIndex++;

	std::cout << "direct calls per " << totalIterations << " times took " << directAverageTime / totalIterations << "ns on average" << std::endl;
	std::cout << "job on stack calls per " << totalIterations << " times took " << jobAverageTime / totalIterations << "ns  on average" << std::endl;
	std::cout << "job on heap calls per " << totalIterations << " times took " << heapAverageTime / totalIterations << "ns  on average" << std::endl;
	std::cout << "job on Worker calls per " << totalIterations << " times took " << workerAverageTime / totalIterations << "ns  on average" << std::endl;
	std::cout << "job on multiple Worker calls per " << totalIterations << " times took " << multiWorkerAverageTime / totalIterations << "ns  on average" << std::endl;



	std::cout << "Average over " << repeatBeforeValid << " runs" << std::endl;

	std::cout << std::endl;
	std::cout << "direct calls vs Stack average difference per call " << (jobAverageTime - directAverageTime) / totalIterations << "ns " << jobAverageTime / directAverageTime << " times slower" << std::endl;
	std::cout << "direct calls vs Heap average difference per call " << (heapAverageTime - directAverageTime) / totalIterations << "ns " << heapAverageTime / directAverageTime << " times slower" << std::endl;
	std::cout << "direct calls vs Worker average difference per call " << (workerAverageTime - directAverageTime) / totalIterations << "ns " << workerAverageTime / directAverageTime << " times slower" << std::endl;
	std::cout << "direct calls vs multiple Worker average difference per call " << (multiWorkerAverageTime - directAverageTime) / totalIterations << "ns " << multiWorkerAverageTime / directAverageTime << " times slower" << std::endl;
	std::cout << "=======" << std::endl;
	printMutex.unlock();
}

void JobSystemCallBenchmark() {
	auto totalTimeDirect = std::make_shared<double>();
	auto totalTimeJob = std::make_shared<double>();
	auto totalTimeJobHeap = std::make_shared<double>();
	auto totalTimeJobHeapWorker = std::make_shared<double>();
	auto totalTimeJobHeapWorkerMulti = std::make_shared<double>();
	auto mutex = std::make_shared<JbSystem::mutex>();


	auto job = JobSystem::CreateParallelJob(0, 32, 1, [](const int& index, auto mutex, auto totalTimeDirect, auto totalTimeJob, auto totalTimeJobHeap, auto totalTimeJobHeapWorker, auto totalTimeJobHeapWorkerMulti) {

		double time = 0;
		for (size_t i = 0; i < repeatBeforeValid; i++)
		{
			time = CallDirect();
			mutex->lock();
			*totalTimeDirect += time;
			mutex->unlock();
		}
		for (size_t i = 0; i < repeatBeforeValid; i++)
		{
			time = CallJobStack();
			mutex->lock();
			*totalTimeJob += time;
			mutex->unlock();
		}

		for (size_t i = 0; i < repeatBeforeValid; i++)
		{
			time = CallJobHeap();
			mutex->lock();
			*totalTimeJobHeap += time;
			mutex->unlock();
		}

		for (size_t i = 0; i < repeatBeforeValid; i++)
		{
			time = CallJobHeapWorker();
			mutex->lock();
			*totalTimeJobHeapWorker += time;
			mutex->unlock();
		}

		for (size_t i = 0; i < repeatBeforeValid; i++)
		{
			time = CallMultiJobHeapWorker();
			mutex->lock();
			*totalTimeJobHeapWorkerMulti += time;
			mutex->unlock();
		}

		printMutex.lock();
		std::cout << index;
		printMutex.unlock();

		}, mutex, totalTimeDirect, totalTimeJob, totalTimeJobHeap, totalTimeJobHeapWorker, totalTimeJobHeapWorkerMulti);
	auto jobSystem = JobSystem::GetInstance();
	auto jobId = jobSystem->Schedule(job, JobPriority::Low);
	jobSystem->WaitForJobCompletion(jobId);

	double directAverageTime = *totalTimeDirect / repeatBeforeValid;
	double jobAverageTime = *totalTimeJob / repeatBeforeValid;
	double heapAverageTime = *totalTimeJobHeap / repeatBeforeValid;
	double workerAverageTime = *totalTimeJobHeapWorker / repeatBeforeValid;
	double multiWorkerAverageTime = *totalTimeJobHeapWorkerMulti / repeatBeforeValid;

	printMutex.lock();
	std::cout << "\n Run #" << runIndex << std::endl;
	runIndex++;

	std::cout << "direct calls per " << totalIterations << " times took " << directAverageTime / totalIterations << "ns on average" << std::endl;
	std::cout << "job on stack calls per " << totalIterations << " times took " << jobAverageTime / totalIterations << "ns  on average" << std::endl;
	std::cout << "job on heap calls per " << totalIterations << " times took " << heapAverageTime / totalIterations << "ns  on average" << std::endl;
	std::cout << "job on Worker calls per " << totalIterations << " times took " << workerAverageTime / totalIterations << "ns  on average" << std::endl;
	std::cout << "job on multiple Worker calls per " << totalIterations << " times took " << multiWorkerAverageTime / totalIterations << "ns  on average" << std::endl;



	std::cout << "Average over " << repeatBeforeValid << " runs" << std::endl;

	std::cout << std::endl;
	std::cout << "direct calls vs Stack average difference per call " << (jobAverageTime - directAverageTime) / totalIterations << "ns " << jobAverageTime / directAverageTime << " times slower" << std::endl;
	std::cout << "direct calls vs Heap average difference per call " << (heapAverageTime - directAverageTime) / totalIterations << "ns " << heapAverageTime / directAverageTime << " times slower" << std::endl;
	std::cout << "direct calls vs Worker average difference per call " << (workerAverageTime - directAverageTime) / totalIterations << "ns " << workerAverageTime / directAverageTime << " times slower" << std::endl;
	std::cout << "direct calls vs multiple Worker average difference per call " << (multiWorkerAverageTime - directAverageTime) / totalIterations << "ns " << multiWorkerAverageTime / directAverageTime << " times slower" << std::endl;
	std::cout << "=======" << std::endl;
	printMutex.unlock();
}

void Benchmark() {
	std::cout << "Started normal Runs\n";
	for (size_t i = 0; i < 10; i++)
	{
		SimpleCallBenchmark();
	}

	auto job = JobSystem::CreateParallelJob(0, 10, 1, [](const int& testIndex) {
			JobSystemCallBenchmark();
		});

	auto jobSystem = JobSystem::GetInstance();
	//jobSystem->ReConfigure(5);
	std::cout << "\rJobSystem Runs Started! Please wait for results\n";
	auto id = jobSystem->Schedule(job, JobPriority::Low);
	jobSystem->WaitForJobCompletion(id);

	std::cout << "Started post Runs\n";
	for (size_t i = 0; i < 10; i++)
	{
		SimpleCallBenchmark();
	}
}

int main() {

	for (size_t i = 0; i < 10; i++)
	{
		Benchmark();
	}

	return 0;
}