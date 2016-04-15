#pragma once
// Hack

#include "clrhost.h"

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
			// Hack
			m_critsec = ClrCreateCriticalSection(CrstArenaAllocator, CRST_UNSAFE_ANYMODE);
		}


		~critical_section()
		{

			ClrDeleteCriticalSection(m_critsec);
		}


		void lock()
		{
			ClrEnterCriticalSection(m_critsec);
		}


		void unlock()
		{
			ClrLeaveCriticalSection(m_critsec);
		}

	private:
		CRITSEC_COOKIE m_critsec;
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
