#pragma once
// Hack

#include "ArenaAllocator.h"

#include <stdint.h>

namespace sfl
{

	//
	// Implementation of boost Mutex concept (called Lockable in docs) using CRITICAL_SECTION
	//
	class critical_section
	{
	public:
		critical_section()
		{
			m_locked = 0;
			// Hack			
		}


		~critical_section()
		{
		}


		void lock()
		{
			while (1==::InterlockedCompareExchange(&m_locked, 1, 0));
		}


		void unlock()
		{
			while (0==::InterlockedCompareExchange(&m_locked, 0, 1));
		}

	private:
		volatile unsigned int m_locked;
	};



	template<class _Mutex>
	class lock_guard
	{	// class with destructor that unlocks mutex
	public:
		typedef _Mutex mutex_type;

		explicit lock_guard(_Mutex& _Mtx)
			: _MyMutex(_Mtx)
		{	// construct and lock
			_MyMutex.lock();
		}

		~lock_guard() noexcept
		{	// unlock
			_MyMutex.unlock();
		}

		lock_guard(const lock_guard&) = delete;
		lock_guard& operator=(const lock_guard&) = delete;
	private:
		_Mutex& _MyMutex;
	};
}
