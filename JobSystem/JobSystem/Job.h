#pragma once
#include <memory>
#include <vector>

namespace JbSystem {
	class JobBase {
	public:
		const int GetId() const;
		virtual void Run() = 0;

	protected:
		JobBase();
		int _id;
		bool _running;
	};

	template<typename JobFunction>
	class Job : public JobBase {
	public:
		Job(JobFunction* function);
		void Run();

	private:
		JobFunction* _function;
	};

	template<typename JobFunction>
	inline Job<JobFunction>::Job(JobFunction* function)
	{
		_function = function;
	}

	template<typename JobFunction>
	inline void Job<JobFunction>::Run()
	{
		//Check dependencies
		_running = true;
		(*_function)();
		_running = false;
	}
}