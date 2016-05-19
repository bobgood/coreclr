#pragma once

#include "../common.h"
#include <assert.h>
#include <stdio.h>

typedef short ArenaId;
typedef int BufferId;

struct ArenaVirtualMemoryState
{
	BufferId m_nextSlot;
	BufferId m_startSlot;
	BufferId m_circleNextSlot;
	LONG m_lock;
};

#define ARENALOOKUP(x) (((ArenaId*)arenaBaseRequest)[x])

class ArenaVirtualMemory
{
public:
	static const int addressBits = 43;
	// The base address of arenas (to distinguish arenas from other memory)
	static const size_t arenaBaseRequest = 1ULL << (addressBits - 1); // half of virtual address space reserved for arenas

	static const size_t arenaBaseSize = (size_t)256 * 1024 * 1024 * 1024;  // 256GB max for arenas for now...
	static const size_t arenaRangeEnd = arenaBaseRequest + arenaBaseSize;

	static const size_t bufferReserveSize = 1024 * 1024;
	static const size_t bufferPadding = 16 * 1024;
	static const size_t bufferSize = bufferReserveSize - bufferPadding;
private:
	static const size_t maxBuffers = arenaBaseSize / bufferReserveSize;
	static const ArenaId available = (ArenaId)-1;
	static const ArenaId empty = (ArenaId)-2;


	static ArenaVirtualMemoryState s;

public:

	static void Initialize()
	{
		size_t allocNeeded = maxBuffers * sizeof(ArenaId) + bufferPadding * 2;
		size_t allocSize = (allocNeeded / bufferReserveSize + 1)*bufferReserveSize;
		auto virtualBase = (size_t)ClrVirtualAlloc((LPVOID)arenaBaseRequest, arenaBaseRequest/*arenaBaseSize*/, MEM_RESERVE, PAGE_NOACCESS);
		if ((size_t)virtualBase != arenaBaseRequest)
		{
			printf("failed to initialize virtual memory for arenas");
			MemoryException();
		}


		LPVOID addr = ClrVirtualAlloc((LPVOID)virtualBase, allocSize - bufferPadding, MEM_COMMIT, PAGE_READWRITE);
		if (addr == 0)
		{
			int err = GetLastError();
			printf("error %d\n", err);
		}
		if ((size_t)addr != arenaBaseRequest)
		{
			printf("failed to initialize virtual memory for arenas");
			MemoryException();
		}

		s.m_nextSlot = bufferReserveSize / bufferSize;
		s.m_startSlot = s.m_nextSlot;
		s.m_circleNextSlot = s.m_nextSlot;
	}

private:
	static BufferId Slots(size_t len)
	{
		return (BufferId)((len + bufferPadding - 1) / bufferReserveSize + 1);
	}

	static void SpinLock(LONG& lock)
	{
		while (0 != InterlockedCompareExchange(&lock, 1, 0));
	}

	static void SpinUnlock(LONG& lock)
	{
		while (1 != InterlockedCompareExchange(&lock, 0, 1));
	}

	static void* BufferIdToAddress(BufferId id)
	{
		return  (void*)(arenaBaseRequest + id*bufferReserveSize);
	}

	static BufferId BufferAddressToId(void* addr)
	{
		return (BufferId)(((size_t)addr - arenaBaseRequest) / bufferReserveSize);
	}

public:
	static bool IsArena(void*addr)
	{
		return ((size_t)addr >= arenaBaseRequest);
	}

	static ArenaId ArenaNumber(void*addr)
	{
		if (!IsArena(addr)) return -1;
		BufferId bid =  BufferAddressToId(addr);
		return ARENALOOKUP(bid);
	}

	static void FreeBuffer(void* addr, size_t len = bufferSize)
	{
		if (len == bufferSize)
		{
			BufferId first = BufferAddressToId(addr);
			int slots = Slots(len);
			for (BufferId i = first; i < first + slots; i++)
			{
				ARENALOOKUP(i) = available;
			}
		}
		else {
			ClrVirtualFree(addr, len, MEM_DECOMMIT);
			BufferId first = BufferAddressToId(addr);
			int slots = Slots(len);
			for (BufferId i = first; i < first + slots; i++)
			{
				ARENALOOKUP(i) = empty;
			}
		}
	}

	__declspec(noinline)
	static void* GetBuffer(ArenaId arenaId, size_t len = bufferSize)
	{
		assert(arenaId != empty && arenaId != available);
		SpinLock(s.m_lock);
		BufferId bufferId = 0;
		BufferId backupId = 0;
		void* ret = nullptr;

		if (len == bufferSize)
		{
			int cnt = s.m_nextSlot - s.m_startSlot;
			for (BufferId i = s.m_circleNextSlot; --cnt >= 0 && ret == nullptr; i = (i == s.m_nextSlot - 1) ? s.m_startSlot : i + 1)
			{
				if (ARENALOOKUP(i) == available)
				{
					bufferId = i;
					ret = BufferIdToAddress(bufferId);
				}
				else if (ARENALOOKUP(i) == empty)
				{
					backupId = i;
				}
			}
		}


		if (ret) {
			ARENALOOKUP(bufferId) = arenaId;
			SpinUnlock(s.m_lock);
			return ret;
		}

		if (backupId == 0)
		{
			bufferId = s.m_nextSlot;
			s.m_nextSlot += Slots(len);
			if (s.m_nextSlot > maxBuffers)
			{
				MemoryException();
			}
		}
		else
		{
			bufferId = backupId;
		}

		for (int i = bufferId; i < bufferId + Slots(len); i++)
		ARENALOOKUP(i) = arenaId;
		ret = BufferIdToAddress(bufferId);
		SpinUnlock(s.m_lock);

		LPVOID addr = ClrVirtualAlloc(ret, len, MEM_COMMIT, PAGE_READWRITE);

		if (addr != ret)
		{
			printf("failed to initialize virtual memory for arenas");
			MemoryException();
		}

		return ret;
	}

private:
	static void MemoryException()
	{
		EEPOLICY_HANDLE_FATAL_ERROR(COR_E_OUTOFMEMORY);

	}
};
