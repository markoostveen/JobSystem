#pragma once
#include <memory>
#include <vector>
#include <functional>
#include <type_traits>

namespace JbSystem {
	template<typename T>
	concept JobFunction = std::is_convertible<T, std::function<void()>>::value;

	template<typename T>
	concept ParrallelJobFunction = std::is_convertible<T, std::function<void(int)>>::value;

	enum class JobPriority {
		/// <summary>
		/// For your smallest jobs
		/// </summary>
		High = 0,
		/// <summary>
		/// For jobs either depended on short jobs, or taking a little bit longer
		/// </summary>
		Normal = 1,
		/// <summary>
		/// For lengthy jobs, like loading files from places
		/// </summary>
		Low = 2
	};

	class InternalJobBase {
	public:
		const int GetId() const;
		const JobPriority GetTimeInvestment() const;
		virtual ~InternalJobBase() = default;
		virtual void Run() = 0;

	protected:
		InternalJobBase() = delete;
		InternalJobBase(JobPriority length);
		int _id;
		JobPriority _timeInvestment;
	};

	template<JobFunction T>
	class InternalJob : public InternalJobBase {
	public:
		InternalJob(T function, JobPriority length) : InternalJobBase(length) {
			_function = function;
		}
		InternalJob(const InternalJob&) = delete;
		virtual ~InternalJob() = default;

		void Run() {
			_function();
		}

	private:
		std::function<void()> _function;
	};
}