// Hack
#pragma once

#include <vcruntime.h>
#include "arenastack.h"
#include "..\..\vm\threads.h"



class Arena;
class ArenaThread;

class ArenaManager
{
	// x64 process memory is 8192GB (8TB) (43 bits)
	// Each arena uses a fixed location in virtual memory, which are precalculated based on constants.
	// Each arena creates a set of buffers on demand, doubling size on each new buffer from
	// minBufferSize to maxBufferSize.

public:
	// number of arenas to support (and provide address spaces for.)
	// avoid very top of range >= 7f8'00000000
	static const int maxArenas = 0x3f8;
	static const int ArenaMask = 0x3ff;

	// The amount of address space allocated per arena (1<<32 == 4GB)
	static const int arenaAddressShift = 32;  // at most 4GB per arena total
	static const int addressBits = 43;

	// The base address of arenas (to distinguish arenas from other memory)
	static const size_t arenaBaseRequest = 1ULL << (addressBits - 1); // half of virtual address space reserved for arenas
	static const size_t arenaRangeEnd = arenaBaseRequest + ((size_t)maxArenas << arenaAddressShift);

	// Minimum and maximum size of buffers allocated to arenas (each new buffer is twice the size of the prior)
	static const size_t minBufferSize = 1ULL << 24;  //(16MB) min per arena
	static const size_t maxBufferSize = (1ULL << (arenaAddressShift - 1));

#ifdef _WIN64
	static const int headerSize = 8;
#else
	static const int headerSize = 4;
#endif

private:
	// Ensure all arena buffers fit in X64 virtual process memory.
	static_assert (((size_t)maxArenas << arenaAddressShift) + arenaBaseRequest <= (1ULL << 43), "arenas use too much memory");

	// Reservation system for all arenas.
	static unsigned long refCount[maxArenas];
	static void* arenaById[maxArenas];
	static HANDLE hFile;

public:
	static volatile __int64 totalMemory;

	static int lcnt;

private:
	static Arena* MakeArena();

	// Should be the same as arenaBaseRequest.  This is the actual location in virtual memory for all arenas.
	static size_t virtualBase;

	// The last arena ID allocated.  This is used to maximize the time between when an arena is destroyed, and
	// when the same virtual address space will be reused.
	static unsigned int lastId;

	// Converts an arena ID into a base address in virtual address space.
	static size_t IdToAddress(int id);


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
	inline static void* GetArena();

	// Address alignment copied from "gcpriv.h" 
	inline static size_t Align(size_t nbytes, int alignment = 7);

	inline static unsigned int Id(void * allocator);

	inline static void* AllocatorFromAddress(void * addr);

	static void CloneArray(void* dst, Object* src, PTR_MethodTable pMT, int ioffset, size_t size);

	static void CloneClass(void* dst, Object* src, PTR_MethodTable mt, int ioffset, size_t classSize);

	static void CloneClass1(void* dst, Object* src, PTR_MethodTable mt, int ioffset, size_t classSize);

public:
	// Initializes all Arena structures (call this once per process, before all other calls).
	static void InitArena();

	// This is the method that the user C# process calls to set the allocator state
	static void SetAllocator(unsigned int type);

	static int _cdecl GetArenaId();

	// returns null if no arena allocator is active, otherwise returns
	// a pointer to an allocated buffer
	static void* Allocate(size_t jsize);

	static void* Peek();

	// Returns a pointer to allocated memory from a specific arena
	static void* Allocate(ArenaThread* arena, size_t jsize);

	// Log method that writes to STD_OUTPUT
	static void Log(char* str, size_t n = 0, size_t n2 = 0, char*hdr = nullptr, size_t n3=0);

	// system code (i.e. JIT) that runs in the user thread should not use arenas.
	static void PushGC();
	static void Pop();

	static void* ArenaMarshal(void*, void*);

	static bool IsArenaAddress(void*p) {
		size_t a = (size_t)p;
		//return (0 != _bittest64((LONG64*)&a, addressBits - 1));
		return (a >= arenaBaseRequest);
	}

	static bool IsSameArenaAddress(void*p, void*q) {
		size_t a = ((size_t)p ^ (size_t)q) >> 32;
		return a == 0;
	}

	// Deep clones the src object, and returns a pointer.  The clone is done into the allocator
	// that holds the object target.
	static void* Marshal(void*src, void*target);

	static void VerifyObject(Object* o, MethodTable* pMT=nullptr);
	static void VerifyClass(Object* o, MethodTable* pMT = nullptr);
	static void VerifyArray(Object* o, MethodTable* pMT = nullptr);

	static ArenaStack& GetArenaStack()
	{
		return GetThread()->m_arenaStack;
	}
};


#define ISARENA(x) ::ArenaManager::IsArenaAddress(x)
#define ISSAMEARENA(x,y) ::ArenaManager::IsSameArenaAddress(x,y)


#define START_NOT_ARENA_SECTION ::ArenaManager::PushGC();
#define END_NOT_ARENA_SECTION ::ArenaManager::Pop();

//extern void* _stdcall ArenaMarshal(void*, void*);