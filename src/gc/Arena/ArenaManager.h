// Hack
#pragma once

#include <vcruntime.h>
#include "arenastack.h"
#include "arenavirtualMemory.h"
#include "..\..\vm\threads.h"


#define NOT_VERIFYALLOC

class Arena;
class ArenaThread;

class ArenaManager
{
	// x64 process memory is 8192GB (8TB) (43 bits)
	// Each arena uses a fixed location in virtual memory, which are precalculated based on constants.
	// Each arena creates a set of buffers on demand, doubling size on each new buffer from
	// minBufferSize to maxBufferSize.

public:
	static const int maxArenas = 4096;
private:
	// Reservation system for all arenas.
	static unsigned long refCount[maxArenas];
	static void* arenaById[maxArenas];
	static HANDLE hFile;
	
public:
	static volatile __int64 totalMemory;

	static int lcnt;

private:
	static Arena* MakeArena();

	// The last arena ID allocated.  This is used to maximize the time between when an arena is destroyed, and
	// when the same virtual address space will be reused.
	static unsigned int lastId;


private:
	// Deletes an Arena and releases all its memory.
	static void DeleteAllocator(void*);

	// Gets the next available Arena ID
	inline static int getId();

	// decrements the reference count, and releases the arena if zero
	static void DereferenceId(int id);

	// adds to the reference count
	static void ReferenceId(int id);

	// Gets the allocator at the top of the stack
	static void* ArenaManager::GetArena()
	{
		return GetArenaStack().Current();
	}
	
	static size_t Align(size_t nbytes)
	{
#ifdef AMD64
		return (nbytes + 7) & ~7;
#else
		return (nbytes + 3) & ~3;
#endif
	}

	static void* AllocatorFromAddress(void * addr)
	{
		ArenaId id = ArenaVirtualMemory::ArenaNumber(addr);
		if (id == -1) return nullptr;
		return arenaById[id];
	}


	static void RegisterForFinalization(Object* o, size_t size);

public:
	// Initializes all Arena structures (call this once per process, before all other calls).
	static void InitArena();

	// This is the method that the user C# process calls to set the allocator state
	static void SetAllocator(unsigned int type);

	static ArenaId _cdecl GetArenaId();

	// returns null if no arena allocator is active, otherwise returns
	// a pointer to an allocated buffer
	static void* Allocate(size_t jsize, uint32_t flags);

	static void* Peek();

	// Returns a pointer to allocated memory from a specific arena
	static void* Allocate(ArenaThread* arena, size_t jsize);

	// Log method that writes to STD_OUTPUT
	static void Log(char* str, size_t n = 0, size_t n2 = 0, char*hdr = nullptr, size_t n3=0);

	static void RegisterAddress(void* addr)
	{
#ifdef VERIFYALLOC
		auto p = (size_t**)0x60000000000;
		*(*p)++ = (size_t)addr;
#else
		UNREFERENCED_PARAMETER(addr);
#endif
	}

	__declspec(noinline)
		static void CheckAll();

	// system code (i.e. JIT) that runs in the user thread should not use arenas.
	static void PushGC()
	{
		GetArenaStack().Push(nullptr);
	}

	static void Pop() {
		void* allocator = GetArenaStack().Pop();
		if (allocator != nullptr)
		{
			DereferenceId(ArenaVirtualMemory::ArenaNumber(allocator));
		}
	}

	static void PushGC(Thread* thread)
	{
		GetArenaStack(thread).Push(nullptr);
	}

	static void Pop(Thread* thread) {
		void* allocator = GetArenaStack(thread).Pop();
		if (allocator != nullptr)
		{
			DereferenceId(ArenaVirtualMemory::ArenaNumber(allocator));
		}
	}


	static void* ArenaMarshal(void*, void*);

	static bool IsArenaAddress(void*p) {
		return ((size_t)p >> ArenaVirtualMemory::addressBits) == 1;
	}

	static bool IsSameArenaAddress(void*p, void*q) {
		size_t a = ((size_t)p ^ (size_t)q) >> 32;
		return a == 0;
	}


	static void VerifyObject(Object* o, MethodTable* pMT = nullptr);
	static void VerifyClass(Object* o, MethodTable* pMT = nullptr);
	static void VerifyArray(Object* o, MethodTable* pMT = nullptr);

	static ArenaStack& GetArenaStack()
	{
		return GetThread()->m_arenaStack;
	}

	static ArenaStack& GetArenaStack(Thread* thread)
	{
		return thread->m_arenaStack;
	}

	static void* GetBuffer(ArenaId arenaId, size_t len = ArenaVirtualMemory::bufferSize);

#if defined(BIT64)
	// Card byte shift is different on 64bit.
#define card_byte_shift     11
#else
#define card_byte_shift     10
#endif

#define card_byte(addr) (((size_t)(addr)) >> card_byte_shift)

	// to guarantee inline, this code is cloned from GCSample
	static void ErectWriteBarrier(Object ** dst, Object * ref)
	{
#if !defined(DACCESS_COMPILE)
		// if the dst is outside of the heap (unboxed value classes) then we
		//      simply exit
		if (((uint8_t*)dst < g_lowest_address) || ((uint8_t*)dst >= g_highest_address))
			return;

		if ((uint8_t*)ref >= g_ephemeral_low && (uint8_t*)ref < g_ephemeral_high)
		{
			// volatile is used here to prevent fetch of g_card_table from being reordered 
			// with g_lowest/highest_address check above. See comment in code:gc_heap::grow_brick_card_tables.
			uint8_t* pCardByte = (uint8_t *)*(volatile uint8_t **)(&g_card_table) + card_byte((uint8_t *)dst);
			if (*pCardByte != 0xFF)
				*pCardByte = 0xFF;
		}
#endif
	}

};


#define ISARENA(x) ::ArenaManager::IsArenaAddress(x)
#define ISSAMEARENA(x,y) ::ArenaManager::IsSameArenaAddress(x,y)


#define START_NOT_ARENA_SECTION ::ArenaManager::PushGC();
#define END_NOT_ARENA_SECTION ::ArenaManager::Pop();

//extern void* _stdcall ArenaMarshal(void*, void*);