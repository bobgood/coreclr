#pragma once

#include <assert.h>
#include "..\..\vm\eepolicy.h"

class ArenaThread
{
private:
	Arena* m_arena;
	char *m_next;
	char *m_end;
	size_t m_bufferSize;

public:
	ArenaThread()
	{

	}

	ArenaThread(Arena* arena, char *next, char *end,size_t bufferSize)
	{
		if ((size_t)arena < 0x40000000000)
		{
			assert(!"bad arena pointer");
		}
		m_arena = arena;
		m_next = next;
		m_end = end;
		m_bufferSize = bufferSize;
	}

	void SetBuffer(char* next, char* end)
	{
		m_next = next;
		m_end = end;
	}

	void* Allocate(size_t size, bool report=true);
};

	// ArenaAllocator
class Arena
{
	struct Buffer
	{
		char* m_addr;
		size_t m_len;
	};

	friend class ArenaThread;
private:
	static const size_t ThreadSafeBufferPreallocate = 8 * 1024;
	// within the fixed address space of a single arena are individual buffers
	// which are assigned to different threads, with one reserved for thread shared access.
	// each buffer doles out memory sequentially to anyone who asks.
	Buffer *m_buffers;

	// Total number of buffers allowed per arena
	int m_bufferCnt;

	// Number of buffers uses per arena
	int m_bufferNext;

	LONG m_lock0;
	LONG m_lock1;
	LONG m_lock2;
	LONG m_lock3;
	ArenaId m_id;

	// normal buffer size used (larger is allowed for overflow allocations)
	size_t m_bufferSize;

	// address that defines the end of this arena memory addresses that we can Virtual Alloc into
	size_t m_arenaEnd;

	// The thread safe access point for the the thread that created the arena
	// other access points can be created with SpawnArenaThread
	ArenaThread m_arenaThread;
	// for allocations from any thread that does not have access to its arenathread, requires a lock to use
	ArenaThread m_sharedArenaThread;

public:
	static Arena* MakeArena(ArenaId id, size_t requestAddress, size_t bufferSize, size_t maxPerArena)
	{
		LPVOID addr = ClrVirtualAlloc((LPVOID)requestAddress, bufferSize, MEM_COMMIT, PAGE_READWRITE);
		return new (addr) Arena(id, requestAddress, bufferSize, maxPerArena);
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

	ArenaThread* BaseArenaThread()
	{
		return &m_arenaThread;
	}

	ArenaThread* SpawnArenaThread()
	{
		SpinLock(m_lock2);
		ArenaThread* arenaThread = (ArenaThread*)m_arenaThread.Allocate(sizeof(ArenaThread));
		new (arenaThread) ArenaThread(this, nullptr, nullptr, m_bufferSize);
		GetNewBuffer(arenaThread);
		SpinUnlock(m_lock2);
		return arenaThread;
	}

	void SpinLock(LONG& lock)
	{
		while (0!=InterlockedCompareExchange(&lock, 1, 0));
	}

	void SpinUnlock(LONG& lock)
	{
		while (1 != InterlockedCompareExchange(&lock, 0, 1));
	}

	// c/dtor
	Arena(ArenaId id, size_t addr, size_t bufferSize, size_t maxPerArena)
	{
		assert((size_t)this == addr); // , "Arena should only be constructed through MakeArena");
		new (&m_arenaThread) ArenaThread(this, (char*)this + sizeof(Arena), (char*)this + bufferSize, bufferSize);

		m_bufferCnt = (int)( maxPerArena / bufferSize);
		m_buffers = (Buffer*)m_arenaThread.Allocate(m_bufferCnt*sizeof(Buffer*));
		m_buffers[0].m_addr = (char*)this;
		m_buffers[0].m_len = bufferSize;

		m_bufferSize = bufferSize;
		m_arenaEnd = (size_t)this + maxPerArena;
		m_bufferNext = 1;
		m_lock0 = 0;
		m_lock1 = 0;
		m_lock2 = 0;
		m_lock3 = 0;
		m_id = id;

		char* threadSafeBuffer = (char*)m_arenaThread.Allocate(ThreadSafeBufferPreallocate);
		new (&m_sharedArenaThread) ArenaThread(this, threadSafeBuffer, threadSafeBuffer + ThreadSafeBufferPreallocate, bufferSize);
	}

	char* VAlloc(size_t len)
	{
		if (m_bufferNext >= m_bufferCnt)
		{
			EEPOLICY_HANDLE_FATAL_ERROR(COR_E_OUTOFMEMORY);
		}

		void* addr = ArenaManager::GetBuffer(m_id, len);
		SpinLock(m_lock0);
		m_buffers[m_bufferNext].m_addr = (char*)addr;
		m_buffers[m_bufferNext++].m_len = len;
		assert(m_nextAddress < m_arenaEnd);
		SpinUnlock(m_lock0);
		::ArenaManager::Log("VirtualAlloc", (size_t)addr, len,nullptr,m_id);		
		return (char*)addr;
	}

	char*Overflow(size_t size)
	{
		return VAlloc(size);
	}

	void GetNewBuffer(ArenaThread* arenaThread)
	{	
		auto next = VAlloc(m_bufferSize);
		arenaThread->SetBuffer(next, next + m_bufferSize);
	}

	void Destroy()
	{
		for (int i = m_bufferNext - 1; i >= 0; i--)
		{
			auto addr = m_buffers[i].m_addr;
			auto len = m_buffers[i].m_len;
			ClrVirtualFree(addr, len, MEM_DECOMMIT);
		//::ArenaManager::Log("VirtualFree", (size_t)addr, len);

		}
	}

	void* ThreadSafeAllocate(size_t size)
	{
		SpinLock(m_lock1);
		auto ret = m_sharedArenaThread.Allocate(size, false);
		SpinUnlock(m_lock1);
		::ArenaManager::Log("clone Allocate", (size_t)ret, size);
		return ret;
	}
};

void* ArenaThread::Allocate(size_t size, bool report)
{
	for (;;)
	{
		char* after = m_next + size;
		if (after < m_end)
		{
			char* ret = m_next;
			m_next = after;
		
			memset(ret, 0, size);
			return ret;
		}
		else if (size < m_bufferSize)
		{
			m_arena->GetNewBuffer(this);
			
		}
		else
		{
			char* ret = m_arena->Overflow(size);
			memset(ret, 0, size);
			return ret;
		}
	}
}


