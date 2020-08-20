#include "JobSystemThread.h"

#include <iostream>

using namespace JbSystem;

void WorkerThreadLoop(JobSystemWorker* worker) {
	std::cout << "Thread started" << std::endl;

	while (true) {
		JobBase* job = JobSystem::GetWork();
		if (job == nullptr)
			continue;

		worker->CurrentJobId = job->GetId();

		job->Run();

		JobSystem::CompleteJob(job);
		worker->CurrentJobId = 0;
	}
	worker->CurrentJobId = 0;
}

JobSystemWorker::JobSystemWorker()
{
	CurrentJobId = 0;
	start();
}

const bool JobSystemWorker::IsRunning()
{
	return _worker.joinable();
}

void JobSystemWorker::start()
{
	if (CurrentJobId != 0)
		std::cout << "Thread restarted, but exited previously because of an error" << std::endl;
	_worker = std::thread(WorkerThreadLoop, this);
}