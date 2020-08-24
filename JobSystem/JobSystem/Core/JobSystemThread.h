#pragma once
#include "InternalJob.h"

#include <thread>
#include <functional>
#include <queue>
#include <mutex>

namespace JbSystem {
	class JobSystemWorker {
		using StealFunction = std::function<InternalJobBase* ()>;
		using FinishJobFunction = std::function<void(InternalJobBase*&)>;

	public:
		JobSystemWorker(StealFunction stealJobFunction, FinishJobFunction finishJobFunction);
		JobSystemWorker(JobSystemWorker& worker);
		~JobSystemWorker();

		/// <summary>
		/// returns weather or not worker is open to execute tasks
		/// there might be a delay in thread actually exiting be carefull
		/// </summary>
		/// <returns></returns>
		const bool IsRunning();
		void WaitForShutdown();
		void Start(); //Useful when thread became lost for some reason

		InternalJobBase* TryTakeJob();
		void GiveJob(InternalJobBase*& newJob);

		//Is the read suppost to be active
		bool Active;
	private:
		void ThreadLoop();

		StealFunction _stealJobFunction;
		FinishJobFunction _finishJobFunction;

		std::mutex _queueMutex;
		std::queue<InternalJobBase*> _shortTaskQueue;
		std::queue<InternalJobBase*> _mediumTaskQueue;
		std::queue<InternalJobBase*> _longTaskQueue;

		std::mutex _isRunningMutex;
		std::condition_variable _isRunningConditionalVariable;
		std::thread _worker;
	};
}