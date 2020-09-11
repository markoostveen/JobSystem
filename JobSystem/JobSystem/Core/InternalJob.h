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

	class Job {
	public:
		Job() = delete;
		template<JobFunction T>
		Job(const T function, const JobPriority length) :Job(length) {
			_function = function;
		}

		const int GetId() const;
		const JobPriority GetTimeInvestment() const;
		void Run() const;

	private:
		Job(JobPriority length);
		const int _id;
		const JobPriority _timeInvestment;
		std::function<void()> _function;
	};
}