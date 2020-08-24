#pragma once
#include <memory>
#include <vector>
#include <functional>

namespace JbSystem {
	enum class JobTime {
		/// <summary>
		/// For your smallest jobs
		/// </summary>
		Short = 0,
		/// <summary>
		/// For jobs either depended on short jobs, or taking a little bit longer
		/// </summary>
		Medium = 1,
		/// <summary>
		/// For lengthy jobs, like loading files from places
		/// </summary>
		Long = 2
	};

	class InternalJobBase {
	public:
		const int GetId() const;
		const JobTime GetTimeInvestment() const;
		virtual ~InternalJobBase() = default;
		virtual void Run() = 0;

	protected:
		InternalJobBase() = delete;
		InternalJobBase(JobTime length);
		int _id;
		JobTime _timeInvestment;
	};

	template<typename JobFunction>
	class InternalJob : public InternalJobBase {
	public:
		InternalJob(JobFunction function, JobTime length) : InternalJobBase(length) {
			_function = [function]() { function(); };
		}
		InternalJob(const InternalJob&) = delete;
		virtual ~InternalJob() = default;

		void Run();

	private:
		std::function<void()> _function;
	};

	template<typename JobFunction>
	inline void InternalJob<JobFunction>::Run()
	{
		//Check dependencies
		_function();
	}
}