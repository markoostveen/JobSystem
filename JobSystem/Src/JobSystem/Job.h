#pragma once

#include <memory>
#include "boost/container/vector.hpp"
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
		inline void Free() {
			_destructorfunction(this);
		}
		const int GetId() const;

		inline void Run() const {
			_basefunction(this);
		}

		static const int RequestUniqueID();

	protected:
		typedef void(*Function)(const Job*);
		typedef void(*DestructorFunction)(Job*);

		Job(const int id, const Function callback, const DestructorFunction destructorfunction);

		const Function _basefunction;
		const DestructorFunction _destructorfunction;
		const int _id;
	};

	//function with parameters
	template<class... Args>
	class JobWithParameters : public Job {
		using Parameters = std::tuple<Args...>;
	public:
		typedef void(*Function)(Args...);

		inline void Free() { delete this; };

		JobWithParameters(const Function function, Job::DestructorFunction destructorFunction, Args... parameters) : JobWithParameters(Job::RequestUniqueID(), function, destructorFunction, std::forward<Args>(parameters)...) {}
		JobWithParameters(const Function function, Args... parameters) : JobWithParameters(Job::RequestUniqueID(), function, [](Job* base) { static_cast<JobWithParameters<Args...>*>(base)->Free(); }, std::forward<Args>(parameters)...) {}
		inline void Run() const {
			std::apply(_function, _parameters);
		}

		inline JobWithParameters operator=(const JobWithParameters& otherJob) {
			return JobWithParameters(otherJob._function, otherJob._basefunction, otherJob._priority, otherJob._id, otherJob._parameters);
		}

	private:
		JobWithParameters(const int id, const Function function, Job::DestructorFunction destructorFunction, Args... parameters)
			: Job(id,
				[](const Job* base) { static_cast<const JobWithParameters<Args...>*>(base)->Run(); },
				destructorFunction),
			_function(function), _parameters(parameters...) {
		}

		JobWithParameters(const Function function, const Job::Function baseFunction, const int id, Args... parameters)
			: Job(id, baseFunction), _function(function), _parameters(std::forward<Args>(parameters)...) {
		}

		const Function _function;
		const Parameters _parameters;
	};

	template<class... Args>
	class JobSystemWithParametersJob : public JobWithParameters<Args...> {
	public:
		struct Tag {};
		typedef void(*DeconstructorCallback)(JobSystemWithParametersJob*);

		inline void Free() {
			_deconstructorCallback(this);
		}

		JobSystemWithParametersJob(const typename JobWithParameters<Args...>::Function function, DeconstructorCallback deconstructorCallback, Args... parameters)
			: JobWithParameters<Args...>(function, [](Job* base) { static_cast<JobSystemWithParametersJob*>(base)->Free(); }, std::forward<Args>(parameters)...), _deconstructorCallback(deconstructorCallback) {}

	private:
		const DeconstructorCallback _deconstructorCallback;
	};

	//void function
	class JobVoid : public Job {
	public:
		typedef void(*Function)();

		inline void Free() { delete this; };

		JobVoid(const Function function, Job::DestructorFunction destructorFunction) : JobVoid(Job::RequestUniqueID(), function, destructorFunction) {}
		JobVoid(const Function function) : JobVoid(Job::RequestUniqueID(), function, [](Job* base) { static_cast<JobVoid*>(base)->Free(); }) {}

		inline void Run() const {
			_function();
		}

		inline JobVoid operator=(const JobVoid& otherJob) {
			return JobVoid(otherJob._function, otherJob._basefunction, otherJob._destructorfunction, otherJob._id);
		}

	private:
		JobVoid(const int id, const Function function, const Job::DestructorFunction destructorFunction)
			: Job(id,
				[](const Job* base) { static_cast<const JobVoid*>(base)->Run(); },
				destructorFunction),
			_function(function) {
		}

		JobVoid(const Function function, const Job::Function baseFunction, const Job::DestructorFunction destructorFunction, const int id)
			: Job(id, baseFunction, destructorFunction), _function(function) {
		}

		const Function _function;
	};

	class JobSystemVoidJob : public JobVoid {
	public:
		typedef void(*DeconstructorCallback)(JobSystemVoidJob*);

		inline void Free() {
			_deconstructorCallback(this);
		}

		JobSystemVoidJob(const Function function, const DeconstructorCallback deconstructorCallback)
			: JobVoid(function, [](Job* base) { static_cast<JobSystemVoidJob*>(base)->Free(); }), _deconstructorCallback(deconstructorCallback) {}

	private:
		const DeconstructorCallback _deconstructorCallback;
	};
}