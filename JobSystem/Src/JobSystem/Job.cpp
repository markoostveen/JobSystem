#include "Job.h"

#include <atomic>

namespace JbSystem
{

    JobId Job::GetId() const
    {
        return _id;
    }

    static std::atomic<int> Identifier;
    void Job::SetIgnoreCallback(const IgnoreJobCallback& callback)
    {
        _ignoreCallback = callback;
    }
    const IgnoreJobCallback& Job::GetIgnoreCallback() const
    {
        return _ignoreCallback;
    }

    void Job::SetEmptyStackRequired(bool emptyStackRequired)
    {
        _requireEmptyJobStack = emptyStackRequired;
    }

    const bool& Job::GetEmptyStackRequired() const
    {
        return _requireEmptyJobStack;
    }

    JobId Job::RequestUniqueID()
    {
        return JobId{Identifier++};
    }

    Job::Job(const JobId& id, const Function& callback, const DestructorFunction& destructorfunction) :
        _basefunction(callback), _destructorfunction(destructorfunction), _id(id), _ignoreCallback(nullptr), _requireEmptyJobStack(false)
    {
    }

    JobId::JobId(const int& Id) : _id(Id)
    {
    }

    const int& JobId::ID() const
    {
        return _id;
    }
} // namespace JbSystem