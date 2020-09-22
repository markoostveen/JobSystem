#pragma once
#include <memory>
#include <vector>
#include <functional>
#include <type_traits>

namespace JbSystem {
	//Function able to call a function with parameters
	template<class... Args>
	class Job;

	enum class JobPriority {
		/// <summary>
		/// Doesn't represent any job
		/// </summary>
		None = -1,

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

	class JobBase {
	public:
		JobBase() = delete;
		virtual ~JobBase() = default;
		const int GetId() const;
		const JobPriority GetPriority() const;

		inline void Run() const {
			_basefunction(this);
		}

	protected:
		typedef void(*Function)(const JobBase*);

		JobBase(const JobPriority priority, const Function callback);
		JobBase(const int id, const JobPriority priority, const Function callback);

		const Function _basefunction;
		const int _id;
		const JobPriority _priority;
	};

	//function with parameters
	template<class... Args>
	class Job : public JobBase {
		typedef std::tuple<Args...> Parameters;
	public:
		typedef void(*Function)(Args...);

		virtual ~Job() = default;

		Job(const JobPriority priority, const Function function, Args... parameters)
			: JobBase(priority,
				[](auto base) { static_cast<const Job<Args...>*>(base)->Run(); }),
			_function(function), _parameters(parameters...) {
		}

		inline void Run() const {
			std::apply(_function, _parameters);
		}

		Job operator=(const Job& otherJob) {
			return Job(otherJob._function, otherJob._basefunction, otherJob._priority, otherJob._id, otherJob._parameters);
		}

	private:
		Job(const Function function, const JobBase::Function baseFunction, const JobPriority priority, const int id, Parameters parameters)
			: JobBase(id, priority, baseFunction), _function(function), _parameters(parameters) {
		}
		Function _function;
		const Parameters _parameters;
	};

	//void function
	template<>
	class Job<> : public JobBase {
	public:
		typedef void(*Function)();

		Job(Function function, const JobPriority priority)
			: JobBase(priority,
				[](auto base) { static_cast<const Job<>*>(base)->Run(); }),
			_function(function) {
		}

		void Run() const {
			_function();
		}

		Job operator=(const Job& otherJob) {
			return Job(otherJob._function, otherJob._basefunction, otherJob._priority, otherJob._id);
		}

	private:
		Job(const Function function, const JobBase::Function baseFunction, const JobPriority priority, const int id)
			: JobBase(id, priority, baseFunction), _function(function) {
		}

		Function _function;
	};
}