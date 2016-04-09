#pragma once
// Hack
#ifdef USAGE_EXAMPLES

template <typename Unused>
inline void LockUsageExamples()
{
	// Critical section
	sfl::critical_section cs;

	{
		boost::lock_guard<sfl::critical_section> guard(cs);
		// critical section
	}

	// Slim Reader/Writer lock
	sfl::reader_writer_lock srw;

	{
		boost::lock_guard<sfl::reader_writer_lock::shared> guard(srw);
		// reader (shared) access
	}

	{
		boost::lock_guard<sfl::reader_writer_lock::exclusive> guard(srw);
		// writer (exclusive) access
	}
}
#endif // USAGE_EXAMPLES
#define _ASSERTE(x) if(!(x)) {throw "BobAssert";}
#include "clrhost.h"
// #include <boost/noncopyable.hpp>

// Disable warnings in boost::microsec_clock
// warning C4242: 'argument' : conversion from 'int' to 'unsigned short', possible loss of data
// warning C4244: 'argument' : conversion from 'int' to 'unsigned short', possible loss of data
//
// Boost uses prefix/suffix headers to sets/restore packing 
// warning C4103: alignment changed after including header, may be due to missing #pragma pack(pop)
#pragma warning(push)
#   pragma warning(disable : 4103 4242 4244)
//#   include <boost/thread/lock_guard.hpp>
#pragma warning(pop)
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
			m_critsec = ClrCreateCriticalSection(CrstGCMemoryPressure, CRST_DEFAULT);
		}


		//explicit critical_section(uint32_t spinCount)
		//{
		//    InitializeCriticalSectionAndSpinCount(&m_critsec, spinCount);
		//}


		//critical_section(uint32_t spinCount, uint32_t flags)
		//{
		//    InitializeCriticalSectionEx(&m_critsec, spinCount, flags);
		//}


		~critical_section()
		{

			ClrDeleteCriticalSection(&m_critsec);
		}


		void lock()
		{
			ClrEnterCriticalSection(&m_critsec);
		}


		//bool try_lock()
		//{
		//    return !!TryEnterCriticalSection(&m_critsec);
		//}


		void unlock()
		{
			ClrLeaveCriticalSection(&m_critsec);
		}

	private:
		CRITSEC_COOKIE m_critsec;
	};


	////
	//// Implementation of boost Mutex concept using Slim Reader/Writer (SRW) Lock
	////
	//class reader_writer_lock 
	//{
	//public:
	//    // shared (reader) accessor
	//    class shared
	//    {
	//    public:    
	//        void lock()
	//        {
	//            AcquireSRWLockShared(&m_srw);
	//        }
	//
	//
	//#if _WIN32_WINNT >= 0x0601
	//        // Win2k8 R2 only
	//        bool try_lock()
	//        {
	//            return !!TryAcquireSRWLockShared(&m_srw);
	//        }
	//#endif
	//
	//
	//        void unlock()
	//        {
	//            ReleaseSRWLockShared(&m_srw);
	//        }
	//
	//        friend class reader_writer_lock;
	//
	//    private:
	//        shared()
	//        {
	//            InitializeSRWLock(&m_srw);
	//        }
	//
	//        SRWLOCK m_srw;
	//    };
	//
	//
	//    // exclusive (writer) accessor
	//    class exclusive
	//    {
	//    public:    
	//        void lock()
	//        {
	//            AcquireSRWLockExclusive(m_srw);
	//        }
	//
	//
	//#if _WIN32_WINNT >= 0x0601    
	//        // Win2k8 R2 only
	//        bool try_lock()
	//        {
	//            return !!TryAcquireSRWLockExclusive(m_srw);
	//        }
	//#endif
	//
	//
	//        void unlock()
	//        {
	//            ReleaseSRWLockExclusive(m_srw);
	//        }
	//
	//        friend class reader_writer_lock;
	//
	//    private:
	//        exclusive(PSRWLOCK srw)
	//            : m_srw(srw)
	//        {}
	//
	//        PSRWLOCK m_srw;
	//    };
	//    
	//    
	//    reader_writer_lock()
	//        : m_exclusive(&m_shared.m_srw)
	//    {}
	//
	//    operator shared&()
	//    {
	//        return m_shared;
	//    }
	//
	//    operator exclusive&()
	//    {
	//        return m_exclusive;
	//    }
	//
	//private:
	//    shared      m_shared;
	//    exclusive   m_exclusive;
	//};
	//

}
