#pragma once

#include <memory>
#include "boost/container/vector.hpp"
#include <functional>
#include <type_traits>
#include <cstdint>

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

	class JobId {
		// New kind of Id based on int, but without implicit conversion as it should not be used that way

	public:
		explicit JobId(const int& Id);
		bool operator==(const JobId&) const = default;

		[[nodiscard]] const int& ID() const;

	private:
		int _id;
	};

	/// <summary>
	/// Callback for the jobsystem to call into when checking if it's okay to execute another job when this job is being executed
	/// </summary>
	using IgnoreJobCallback = std::function<bool(const JobId& proposedJob)>;

	class Job {
	public:
		Job() = delete;
		virtual ~Job() = default;

		inline void Free() {
			_destructorfunction(this);
		}
		
		[[nodiscard]] JobId GetId() const; // Return copy it needs to be available after Job object get's destroyed

		inline void Run() const {
			_basefunction(this);
		}

		void SetIgnoreCallback(const IgnoreJobCallback& callback);
		[[nodiscard]] const IgnoreJobCallback& GetIgnoreCallback() const;

		void SetEmptyStackRequired(bool emptyStackRequired);
		[[nodiscard]] const bool& GetEmptyStackRequired() const;

		static JobId RequestUniqueID();

	protected:
		typedef void(*Function)(const Job* const&);
		typedef void(*DestructorFunction)(Job* const&);

		Job(const JobId& id, const Function& callback, const DestructorFunction& destructorfunction);

		const Function _basefunction;
		const DestructorFunction _destructorfunction;
		const JobId _id;
		IgnoreJobCallback _ignoreCallback;
		bool _requireEmptyJobStack;
	};

	//function with parameters
	template<class... Args>
	class JobWithParameters : public Job {
		using Parameters = std::tuple<Args...>;
	public:
		typedef void(*Function)(Args...);

		virtual ~JobWithParameters() = default;

		inline void Free() { delete this; };

		JobWithParameters(const Function& function, const Job::DestructorFunction& destructorFunction, Args... parameters) : JobWithParameters(Job::RequestUniqueID(), function, destructorFunction, parameters...) {}
		JobWithParameters(const Function& function, Args... parameters) : JobWithParameters(Job::RequestUniqueID(), function, [](Job* base) { static_cast<JobWithParameters<Args...>*>(base)->Free(); }, parameters...) {}
		inline void Run() const {
			std::apply(_function, _parameters);
		}

		inline JobWithParameters operator=(const JobWithParameters& otherJob) {
			return JobWithParameters(otherJob._function, otherJob._basefunction, otherJob._priority, otherJob._id, otherJob._parameters);
		}

	private:
		JobWithParameters(const JobId& id, const Function& function, const Job::DestructorFunction& destructorFunction, Args... parameters)
			: Job(id,
				[](const Job* const& base) { static_cast<const JobWithParameters<Args...>*>(base)->Run(); },
				destructorFunction),
			_function(function), _parameters(parameters...) {
		}

		const Function _function;
		const Parameters _parameters;
	};

	template<class... Args>
	class JobSystemWithParametersJob : public JobWithParameters<Args...> {
	public:
		struct Tag {};
		typedef void(*DeconstructorCallback)(JobSystemWithParametersJob* const&);

		virtual ~JobSystemWithParametersJob() = default;

		inline void Free() {
			_deconstructorCallback(this);
		}

		JobSystemWithParametersJob(const typename JobWithParameters<Args...>::Function& function, const DeconstructorCallback& deconstructorCallback, Args... parameters)
			: JobWithParameters<Args...>(function, [](Job* const& base) { static_cast<JobSystemWithParametersJob*>(base)->Free(); }, parameters...), _deconstructorCallback(deconstructorCallback) {}

	private:
		const DeconstructorCallback _deconstructorCallback;
	};

	//void function
	class JobVoid : public Job {
	public:
		typedef void(*Function)();

		virtual ~JobVoid() = default;


		inline void Free() { delete this; };


		explicit JobVoid(const Function& function, const Job::DestructorFunction& destructorFunction) : JobVoid(Job::RequestUniqueID(), function, destructorFunction) {}
		explicit JobVoid(const Function& function) : JobVoid(Job::RequestUniqueID(), function, [](Job* const& base) { static_cast<JobVoid*>(base)->Free(); }) {}

		inline void Run() const {
			_function();
		}

		inline JobVoid operator=(const JobVoid& otherJob) {
			return JobVoid(otherJob._function, otherJob._basefunction, otherJob._destructorfunction, otherJob._id);
		}

	private:
		JobVoid(const JobId& id, const Function& function, const Job::DestructorFunction& destructorFunction)
			: Job(id,
				[](const Job* const& base) { static_cast<const JobVoid*>(base)->Run(); },
				destructorFunction),
			_function(function) {
		}

		JobVoid(const Function& function, const Job::Function& baseFunction, const Job::DestructorFunction& destructorFunction, const JobId& id)
			: Job(id, baseFunction, destructorFunction), _function(function) {
		}

		const Function _function;
	};

	class JobSystemVoidJob : public JobVoid {
	public:
		typedef void(*DeconstructorCallback)(JobSystemVoidJob* const&);

		virtual ~JobSystemVoidJob() = default;


		inline void Free() {
			_deconstructorCallback(this);
		}

		JobSystemVoidJob(const Function& function, const DeconstructorCallback& deconstructorCallback)
			: JobVoid(function, [](Job* const& base) { static_cast<JobSystemVoidJob*>(base)->Free(); }), _deconstructorCallback(deconstructorCallback) {}

	private:
		const DeconstructorCallback _deconstructorCallback;
	};
}

// Add overload to make SimulatorId hashable for use in unordered_map
namespace std {
	template <>
	struct hash<JbSystem::JobId>
	{
		std::size_t operator()(const JbSystem::JobId& k) const
		{
			return hash<std::int32_t>()(k.ID());
		}
	};
}