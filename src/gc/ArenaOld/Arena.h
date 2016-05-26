// Hack
#pragma once

#include <vcruntime.h>
#include "../common.h"
#include "..\..\vm\threads.h"


//#define VERIFYALLOC

typedef short ArenaId;
typedef int BufferId;

class Arena;
class ArenaThread;
class ArenaStack;

////////////////////////////////////////////////////////
// ArenaStack
//
// A fixed sized Stack<size_t> that pretends it can get
// larger, but never allocates memory This class is 
// part of every thread object.  This object holds
// the Stack of Allocators for each thread.
////////////////////////////////////////////////////////

class ArenaStack
{
	// Depth of fixed stack depth for arenas in each thread
	// The stack can go deeper than this amount, but only
	// nullptr can be put into the deep stack, and thus
	// no storage is needed for this.
	static const int arenaStackDepth = 10;
	void* current = nullptr;
	void* stack[arenaStackDepth];
	int size = 0;
public:
	bool freezelog = false;

public:
	void* Current()
	{
		return current;
	}

	void Reset()
	{
		size = 0;
		current = nullptr;
	}

	int Size()
	{
		return size;
	}


	void* operator[](int offset)
	{
		if (offset >= arenaStackDepth) return nullptr;
		//assert(offset >= 0 && offset < size);
		return stack[offset];
	}

	void Push(void* v)
	{

		current = v;
		if (size >= arenaStackDepth)
		{
			assert(v == nullptr);
			size++;
		}
		else {
			stack[size++] = v;
		}
	}

	void* Pop()
	{
		DWORD written;
		assert(size > 0);
		auto ret = operator[](--size);
		if (size == 0) current = nullptr;
		current = operator[](size - 1);
		return ret;
	}
};

////////////////////////////////////////////////////////
// ArenaManager
//
// This class is the public interface to all Arena
// functionality (except ArenaStack is embedded in the thread object.
//
////////////////////////////////////////////////////////

class ArenaManager
{
public:
	// x64 process memory is 8192GB (8TB) (43 bits)
	// Each arena uses a fixed location in virtual memory, which are precalculated based on constants.
	// Each arena creates a set of buffers on demand, doubling size on each new buffer from
	// minBufferSize to maxBufferSize.
	static const int addressBits = 43;
	// The base address of arenas (to distinguish arenas from other memory)
	static const size_t arenaBaseRequest = 1ULL << (addressBits - 1); // half of virtual address space reserved for arenas

	static const size_t arenaBaseSize = (size_t)256 * 1024 * 1024 * 1024;  // 256GB max for arenas for now...
	static const size_t arenaRangeEnd = arenaBaseRequest + arenaBaseSize;

	static const size_t bufferReserveSize = 1024 * 1024;
	static const size_t bufferPadding = 16 * 1024;
	static const size_t bufferSize = bufferReserveSize - bufferPadding;

	static const int maxArenas = 4096;
private:
	// Reservation system for all arenas.
	static unsigned long refCount[maxArenas];
	static void* arenaById[maxArenas];
#ifdef LOGGING
	static HANDLE hFile;
	static int lcnt;


#endif

	static Arena* MakeArena();

	// The last arena ID allocated.  This is used to maximize the time between when an arena is destroyed, and
	// when the same virtual address space will be reused.
	static unsigned int lastId;

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
		ArenaId id = GetArenaId(addr);
		if (id == -1) return nullptr;
		return arenaById[id];
	}

	static void RegisterForFinalization(Object* o, size_t size);

	// Gets the arena ID given an address of an object, arena or arenathread
	static ArenaId GetArenaId(void*addr);


public:
	// Initializes all Arena structures (call this once per process, before all other calls).
	static void InitArena();

	// This is the method that the user C# process calls to set the allocator state
	// 1 = reset to GCHeap
	// 2 = push new arena allocator
	// 3 = push GCHeap
	// 4 = pop
	// 1024+ push old arena
	static void SetAllocator(unsigned int type);

	// Gets the arenaID for the current arena in this thread,
	// returns -1, if no arena is the current allocator for this thread.
	static ArenaId GetArenaId();

	// returns null if no arena allocator is active, otherwise returns
	// a pointer to an allocated buffer
	static void* Allocate(size_t jsize, uint32_t flags);

	// returns the address of the current ArenaThread object for this thread
	static void* Peek();

	// Returns a pointer to allocated memory from a specific arena
	static void* Allocate(ArenaThread* arena, size_t jsize);

	// Log method that writes to STD_OUTPUT
	static void Log(char* str, size_t n = 0, size_t n2 = 0, char*hdr = nullptr, size_t n3=0);

	// registers the address of an arena allocated object for
	// later verification
	static void RegisterAddress(void* addr)
	{
#ifdef VERIFYALLOC
		auto p = (size_t**)0x60000000000;
		*(*p)++ = (size_t)addr;
#else
		UNREFERENCED_PARAMETER(addr);
#endif
	}

#ifdef VERIFYALLOC
	// verifies the correctness of any registered object
	__declspec(noinline)
		static void VerifyAllArenaObjects();
#endif

	// Pushes nullptr onto this threads arena stack to signify
	// that no arena should be used for this thread 
	static void PushGC()
	{
		GetArenaStack().Push(nullptr);
	}

	// Pop's the state of the arena stack for this thread
	// to the state prior to the last push.
	static void Pop() {
		void* allocator = GetArenaStack().Pop();
		if (allocator != nullptr)
		{
			DereferenceId(GetArenaId(allocator));
		}
	}

	// Pushes nullptr onto a different threads arena stack to signify
	// that no arena should be used for this thread 
	static void PushGC(Thread* thread)
	{
		GetArenaStack(thread).Push(nullptr);
	}

	// Pop's the state of the arena stack for a specified thread
	// to the state prior to the last push.
	static void Pop(Thread* thread) {
		void* allocator = GetArenaStack(thread).Pop();
		if (allocator != nullptr)
		{
			DereferenceId(GetArenaId(allocator));
		}
	}

	// Clones the object with address src, and places
	// a pointer to the cloned object at target.
	static void ArenaMarshal(void*target, void*src);

	// True if the supplied pointer is within the arena
	// address space (the top half of the address space for
	// the process).
	static bool IsArenaAddress(void*p) {
		return (0!=((size_t)p & (1ULL << (addressBits - 1))));
	}

	// True if p and q are both pointers within the same arena
	static bool IsSameArenaAddress(void*p, void*q) {
		size_t a = ((size_t)p ^ (size_t)q) >> 32;
		return a == 0;
	}

#ifdef VERIFYOBJECTS
	static void VerifyObject(Object* o, MethodTable* pMT = nullptr);
	static void VerifyClass(Object* o, MethodTable* pMT = nullptr);
	static voAddBufferGetBufferid VerifyArray(Object* o, MethodTable* pMT = nullptr);
#endif

	// Gets the ArenaStack for the current thread.
	static ArenaStack& GetArenaStack();

	// Gets the ArenaStack for a specified thread.
	static ArenaStack& GetArenaStack(Thread* thread);

	// Creates a buffer in the arena virtual address space
	static void* CreateBuffer(ArenaId arenaId, size_t len = ArenaManager::bufferSize);

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
