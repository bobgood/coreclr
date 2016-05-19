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
	inline static void* GetArena();

	// Address alignment copied from "gcpriv.h" 
	inline static size_t Align(size_t nbytes, int alignment = 7);

	inline static void* AllocatorFromAddress(void * addr);

	static void CloneArray(void* dst, Object* src, PTR_MethodTable pMT, int ioffset, size_t size);

	static void CloneClass(void* dst, Object* src, PTR_MethodTable mt, int ioffset, size_t classSize);

	static void CloneClass1(void* dst, Object* src, PTR_MethodTable mt, int ioffset, size_t classSize);

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
	static void PushGC();
	static void Pop();

	static void* ArenaMarshal(void*, void*);

	static bool IsArenaAddress(void*p) {
		size_t a = (size_t)p;
		//return (0 != _bittest64((LONG64*)&a, addressBits - 1));
		return (a >= ArenaVirtualMemory::arenaBaseRequest);
	}

	static bool IsSameArenaAddress(void*p, void*q) {
		size_t a = ((size_t)p ^ (size_t)q) >> 32;
		return a == 0;
	}

	// Deep clones the src object, and returns a pointer.  The clone is done into the allocator
	// that holds the object target.
	static void* Marshal(void*src, void*target);

	static void VerifyObject(Object* o, MethodTable* pMT = nullptr);
	static void VerifyClass(Object* o, MethodTable* pMT = nullptr);
	static void VerifyArray(Object* o, MethodTable* pMT = nullptr);

	static ArenaStack& GetArenaStack()
	{
		return GetThread()->m_arenaStack;
	}

	static void* GetBuffer(ArenaId arenaId, size_t len = ArenaVirtualMemory::bufferSize);
};


#define ISARENA(x) ::ArenaManager::IsArenaAddress(x)
#define ISSAMEARENA(x,y) ::ArenaManager::IsSameArenaAddress(x,y)


#define START_NOT_ARENA_SECTION ::ArenaManager::PushGC();
#define END_NOT_ARENA_SECTION ::ArenaManager::Pop();

//extern void* _stdcall ArenaMarshal(void*, void*);