#pragma once
// Hack
//#include <type_traits>
#include "debugmacros.h"
namespace sfl
{


namespace policy
{


// Policy to make arena allocator safe to use from multiple threads
class ThreadSafe;


// Policy to make arena allocator for single thread usage, with no
// overhead for thread safety.
class SingleThread;


// Policy to make arena allocator which keeps count of de/allocations and
// asserts that all allocations have been freed when arena is released. For
// every call to Allocate, applications must make a matching call to Deallocate.
template <typename ConcurrencyPolicy>
class RefcountValidation;


// Policy that makes arena allocator which doesn't perform any validations. 
// Calling ArenaAllocator::Deallocate is optional.
class NoValidation;


// Policy that makes arena allocator safe to use from multiple threads
class ThreadSafe
{
public:
    typedef sfl::critical_section   Lock;

    
    static void MemoryBarrier()
    {
        ::MemoryBarrier();
    }
    
    
    static long Increment(volatile long& dest)
    {
        return InterlockedIncrement(&dest);
    }


    static long Decrement(volatile long& dest)
    {
        return InterlockedDecrement(&dest);
    }

    
    template<typename T>
    static bool CompareExchange(volatile T& dest, const T& new_value, const T& old_value, 
        typename std::enable_if<sizeof(T) == sizeof(int64_t)>::type* = 0)
    {
        return reinterpret_cast<const int64_t&>(old_value) == InterlockedCompareExchange64(
                        reinterpret_cast<volatile int64_t*>(&dest), 
                        reinterpret_cast<const int64_t&>(new_value), 
                        reinterpret_cast<const int64_t&>(old_value));
    }
};


// Policy that makes arena allocator for single thread usage
class SingleThread
{
public:
    struct Lock
    {
        void lock()
        {}

        bool try_lock()
        {
            return true;
        }

        void unlock()
        {}
    };

    
    static void MemoryBarrier()
    {}

    
    static long Increment(volatile long& dest)
    {
        return dest++;
    }


    static long Decrement(volatile long& dest)
    {
        return dest--;
    }

    
    template<typename T>
    static bool CompareExchange(volatile T& dest, const T& new_value, const T& /*old_value*/)
    {
        dest = new_value;
        return true;
    }
};


// Policy that makes arena allocator which keeps reference count of de/allocations 
// and asserts that all allocations have been freed when arena is released. 
// For every Allocate applications must make a matching call to Deallocate.
template <typename ConcurrencyPolicy>
class RefcountValidation
{
protected:
    RefcountValidation()
        : m_refCount(0)
    {}

    
    void Allocate(size_t)
    {
        ConcurrencyPolicy::Increment(m_refCount);
    }

    
    void Deallocate(void*)
    {
        ConcurrencyPolicy::Decrement(m_refCount);
    }

    
    void Reset()
    {
        assert(m_refCount == 0);
    }

    volatile long m_refCount; 
};


// Policy that makes arena allocator which doesn't perform any validations 
class NoValidation
{
protected:
    void Allocate(size_t)
    {}

    
    void Deallocate(void*)
    {}

    
    void Reset()
    {}
};


}; // namespace policy

}; // namespace sfl
