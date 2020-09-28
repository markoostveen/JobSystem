#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <type_traits>

namespace JbSystem {
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

	class Job {
	public:
		Job() = delete;
		virtual void Free() = 0;
		const int GetId() const;
		const JobPriority GetPriority() const;

		inline void Run() const {
			_basefunction(this);
		}

		static const int RequestUniqueID();

	protected:
		typedef void(*Function)(const Job*);

		Job(const int id, const JobPriority priority, const Function callback);

		const Function _basefunction;
		const int _id;
		const JobPriority _priority;
	};

	//function with parameters
	template<class... Args>
	class JobWithParameters : public Job {
		using Parameters = std::tuple<Args...>;
	public:
		typedef void(*Function)(Args...);

		void Free() override { delete this; };

		JobWithParameters(const JobPriority priority, const Function function, Args... parameters) : JobWithParameters(Job::RequestUniqueID(), priority, function, std::forward<Args>(parameters)...) {}

		inline void Run() const {
			std::apply(_function, _parameters);
		}

		inline JobWithParameters operator=(const Job& otherJob) {
			return JobWithParameters(otherJob._function, otherJob._basefunction, otherJob._priority, otherJob._id, otherJob._parameters);
		}

	private:
		JobWithParameters(const int id, const JobPriority priority, const Function function, Args... parameters)
			: Job(id,
				priority,
				[](const Job* base) { static_cast<const JobWithParameters<Args...>*>(base)->Run(); }),
			_function(function), _parameters(parameters...) {
		}

		JobWithParameters(const Function function, const Job::Function baseFunction, const JobPriority priority, const int id, Parameters parameters)
			: JobWithParameters(id, priority, baseFunction), _function(function), _parameters(parameters) {
		}

		const Function _function;
		const Parameters _parameters;
	};

	template<class... Args>
	class JobSystemWithParametersJob : public JobWithParameters<Args...> {
	public:
		struct Tag {};
		typedef void(*DeconstructorCallback)(JobSystemWithParametersJob*);

		void Free() override {
			_deconstructorCallback(this);
		}

		JobSystemWithParametersJob(const JobPriority priority, const JobWithParameters<Args...>::Function function, DeconstructorCallback deconstructorCallback, Args... parameters)
			: JobWithParameters<Args...>(priority, function, std::forward<Args>(parameters)...), _deconstructorCallback(deconstructorCallback) {}

	private:
		const DeconstructorCallback _deconstructorCallback;
	};

	//void function
	class JobVoid : public Job {
	public:
		typedef void(*Function)();

		void Free() override { delete this; };

		JobVoid(const JobPriority priority, const Function function) : JobVoid(Job::RequestUniqueID(), function, priority) {}

		inline void Run() const {
			_function();
		}

		inline JobVoid operator=(const JobVoid& otherJob) {
			return JobVoid(otherJob._function, otherJob._basefunction, otherJob._priority, otherJob._id);
		}

	private:
		JobVoid(const int id, const Function function, const JobPriority priority)
			: Job(id,
				priority,
				[](const Job* base) { static_cast<const JobVoid*>(base)->Run(); }),
			_function(function) {
		}

		JobVoid(const Function function, const Job::Function baseFunction, const JobPriority priority, const int id)
			: Job(id, priority, baseFunction), _function(function) {
		}

		const Function _function;
	};

	class JobSystemVoidJob : public JobVoid {
	public:
		typedef void(*DeconstructorCallback)(JobSystemVoidJob*);

		void Free() override {
			_deconstructorCallback(this);
		}

		JobSystemVoidJob(const JobPriority priority, const Function function, const DeconstructorCallback deconstructorCallback)
			: JobVoid(priority, function), _deconstructorCallback(deconstructorCallback) {}

	private:
		const DeconstructorCallback _deconstructorCallback;
	};
}